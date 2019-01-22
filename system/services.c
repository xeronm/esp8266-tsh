/* 
 * ESP8266 Services Catalog and Control
 * Copyright (c) 2018 Denis Muratov <xeronm@gmail.com>.
 * https://dtec.pro/gitbucket/git/esp8266/esp8266-tsh.git
 *
 * This file is part of ESP8266 Things Shell.
 *
 * ESP8266 Things Shell is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ESP8266 Things Shell is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ESP8266 Things Shell.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include "sysinit.h"
#include "core/utils.h"
#include "core/logging.h"
#include "core/system.h"
#include "system/services.h"
#include "system/imdb.h"
#include "system/comavp.h"

#define	SERVICES_SERVICE_NAME			"svcs"

#define SERVICES_DATA_STORAGE_PAGES		1
#define SERVICES_DATA_STORAGE_PAGE_BLOCKS	2

#define SERVICES_STORAGE_PAGES		1
#define SERVICES_STORAGE_PAGE_BLOCKS	1

#define SERVICES_CONFIG_STORAGE_PAGES		8
#define SERVICES_CONFIG_STORAGE_PAGE_BLOCKS	4

#define SERVICES_IMDB_CLS_DATA		"svcs$data"
#define SERVICES_IMDB_CLS_SERVICE	"svcs$service"
#define SERVICES_IMDB_CLS_CONFIG	"svcs$conf"

#define SVCS_INFO_ARRAY_SZIE		20

#define d_check_is_run(void) \
	if (!sdata) \
		return SVCS_NOT_RUN;


typedef struct svcs_service_conf_s {
    service_ident_t service_id;
    svcs_cfgtype_t  cfgtype: 2;
    bool            disabled: 1;
    uint8           reserved: 5;
    os_time_t       utime;
    dtlv_size_t     varlen;
    ALIGN_DATA char vardata[];
} svcs_service_conf_t;

typedef struct svcs_service_s {
    svcs_service_info_t info;
    // handlers
    svcs_on_start_t on_start;
    svcs_on_stop_t  on_stop;
    svcs_on_message_t on_message;
    svcs_on_cfgupd_t on_cfgupd;
    bool            multicast;
    // configuration
    svcs_service_conf_t *conf;
} svcs_service_t;

typedef struct services_data_s {
    svcs_resource_t svcres;
    imdb_hndlr_t    hconf;
    imdb_hndlr_t    hsvcs;
} services_data_t;

static services_data_t *sdata = NULL;

typedef struct svcs_find_ctx_s {
    service_ident_t service_id;
    const char     *name;
    svcs_service_t *svc;
} svcs_find_ctx_t;

typedef struct svcs_find_conf_ctx_s {
    svcs_cfgtype_t  cfgtype;
    service_ident_t service_id;
    imdb_rowid_t    rowid;
    svcs_service_conf_t *conf;
} svcs_find_conf_ctx_t;


LOCAL imdb_errcode_t ICACHE_FLASH_ATTR
svcctl_forall_conf_find (imdb_fetch_obj_t * fobj, void *data)
{
    svcs_service_conf_t *conf = d_pointer_as (svcs_service_conf_t, fobj->dataptr);
    svcs_find_conf_ctx_t *find_conf_ctx = d_pointer_as (svcs_find_conf_ctx_t, data);
    if ((find_conf_ctx->service_id == conf->service_id) && (find_conf_ctx->cfgtype == conf->cfgtype)) {
        os_memcpy (&find_conf_ctx->rowid, &fobj->rowid, sizeof (imdb_rowid_t));
        find_conf_ctx->conf = conf;
        return IMDB_CURSOR_BREAK;
    }
    return IMDB_ERR_SUCCESS;
}

LOCAL imdb_errcode_t ICACHE_FLASH_ATTR
svcctl_forall_conf_clean (imdb_fetch_obj_t * fobj, void *data)
{
    svcs_service_conf_t *conf = d_pointer_as (svcs_service_conf_t, fobj->dataptr);
    if (conf->cfgtype == SVCS_CFGTYPE_NEW) {
        return imdb_clsobj_delete (sdata->svcres.hfdb, sdata->hconf, data);
    }
    return IMDB_ERR_SUCCESS;
}

LOCAL svcs_errcode_t ICACHE_FLASH_ATTR
svcctl_find_conf (service_ident_t service_id, svcs_service_conf_t ** conf, svcs_cfgtype_t cfgtype, bool update)
{
    svcs_find_conf_ctx_t find_conf_ctx;
    find_conf_ctx.service_id = service_id;
    find_conf_ctx.cfgtype = cfgtype;
    find_conf_ctx.conf = NULL;

    d_svcs_check_imdb_error (imdb_class_forall
                             (sdata->svcres.hfdb, sdata->hconf, &find_conf_ctx, svcctl_forall_conf_find)
        );

    if (update && find_conf_ctx.conf)
        d_svcs_check_imdb_error (imdb_clsobj_update (sdata->svcres.hfdb, &find_conf_ctx.rowid, NULL));

    *conf = find_conf_ctx.conf;
    return (*conf) ? SVCS_ERR_SUCCESS : SVCS_NOT_EXISTS;
}

LOCAL imdb_errcode_t ICACHE_FLASH_ATTR
svcctl_svc_stop (svcs_service_t * svc)
{
    switch (svc->info.state) {
    case SVCS_STATE_STOPPED:
    case SVCS_STATE_FAILED:
        return SVCS_NOT_RUN;
        break;
    case SVCS_STATE_RUNNING:
        break;
    default:
        return SVCS_SERVICE_ERROR;
        break;
    }

    svc->info.state = SVCS_STATE_STOPING;
    //d_log_iprintf (SERVICES_SERVICE_NAME, "\"%s\" stoping...", svc->info.name);
    svc->info.errcode = svc->on_stop ();
    svc->info.state = (svc->info.errcode == SVCS_ERR_SUCCESS) ? SVCS_STATE_STOPPED : SVCS_STATE_FAILED;
    svc->info.state_time = system_get_time ();
    if (svc->info.state == SVCS_STATE_STOPPED) {
        d_log_wprintf (SERVICES_SERVICE_NAME, "\"%s\" stoped", svc->info.name);
    }
    else {
        d_log_eprintf (SERVICES_SERVICE_NAME, "\"%s\" failed to stop", svc->info.name);
    }
    return svc->info.errcode;
}

LOCAL imdb_errcode_t ICACHE_FLASH_ATTR
svcctl_svc_start (svcs_service_t * svc, bool ignore_disabled)
{
    switch (svc->info.state) {
    case SVCS_STATE_STOPPED:
    case SVCS_STATE_FAILED:
        break;
    case SVCS_STATE_RUNNING:
        return SVCS_ALREADY_RUN;
        break;
    default:
        return SVCS_SERVICE_ERROR;
        break;
    }

    svcs_service_conf_t *conf_data;
    dtlv_ctx_t      conf;
    dtlv_ctx_t     *conf_ptr = NULL;
    if (!system_get_safe_mode ()) {
        svcs_errcode_t  res = svcctl_find_conf (svc->info.service_id, &conf_data, SVCS_CFGTYPE_CURRENT, false);
        if (res == SVCS_ERR_SUCCESS) {
            conf_ptr = &conf;
            d_svcs_check_dtlv_error (dtlv_ctx_init_decode (conf_ptr, conf_data->vardata, conf_data->varlen));
        }
        else if (res != SVCS_NOT_EXISTS) {
            d_log_wprintf (SERVICES_SERVICE_NAME, "\"%s\" config res:%u", svc->info.name, res);
        }
    }

    if (conf_ptr && !ignore_disabled && conf_data->disabled)
        return SVCS_DISABLED;

    svc->info.state = SVCS_STATE_STARTING;
    //d_log_iprintf (SERVICES_SERVICE_NAME, "\"%s\" starting...", svc->info.name);
    svc->info.errcode = svc->on_start ((const svcs_resource_t *) &sdata->svcres, conf_ptr);
    svc->info.state = (svc->info.errcode == SVCS_ERR_SUCCESS) ? SVCS_STATE_RUNNING : SVCS_STATE_FAILED;
    svc->info.state_time = system_get_time ();
    if (svc->info.state == SVCS_STATE_RUNNING) {
        d_log_wprintf (SERVICES_SERVICE_NAME, "\"%s\" started", svc->info.name);
    }
    else {
        d_log_eprintf (SERVICES_SERVICE_NAME, "\"%s\" failed to run", svc->info.name);
    }
    return svc->info.errcode;
}

LOCAL imdb_errcode_t ICACHE_FLASH_ATTR
svcctl_forall_stop (imdb_fetch_obj_t * fobj, void *data)
{
    svcs_service_t *svc = d_pointer_as (svcs_service_t, fobj->dataptr);
    if (svc->info.state != SVCS_STATE_RUNNING) {
        return IMDB_ERR_SUCCESS;
    }

    svcctl_svc_stop (svc);
    return IMDB_ERR_SUCCESS;
}

LOCAL imdb_errcode_t ICACHE_FLASH_ATTR
svcctl_forall_find (imdb_fetch_obj_t * fobj, void *data)
{
    svcs_service_t *svc = d_pointer_as (svcs_service_t, fobj->dataptr);
    svcs_find_ctx_t *find_ctx = d_pointer_as (svcs_find_ctx_t, data);
    if ((!find_ctx->service_id || svc->info.service_id == find_ctx->service_id) &&
        (!find_ctx->name || os_strncmp (svc->info.name, find_ctx->name, sizeof (service_name_t)) == 0)
        ) {
        find_ctx->svc = svc;
        return IMDB_CURSOR_BREAK;
    }
    return IMDB_ERR_SUCCESS;
}

LOCAL svcs_errcode_t ICACHE_FLASH_ATTR
svcctl_find (service_ident_t service_id, const char *name, svcs_service_t ** svc)
{
    if (!sdata)
        return SVCS_NOT_RUN;

    svcs_find_ctx_t find_ctx;
    find_ctx.service_id = service_id;
    find_ctx.name = name;
    find_ctx.svc = NULL;

    d_svcs_check_imdb_error (imdb_class_forall (sdata->svcres.hmdb, sdata->hsvcs, &find_ctx, svcctl_forall_find)
        );

    *svc = find_ctx.svc;
    return (*svc) ? SVCS_ERR_SUCCESS : SVCS_NOT_EXISTS;
}


typedef struct svcs_message_ctx_s {
    service_ident_t orig_id;
    void           *ctxdata;
    service_msgtype_t msgtype;
    dtlv_ctx_t     *msg_in;
    dtlv_ctx_t     *msg_out;
} svcs_message_ctx_t;

LOCAL imdb_errcode_t ICACHE_FLASH_ATTR
svcctl_forall_message (imdb_fetch_obj_t * fobj, void *data)
{
    svcs_service_t *svc = d_pointer_as (svcs_service_t, fobj->dataptr);
    svcs_message_ctx_t *ctx = d_pointer_as (svcs_message_ctx_t, data);

    if ((svc->info.state != SVCS_STATE_RUNNING) || (!svc->on_message) || (!svc->multicast)) {
        return IMDB_ERR_SUCCESS;
    }

    svcs_errcode_t  ret = svc->on_message (ctx->orig_id, ctx->msgtype, ctx->ctxdata, ctx->msg_in, ctx->msg_out);
    if ((ret != SVCS_ERR_SUCCESS) && (ret != SVCS_MSGTYPE_INVALID))
        d_log_wprintf (SERVICES_SERVICE_NAME, "message error:%u, id:%u", ret, svc->info.service_id);

    return IMDB_ERR_SUCCESS;
}


LOCAL svcs_errcode_t ICACHE_FLASH_ATTR
svcctl_on_msg_info (dtlv_ctx_t * msg_out)
{
    // SYSTEM
    dtlv_avp_t     *list_srv;
    d_svcs_check_dtlv_error (dtlv_avp_encode_list (msg_out, 0, SVCS_AVP_SERVICE, DTLV_TYPE_OBJECT, &list_srv));

    svcs_service_info_t info_array[SVCS_INFO_ARRAY_SZIE];
    uint8           count;
    svcs_errcode_t  result = svcctl_info (&count, &info_array[0], SVCS_INFO_ARRAY_SZIE);

    if (result == SVCS_ERR_SUCCESS) {
        int             i;
        for (i = 0; i < MIN (count, SVCS_INFO_ARRAY_SZIE); i++) {
            dtlv_avp_t     *gavp_srv;
            d_svcs_check_dtlv_error (dtlv_avp_encode_grouping (msg_out, 0, SVCS_AVP_SERVICE, &gavp_srv) ||
                                     dtlv_avp_encode_uint16 (msg_out, SVCS_AVP_SERVICE_ID, info_array[i].service_id) ||
                                     dtlv_avp_encode_nchar (msg_out, COMMON_AVP_SERVICE_NAME, sizeof (service_name_t),
                                                            info_array[i].name)
                                     || dtlv_avp_encode_uint8 (msg_out, SVCS_AVP_SERVICE_ENABLED, info_array[i].enabled)
                                     || dtlv_avp_encode_uint8 (msg_out, SVCS_AVP_SERVICE_STATE, info_array[i].state)
                                     || dtlv_avp_encode_group_done (msg_out, gavp_srv));
        }
    }

    d_svcs_check_dtlv_error (dtlv_avp_encode_group_done (msg_out, list_srv));

    return SVCS_ERR_SUCCESS;
}

LOCAL svcs_errcode_t ICACHE_FLASH_ATTR
svcctl_on_msg_control (dtlv_ctx_t * msg_in, dtlv_ctx_t * msg_out)
{
    dtlv_nscode_t   path[] = { {{0, SVCS_AVP_SERVICE}
                                }
    , {{0, SVCS_AVP_SERVICE}
       }
    ,
    {{0, 0}
     }
    };
    dtlv_davp_t     avp_array[SVCS_INFO_ARRAY_SZIE];
    uint16          total_count;

    dtlv_errcode_t  dtlv_res =
        dtlv_avp_decode_bypath (msg_in, path, avp_array, SVCS_INFO_ARRAY_SZIE, true, &total_count);

    if (dtlv_res != DTLV_ERR_SUCCESS) {
        return SVCS_SERVICE_ERROR;
    }

    int             i;
    for (i = 0; i < total_count; i++) {
        dtlv_ctx_t      dtlv_ctx;
        dtlv_ctx_init_decode (&dtlv_ctx, avp_array[i].avp->data, d_avp_data_length (avp_array[i].havpd.length));

        service_ident_t service_id = 0;
        uint8           senabled = 0xFF;

        dtlv_seq_decode_begin (&dtlv_ctx, SERVICE_SERVICE_ID);
        dtlv_seq_decode_uint16 (SVCS_AVP_SERVICE_ID, &service_id);
        dtlv_seq_decode_uint8 (SVCS_AVP_SERVICE_ENABLED, &senabled);
        dtlv_seq_decode_end (&dtlv_ctx);

        if ((senabled != 0xFF) && (service_id != 0)) {
            svcs_errcode_t  ctl_res =
                (senabled) ? svcctl_service_start (service_id, NULL) : svcctl_service_stop (service_id, NULL);

            dtlv_avp_t     *gavp_srv;
            d_svcs_check_dtlv_error (dtlv_avp_encode_grouping (msg_out, 0, SVCS_AVP_SERVICE, &gavp_srv)
                                     || dtlv_avp_encode_uint16 (msg_out, SVCS_AVP_SERVICE_ID, service_id)
                                     || dtlv_avp_encode_uint8 (msg_out, COMMON_AVP_RESULT_CODE, ctl_res)
                                     || dtlv_avp_encode_group_done (msg_out, gavp_srv));
        }
    }

    return SVCS_ERR_SUCCESS;
}


LOCAL svcs_errcode_t ICACHE_FLASH_ATTR
svcctl_on_msg_config_set (dtlv_ctx_t * msg_in, dtlv_ctx_t * msg_out)
{
    dtlv_nscode_t   path[] = { {{0, SVCS_AVP_SERVICE}
                                }
    , {{0, SVCS_AVP_SERVICE}
       }
    ,
    {{0, 0}
     }
    };
    dtlv_davp_t     avp_array[SVCS_INFO_ARRAY_SZIE];
    uint16          total_count;

    dtlv_errcode_t  dtlv_res =
        dtlv_avp_decode_bypath (msg_in, path, avp_array, SVCS_INFO_ARRAY_SZIE, true, &total_count);
    if (dtlv_res != DTLV_ERR_SUCCESS) {
        return SVCS_SERVICE_ERROR;
    }

    int             i;
    for (i = 0; i < total_count; i++) {
        dtlv_ctx_t      dtlv_ctx;
        dtlv_ctx_init_decode (&dtlv_ctx, avp_array[i].avp->data, d_avp_data_length (avp_array[i].havpd.length));

        service_ident_t service_id = 0;
        char           *conf = NULL;
        dtlv_size_t     conf_len = 0;

        dtlv_seq_decode_begin (&dtlv_ctx, SERVICE_SERVICE_ID);
        dtlv_seq_decode_uint16 (SVCS_AVP_SERVICE_ID, &service_id);
        dtlv_seq_decode_ns (service_id);
        dtlv_seq_decode_group (COMMON_AVP_SVC_CONFIGURATION, conf, conf_len);
        dtlv_seq_decode_end (&dtlv_ctx);

        if (service_id != 0) {
            dtlv_ctx_t      dtlv_conf;
            svcs_errcode_t  conf_res = SVCS_INVALID_MESSAGE;

            if (conf) {
                d_svcs_check_dtlv_error (dtlv_ctx_init_decode (&dtlv_conf, conf, conf_len));
                conf_res = svcctl_service_conf_set (service_id, &dtlv_conf);
            }

            dtlv_avp_t     *gavp_srv;
            d_svcs_check_dtlv_error (dtlv_avp_encode_grouping (msg_out, 0, SVCS_AVP_SERVICE, &gavp_srv)
                                     || dtlv_avp_encode_uint16 (msg_out, SVCS_AVP_SERVICE_ID, service_id)
                                     || dtlv_avp_encode_uint8 (msg_out, COMMON_AVP_RESULT_CODE, conf_res)
                                     || dtlv_avp_encode_group_done (msg_out, gavp_srv));
        }
    }

    return SVCS_ERR_SUCCESS;
}

LOCAL svcs_errcode_t ICACHE_FLASH_ATTR
svcctl_on_msg_config_get (dtlv_ctx_t * msg_in, dtlv_ctx_t * msg_out)
{
    dtlv_nscode_t   path[] = { {{0, SVCS_AVP_SERVICE}
                                }
    , {{0, SVCS_AVP_SERVICE}
       }
    ,
    {{0, 0}
     }
    };
    dtlv_davp_t     avp_array[SVCS_INFO_ARRAY_SZIE];
    uint16          total_count;

    dtlv_errcode_t  dtlv_res =
        dtlv_avp_decode_bypath (msg_in, path, avp_array, SVCS_INFO_ARRAY_SZIE, true, &total_count);
    if (dtlv_res != DTLV_ERR_SUCCESS) {
        return SVCS_SERVICE_ERROR;
    }

    int             i;
    for (i = 0; i < total_count; i++) {
        dtlv_ctx_t      dtlv_ctx;
        dtlv_ctx_init_decode (&dtlv_ctx, avp_array[i].avp->data, d_avp_data_length (avp_array[i].havpd.length));

        service_ident_t service_id = 0;

        dtlv_seq_decode_begin (&dtlv_ctx, SERVICE_SERVICE_ID);
        dtlv_seq_decode_uint16 (SVCS_AVP_SERVICE_ID, &service_id);
        dtlv_seq_decode_end (&dtlv_ctx);

        if (service_id != 0) {
            svcs_service_conf_t *conf_data;
            svcs_errcode_t  conf_res = svcctl_find_conf (service_id, &conf_data, SVCS_CFGTYPE_CURRENT, false);

            dtlv_avp_t     *gavp_srv;
            dtlv_avp_t     *gavp_cfg;
            d_svcs_check_dtlv_error (dtlv_avp_encode_grouping (msg_out, 0, SVCS_AVP_SERVICE, &gavp_srv)
                                     || dtlv_avp_encode_uint16 (msg_out, SVCS_AVP_SERVICE_ID, service_id)
                                     || dtlv_avp_encode_uint8 (msg_out, COMMON_AVP_RESULT_CODE, conf_res)
                                     || ((conf_res) ? false
                                         : (dtlv_avp_encode_grouping
                                            (msg_out, service_id, COMMON_AVP_SVC_CONFIGURATION, &gavp_cfg)
                                            || dtlv_raw_encode (msg_out, conf_data->vardata, conf_data->varlen)
                                            || dtlv_avp_encode_uint32 (msg_out, COMMON_AVP_UPDATE_TIMESTAMP,
                                                                       conf_data->utime)
                                            || dtlv_avp_encode_group_done (msg_out, gavp_cfg))));

            conf_res = svcctl_find_conf (service_id, &conf_data, SVCS_CFGTYPE_NEW, false);
            d_svcs_check_dtlv_error (((conf_res) ? false
                                      : (dtlv_avp_encode_grouping
                                         (msg_out, service_id, COMMON_AVP_SVC_CONFIGURATION, &gavp_cfg)
                                         || dtlv_raw_encode (msg_out, conf_data->vardata, conf_data->varlen)
                                         || dtlv_avp_encode_uint32 (msg_out, COMMON_AVP_UPDATE_TIMESTAMP,
                                                                    conf_data->utime)
                                         || dtlv_avp_encode_group_done (msg_out, gavp_cfg)))
                                     || dtlv_avp_encode_group_done (msg_out, gavp_srv));
        }
    }

    return SVCS_ERR_SUCCESS;
}

LOCAL svcs_errcode_t ICACHE_FLASH_ATTR
svcctl_on_msg_config_save (dtlv_ctx_t * msg_in, dtlv_ctx_t * msg_out)
{
    dtlv_nscode_t   path[] = { {{0, SVCS_AVP_SERVICE}
                                }
    , {{0, SVCS_AVP_SERVICE}
       }
    ,
    {{0, 0}
     }
    };
    dtlv_davp_t     avp_array[SVCS_INFO_ARRAY_SZIE];
    uint16          total_count;

    dtlv_errcode_t  dtlv_res =
        dtlv_avp_decode_bypath (msg_in, path, avp_array, SVCS_INFO_ARRAY_SZIE, true, &total_count);
    if (dtlv_res != DTLV_ERR_SUCCESS) {
        return SVCS_SERVICE_ERROR;
    }

    int             i;
    for (i = 0; i < total_count; i++) {
        dtlv_ctx_t      dtlv_ctx;
        dtlv_ctx_init_decode (&dtlv_ctx, avp_array[i].avp->data, d_avp_data_length (avp_array[i].havpd.length));

        service_ident_t service_id = 0;

        dtlv_seq_decode_begin (&dtlv_ctx, SERVICE_SERVICE_ID);
        dtlv_seq_decode_uint16 (SVCS_AVP_SERVICE_ID, &service_id);
        dtlv_seq_decode_end (&dtlv_ctx);

        if (service_id != 0) {
            svcs_errcode_t  conf_res = svcctl_service_conf_save (service_id);

            dtlv_avp_t     *gavp_srv;
            d_svcs_check_dtlv_error (dtlv_avp_encode_grouping (msg_out, 0, SVCS_AVP_SERVICE, &gavp_srv)
                                     || dtlv_avp_encode_uint16 (msg_out, SVCS_AVP_SERVICE_ID, service_id)
                                     || dtlv_avp_encode_uint8 (msg_out, COMMON_AVP_RESULT_CODE, conf_res)
                                     || dtlv_avp_encode_group_done (msg_out, gavp_srv));
        }
    }

    return SVCS_ERR_SUCCESS;
}

LOCAL svcs_errcode_t ICACHE_FLASH_ATTR
svcctl_on_message (service_ident_t orig_id, service_msgtype_t msgtype, void *ctxdata, dtlv_ctx_t * msg_in,
                   dtlv_ctx_t * msg_out)
{
    dtlv_ctx_reset_decode (msg_in);
    svcs_errcode_t  res = SVCS_ERR_SUCCESS;
    switch (msgtype) {
    case SVCS_MSGTYPE_INFO:
        res = svcctl_on_msg_info (msg_out);
        break;
    case SVCS_MSGTYPE_CONTROL:
        res = svcctl_on_msg_control (msg_in, msg_out);
        break;
    case SVCS_MSGTYPE_CONFIG_SAVE:
        res = svcctl_on_msg_config_save (msg_in, msg_out);
        break;
    case SVCS_MSGTYPE_CONFIG_GET:
        res = svcctl_on_msg_config_get (msg_in, msg_out);
        break;
    case SVCS_MSGTYPE_CONFIG_SET:
        res = svcctl_on_msg_config_set (msg_in, msg_out);
        break;
    default:
        res = SVCS_MSGTYPE_INVALID;
    }

    return res;
}


/*
 *[public] Start Service Controller Service
 *  - result: svcs_errcode_t
*/
svcs_errcode_t  ICACHE_FLASH_ATTR
svcctl_start (imdb_hndlr_t hmdb, imdb_hndlr_t hfdb)
{
    d_log_iprintf (SERVICES_SERVICE_NAME, "starting...");
    if (sdata) {
        d_log_wprintf (SERVICES_SERVICE_NAME, "already run");
        return SVCS_ALREADY_RUN;
    }

    imdb_class_def_t cdef =
        { SERVICES_IMDB_CLS_DATA, false, true, false, 0, SERVICES_DATA_STORAGE_PAGES, SERVICES_DATA_STORAGE_PAGE_BLOCKS,
0 };

    imdb_hndlr_t    hdata;
    d_svcs_check_svcs_error (imdb_class_create (hmdb, &cdef, &hdata)
        );

    d_svcs_check_svcs_error (imdb_clsobj_insert (hmdb, hdata, (void **) &sdata, sizeof (services_data_t))
        );
    os_memset (sdata, 0, sizeof (services_data_t));

    sdata->svcres.hmdb = hmdb;
    sdata->svcres.hfdb = hfdb;
    sdata->svcres.hdata = hdata;

    imdb_class_def_t cdef2 =
        { SERVICES_IMDB_CLS_SERVICE, false, true, false, 0, SERVICES_STORAGE_PAGES, SERVICES_STORAGE_PAGE_BLOCKS,
sizeof (svcs_service_t) };
    d_svcs_check_svcs_error (imdb_class_create (hmdb, &cdef2, &sdata->hsvcs)
        );

    if (hfdb) {
        imdb_class_find (hfdb, SERVICES_IMDB_CLS_CONFIG, &sdata->hconf);
        if (!sdata->hconf) {
            imdb_class_def_t cdef3 = { SERVICES_IMDB_CLS_CONFIG, false, true, false, 0, SERVICES_CONFIG_STORAGE_PAGES,
                SERVICES_CONFIG_STORAGE_PAGE_BLOCKS, sizeof (svcs_service_conf_t)
            };
            d_svcs_check_svcs_error (imdb_class_create (hfdb, &cdef3, &sdata->hconf));
        }
        if (!system_get_safe_mode ())
            imdb_class_forall (sdata->svcres.hfdb, sdata->hconf, NULL, svcctl_forall_conf_clean);
    }

    d_log_wprintf (SERVICES_SERVICE_NAME, "started");
    return SVCS_ERR_SUCCESS;
}

/*
 * [public] Stop Service Controller Service
 *   - result: svcs_errcode_t
*/
svcs_errcode_t  ICACHE_FLASH_ATTR
svcctl_stop ()
{
    d_log_iprintf (SERVICES_SERVICE_NAME, "stoping...");
    if (!sdata) {
        d_log_wprintf (SERVICES_SERVICE_NAME, "not run");
        return SVCS_NOT_RUN;
    }

    // stop all services in dependency order
    d_svcs_check_imdb_error (imdb_class_forall (sdata->svcres.hmdb, sdata->hsvcs, NULL, svcctl_forall_stop)
        );

    d_svcs_check_svcs_error (imdb_class_destroy (sdata->svcres.hmdb, sdata->hsvcs)
        );
    d_svcs_check_svcs_error (imdb_class_destroy (sdata->svcres.hmdb, sdata->svcres.hdata)
        );
    sdata = NULL;

    d_log_wprintf (SERVICES_SERVICE_NAME, "stoped");
    return SVCS_ERR_SUCCESS;
}

typedef struct svcs_info_ctx_s {
    uint8           count;
    uint8           array_len;
    svcs_service_info_t *info_array;
} svcs_info_ctx_t;

LOCAL imdb_errcode_t ICACHE_FLASH_ATTR
svcctl_forall_get_info (imdb_fetch_obj_t * fobj, void *data)
{
    svcs_service_t *svc = d_pointer_as (svcs_service_t, fobj->dataptr);
    svcs_info_ctx_t *info_ctx = d_pointer_as (svcs_info_ctx_t, data);
    info_ctx->count++;
    if (info_ctx->count <= info_ctx->array_len) {
        os_memcpy (d_pointer_add (uint8, info_ctx->info_array, sizeof (svcs_service_info_t) * (info_ctx->count - 1)),
                   &svc->info, sizeof (svcs_service_info_t));
    }
    return IMDB_ERR_SUCCESS;
}


svcs_errcode_t  ICACHE_FLASH_ATTR
svcctl_info (uint8 * count, svcs_service_info_t * info_array, uint8 array_len)
{
    d_check_is_run ();

    svcs_info_ctx_t info_ctx;
    info_ctx.array_len = array_len;
    info_ctx.info_array = info_array;
    info_ctx.count = 0;

    d_svcs_check_imdb_error (imdb_class_forall
                             (sdata->svcres.hmdb, sdata->hsvcs, (void *) &info_ctx, svcctl_forall_get_info)
        );
    *(count) = info_ctx.count;

    return IMDB_ERR_SUCCESS;
}

/*
 * [public] Install Service
 *  - service_id: Service Identity
 *  - name: Service Name
 *  - result: svcs_errcode_t
*/
svcs_errcode_t  ICACHE_FLASH_ATTR
svcctl_service_install (service_ident_t service_id, const char *name, svcs_service_def_t * sdef)
{
    d_check_is_run ();

    d_log_iprintf (SERVICES_SERVICE_NAME, "\"%s\" [id:%u] installing", name, service_id);
    svcs_service_t *svc = NULL;
    svcs_errcode_t  ret = svcctl_find (0, name, &svc);
    if (ret != SVCS_NOT_EXISTS) {
        d_svcs_check_svcs_error (ret);
    }
    if (svc) {
        d_log_wprintf (SERVICES_SERVICE_NAME, "\"%s\" [id:%u] name already installed, id:%u ", name, service_id,
                       svc->info.service_id);
        return SVCS_ALREADY_EXISTS;
    }
    ret = svcctl_find (service_id, NULL, &svc);
    if (ret != SVCS_NOT_EXISTS) {
        d_svcs_check_svcs_error (ret);
    }
    if (svc) {
        d_log_wprintf (SERVICES_SERVICE_NAME, "\"%s\" [id:%u] id already installed, name:%s", name, service_id,
                       svc->info.name);
        return SVCS_ALREADY_EXISTS;
    }

    d_svcs_check_imdb_error (imdb_clsobj_insert
                             (sdata->svcres.hmdb, sdata->hsvcs, (void **) &svc, sizeof (svcs_service_t) + sdef->varsize)
        );

    os_memset (svc, 0, sizeof (svcs_service_t) + sdef->varsize);
    svc->on_message = sdef->on_message;
    svc->on_start = sdef->on_start;
    svc->on_stop = sdef->on_stop;
    svc->on_cfgupd = sdef->on_cfgupd;
    svc->info.service_id = service_id;
    svc->info.state = SVCS_STATE_STOPPED;
    svc->info.enabled = sdef->enabled;
    svc->multicast = sdef->multicast;
    os_memcpy (svc->info.name, name, MIN (os_strlen (name), sizeof (service_name_t)));

    ret = SVCS_ERR_SUCCESS;
    if (svc->info.enabled) {
        ret = svcctl_svc_start (svc, false);
    }

    return ret;
}

/*
 * [public] Uninstall Service
 *  - name: Service Name
 *  - result: svcs_errcode_t
 */
svcs_errcode_t  ICACHE_FLASH_ATTR
svcctl_service_uninstall (const char *name)
{
    d_check_is_run ();

    svcs_service_t *svc = NULL;
    svcs_errcode_t  ret = svcctl_find (0, name, &svc);
    if (ret == SVCS_NOT_EXISTS) {
        d_log_wprintf (SERVICES_SERVICE_NAME, "\"%s\" not installed", name);
        return SVCS_NOT_EXISTS;
    }
    d_svcs_check_svcs_error (ret);
    d_assert (svc, "svc=%p", svc);

    if (svc->info.state == SVCS_STATE_RUNNING) {
        ret = svcctl_svc_stop (svc);
        d_svcs_check_svcs_error (ret);
    }

    d_svcs_check_imdb_error (imdb_clsobj_delete (sdata->svcres.hmdb, sdata->hsvcs, svc)
        );

    d_log_iprintf (SERVICES_SERVICE_NAME, "\"%s\" has been uninstalled", name);

    return SVCS_ERR_SUCCESS;
}

/*
 * [public] Send signal to Start Service
 *  - name: Service Name
 *  - result: svcs_errcode_t
 */
svcs_errcode_t  ICACHE_FLASH_ATTR
svcctl_service_start (service_ident_t service_id, const char *name)
{
    d_check_is_run ();

    svcs_service_t *svc = NULL;
    svcs_errcode_t  ret = svcctl_find (service_id, name, &svc);
    d_svcs_check_svcs_error (ret);

    ret = svcctl_svc_start (svc, true);
    return ret;
}

/*
 * [public] Send signal to Stop Service
 *  - name: Service Name
 *  - result: svcs_errcode_t
 */
svcs_errcode_t  ICACHE_FLASH_ATTR
svcctl_service_stop (service_ident_t service_id, const char *name)
{
    d_check_is_run ();

    svcs_service_t *svc = NULL;
    svcs_errcode_t  ret = svcctl_find (service_id, name, &svc);
    d_svcs_check_svcs_error (ret);

    ret = svcctl_svc_stop (svc);
    return ret;
}

svcs_errcode_t  ICACHE_FLASH_ATTR
svcctl_service_conf_get (service_ident_t service_id, dtlv_ctx_t * conf, svcs_cfgtype_t cfgtype)
{
    d_check_is_run ();

    svcs_service_conf_t *conf_data;
    d_svcs_check_svcs_error (svcctl_find_conf (service_id, &conf_data, cfgtype, false));

    d_svcs_check_dtlv_error (dtlv_ctx_init_decode (conf, conf_data->vardata, conf_data->varlen));

    return SVCS_ERR_SUCCESS;
}

svcs_errcode_t  ICACHE_FLASH_ATTR
svcctl_service_conf_set (service_ident_t service_id, dtlv_ctx_t * conf)
{
    d_check_is_run ();

    svcs_service_t *svc = NULL;
    svcs_errcode_t  ret = svcctl_find (service_id, NULL, &svc);
    d_svcs_check_svcs_error (ret);

    svcs_service_conf_t *conf_data = NULL;
    svcctl_find_conf (service_id, &conf_data, SVCS_CFGTYPE_NEW, false);

    if (conf_data)
        d_svcs_check_imdb_error (imdb_clsobj_delete (sdata->svcres.hfdb, sdata->hconf, conf_data));

    if (conf && conf->datalen) {
        d_svcs_check_imdb_error (imdb_clsobj_insert
                                 (sdata->svcres.hfdb, sdata->hconf, (void **) &conf_data,
                                  sizeof (svcs_service_conf_t) + conf->datalen));

        conf_data->service_id = service_id;
        conf_data->cfgtype = SVCS_CFGTYPE_NEW;
        conf_data->varlen = conf->datalen;
        conf_data->utime = lt_time (NULL);
        os_memcpy (conf_data->vardata, conf->buf, conf->datalen);
    }

    imdb_flush (sdata->svcres.hfdb);

    ret = SVCS_ERR_SUCCESS;
    if (svc->on_cfgupd)
        ret = svc->on_cfgupd (conf);
    else
        d_log_wprintf (SERVICES_SERVICE_NAME, "\"%s\" no cfgupd handler", svc->info.name);

    return ret;
}

svcs_errcode_t  ICACHE_FLASH_ATTR
svcctl_service_conf_save (service_ident_t service_id)
{
    d_check_is_run ();

    svcs_service_t *svc = NULL;
    d_svcs_check_svcs_error (svcctl_find (service_id, NULL, &svc));

    svcs_service_conf_t *conf_data_new;
    svcctl_find_conf (service_id, &conf_data_new, SVCS_CFGTYPE_NEW, true);
    if (!conf_data_new)
        return SVCS_NOT_EXISTS;

    // delete prev current
    svcs_service_conf_t *conf_data;
    svcctl_find_conf (service_id, &conf_data, SVCS_CFGTYPE_CURRENT, false);
    if (conf_data)
        d_svcs_check_imdb_error (imdb_clsobj_delete (sdata->svcres.hfdb, sdata->hconf, conf_data));

    conf_data_new->cfgtype = SVCS_CFGTYPE_CURRENT;

    d_log_wprintf (SERVICES_SERVICE_NAME, "\"%s\" config save", svc->info.name);
    imdb_flush (sdata->svcres.hfdb);

    return SVCS_ERR_SUCCESS;
}

svcs_errcode_t  svcctl_service_set_enabled (service_ident_t service_id, bool enabled)
{
    svcs_service_conf_t *conf_data;
    svcctl_find_conf (service_id, &conf_data, SVCS_CFGTYPE_NEW, true);
    if (!conf_data)
        svcctl_find_conf (service_id, &conf_data, SVCS_CFGTYPE_CURRENT, true);

    if (!conf_data) {

    }
    else {
        conf_data->disabled = ! enabled;
    }

    imdb_flush (sdata->svcres.hfdb);

    return SVCS_ERR_SUCCESS;
}

svcs_errcode_t  ICACHE_FLASH_ATTR
encode_service_result_ext (dtlv_ctx_t * msg_out, uint8 ext_code, const char *errmsg)
{
    const char     *errmsg2 = errmsg;
    if (!errmsg2)
        errmsg2 = get_last_error ();
    d_svcs_check_dtlv_error (dtlv_avp_encode_uint8 (msg_out, COMMON_AVP_RESULT_EXT_CODE, ext_code)
                             || ((*errmsg2) ? dtlv_avp_encode_char (msg_out, COMMON_AVP_RESULT_MESSAGE, errmsg2) : 0));

    return SVCS_ERR_SUCCESS;
}

/*
[public] Send Synchronous Message to Service
  - orig_id: Message Originator Service Identifier
  - dest_id: Message Destination Service Identifier
  - ctxdata: Context Variable Data
  - result: svcs_errcode_t
*/
svcs_errcode_t  ICACHE_FLASH_ATTR
svcctl_service_message (service_ident_t orig_id,
                        service_ident_t dest_id,
                        void *ctxdata, service_msgtype_t msgtype, dtlv_ctx_t * msg_in, dtlv_ctx_t * msg_out)
{
    d_check_is_run ();

    // message to itself
    if (dest_id == SERVICE_SERVICE_ID) {
        return svcctl_on_message (orig_id, msgtype, ctxdata, msg_in, msg_out);
    }

    svcs_errcode_t  ret = SVCS_ERR_SUCCESS;
    if ((dest_id == 0) && (msgtype >= SVCS_MSGTYPE_MULTICAST_MIN) && (msgtype < SVCS_MSGTYPE_MULTICAST_MAX)) {
        // multicast
        d_log_iprintf (SERVICES_SERVICE_NAME, "broadcast message:%u", msgtype);
        svcs_message_ctx_t ctx;

        ctx.orig_id = orig_id;
        ctx.msgtype = msgtype;
        ctx.ctxdata = ctxdata;
        ctx.msg_in = msg_in;
        ctx.msg_out = msg_out;

        ret = imdb_class_forall (sdata->svcres.hmdb, sdata->hsvcs, &ctx, svcctl_forall_message);
    }
    else if ((dest_id != 0) && (msgtype < SVCS_MSGTYPE_MULTICAST_MIN)) {
        svcs_service_t *svc = NULL;
        ret = svcctl_find (dest_id, NULL, &svc);
        d_svcs_check_svcs_error (ret);
        if (svc->info.state != SVCS_STATE_RUNNING) {
            return SVCS_NOT_RUN;
        }
        ret = svc->on_message (orig_id, msgtype, ctxdata, msg_in, msg_out);
        if ((ret != SVCS_ERR_SUCCESS) && (ret != SVCS_MSGTYPE_INVALID))
            d_log_wprintf (SERVICES_SERVICE_NAME, "message error:%u, id:%u", ret, dest_id);
    }
    else {
        d_log_wprintf (SERVICES_SERVICE_NAME, "message type error:%u, id:%u", msgtype, dest_id);
        ret = SVCS_MSGTYPE_INVALID;
    }

    return ret;
}
