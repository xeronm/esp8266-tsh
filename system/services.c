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

/* 
 *
 *  Description: Services catalog and control
 *    Provides: 
 *      - Common API for services
 *      - Storage for Configuration Data
 *      - Handling messagings between services
 *
 */

#include "sysinit.h"
#include "core/utils.h"
#include "core/logging.h"
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
    char            vardata[];
} svcs_service_conf_t;

typedef struct svcs_service_s {
    svcs_service_info_t info;
    // handlers
    svcs_on_start_t on_start;
    svcs_on_stop_t  on_stop;
    svcs_on_message_t on_message;
    svcs_on_cfgupd_t on_cfgupd;
    // configuration
    svcs_service_conf_t *conf;
    // unperisistent data
    char            vardata[];
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
    service_ident_t service_id;
    svcs_service_conf_t *conf;
} svcs_find_conf_ctx_t;


LOCAL imdb_errcode_t ICACHE_FLASH_ATTR
svcctl_forall_find_conf (void *ptr, void *data)
{
    svcs_service_conf_t *conf = d_pointer_as (svcs_service_conf_t, ptr);
    svcs_find_conf_ctx_t *find_conf_ctx = d_pointer_as (svcs_find_conf_ctx_t, data);
    if (find_conf_ctx->service_id == conf->service_id) {
	find_conf_ctx->conf = conf;
	return IMDB_CURSOR_BREAK;
    }
    return IMDB_ERR_SUCCESS;
}

LOCAL svcs_errcode_t ICACHE_FLASH_ATTR
svcctl_find_conf (service_ident_t service_id, svcs_service_conf_t ** conf)
{
    d_check_is_run ();

    svcs_find_conf_ctx_t find_conf_ctx;
    find_conf_ctx.service_id = service_id;
    find_conf_ctx.conf = NULL;

    d_svcs_check_imdb_error (imdb_class_forall (sdata->hconf, &find_conf_ctx, svcctl_forall_find_conf)
	);

    *conf = find_conf_ctx.conf;
    return (*conf) ? SVCS_ERR_SUCCESS : SVCS_NOT_EXISTS;
}

LOCAL svcs_errcode_t ICACHE_FLASH_ATTR
svcctl_svc_conf_get (svcs_service_t * svc, dtlv_ctx_t * conf)
{
    svcs_service_conf_t *conf_data;
    d_svcs_check_svcs_error (svcctl_find_conf (svc->info.service_id, &conf_data)
	);

    size_t          length;
    d_svcs_check_imdb_error (imdb_clsobj_length (sdata->hconf, conf_data, &length)
	);

    length -= sizeof (svcs_service_conf_t);
    d_svcs_check_dtlv_error (dtlv_ctx_init_decode (conf, (char *) conf_data->vardata, length)
	);

    return SVCS_ERR_SUCCESS;
}

LOCAL svcs_errcode_t ICACHE_FLASH_ATTR
svcctl_svc_conf_set (svcs_service_t * svc, dtlv_ctx_t * conf)
{
    svcs_service_conf_t *conf_data;
    d_svcs_check_imdb_error (svcctl_find_conf (svc->info.service_id, &conf_data)
	);
    // TODO: Not Finished
    //imdb_errcode_t res = imdb_clsobj_length(sdata->hconf, conf_data, &length);
    return SVCS_ERR_SUCCESS;
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
svcctl_svc_start (svcs_service_t * svc)
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

    dtlv_ctx_t      conf;
    dtlv_ctx_t     *conf_ptr = NULL;
    {
	svcs_errcode_t  res = svcctl_svc_conf_get (svc, &conf);
	if (res == SVCS_ERR_SUCCESS) {
	    conf_ptr = &conf;
	}
	else if (res != SVCS_NOT_EXISTS) {
	    d_log_wprintf (SERVICES_SERVICE_NAME, "\"%s\" config res:%u", svc->info.name, res);
	}
    }

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
svcctl_forall_stop (void *ptr, void *data)
{
    svcs_service_t *svc = d_pointer_as (svcs_service_t, ptr);
    if (svc->info.state != SVCS_STATE_RUNNING) {
	return IMDB_ERR_SUCCESS;
    }

    svcctl_svc_stop (svc);
    return IMDB_ERR_SUCCESS;
}

LOCAL imdb_errcode_t ICACHE_FLASH_ATTR
svcctl_forall_find (void *ptr, void *data)
{
    svcs_service_t *svc = d_pointer_as (svcs_service_t, ptr);
    svcs_find_ctx_t *find_ctx = d_pointer_as (svcs_find_ctx_t, data);
    if ((!find_ctx->service_id || svc->info.service_id == find_ctx->service_id) &&
	(!find_ctx->name || os_strncmp (svc->info.name, find_ctx->name, sizeof (service_name_t)) == 0)
	) {
	find_ctx->svc = svc;
	return IMDB_CURSOR_BREAK;
    }
    return IMDB_ERR_SUCCESS;
}

LOCAL svcs_errcode_t  ICACHE_FLASH_ATTR
svcctl_find (service_ident_t service_id, const char *name, svcs_service_t ** svc)
{
    if (!sdata)
	return SVCS_NOT_RUN;

    svcs_find_ctx_t find_ctx;
    find_ctx.service_id = service_id;
    find_ctx.name = name;
    find_ctx.svc = NULL;

    d_svcs_check_imdb_error (imdb_class_forall (sdata->hsvcs, &find_ctx, svcctl_forall_find)
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
svcctl_forall_message (void *ptr, void *data)
{
    svcs_service_t *svc = d_pointer_as (svcs_service_t, ptr);
    svcs_message_ctx_t *ctx = d_pointer_as (svcs_message_ctx_t, data);

    if ((svc->info.state != SVCS_STATE_RUNNING) || (!svc->on_message)) {
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
    , {{0, 0}
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

	// response & action
	dtlv_avp_t     *gavp_srv;
	dtlv_avp_encode_grouping (msg_out, 0, SVCS_AVP_SERVICE, &gavp_srv);

	service_ident_t service_id = 0;
	service_name_t  sname = "";
	uint8           senabled = -1;
	while (dtlv_res == DTLV_ERR_SUCCESS) {
	    dtlv_davp_t     avp;
	    dtlv_res = dtlv_avp_decode (&dtlv_ctx, &avp);
	    if (dtlv_res != DTLV_ERR_SUCCESS) {
		break;
	    }
	    switch (avp.havpd.nscode.comp.code) {
	    case SVCS_AVP_SERVICE_ID:
		dtlv_avp_get_uint16 (&avp, &service_id);
		dtlv_avp_encode_uint16 (msg_out, SVCS_AVP_SERVICE_ID, service_id);
		break;
	    case COMMON_AVP_SERVICE_NAME:
		dtlv_avp_get_char (&avp, sname);
		dtlv_avp_encode_char (msg_out, COMMON_AVP_SERVICE_NAME, sname);
		break;
	    case SVCS_AVP_SERVICE_ENABLED:
		dtlv_avp_get_uint8 (&avp, &senabled);
		break;
	    default:
		break;
	    }
	}

	if ((senabled != -1) && (service_id != 0)) {
	    svcs_errcode_t  ctl_res;
	    if (senabled) {
		ctl_res = svcctl_service_start (service_id, NULL);
	    }
	    else {
		ctl_res = svcctl_service_stop (service_id, NULL);
	    }
	    dtlv_avp_encode_uint8 (msg_out, COMMON_AVP_RESULT_CODE, ctl_res);
	}

	dtlv_avp_encode_group_done (msg_out, gavp_srv);
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
    default:
	res = SVCS_MSGTYPE_INVALID;
    }

    return res;
}

/*
[public] Start Service Controller Service
  - result: svcs_errcode_t
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
	{ SERVICES_IMDB_CLS_DATA, false, true, false, 0, SERVICES_DATA_STORAGE_PAGES, SERVICES_DATA_STORAGE_PAGE_BLOCKS, 0 };

    imdb_hndlr_t    hdata;
    d_svcs_check_svcs_error (imdb_class_create (hmdb, &cdef, &hdata)
	);

    d_svcs_check_svcs_error (imdb_clsobj_insert (hdata, (void **) &sdata, sizeof (services_data_t))
	);
    os_memset (sdata, 0, sizeof (services_data_t));

    sdata->svcres.hmdb = hmdb;
    sdata->svcres.hfdb = hfdb;
    sdata->svcres.hdata = hdata;

    imdb_class_def_t cdef2 =
	{ SERVICES_IMDB_CLS_SERVICE, false, true, false, 0, SERVICES_STORAGE_PAGES, SERVICES_STORAGE_PAGE_BLOCKS, sizeof (svcs_service_t) };
    d_svcs_check_svcs_error (imdb_class_create (hmdb, &cdef2, &sdata->hsvcs)
	);

//    imdb_class_def_t cdef3 =
//	{ SERVICES_IMDB_CLS_CONFIG, false, true, false, 0, SERVICES_CONFIG_STORAGE_PAGES,
//SERVICES_CONFIG_STORAGE_PAGE_BLOCKS, SERVICES_CONFIG_STORAGE_PAGE_BLOCKS, sizeof (svcs_service_conf_t) };
//    d_svcs_check_svcs_error (imdb_class_create (hmdb, &cdef3, &sdata->hconf)
//	);

    d_log_wprintf (SERVICES_SERVICE_NAME, "started");
    return SVCS_ERR_SUCCESS;
}

/*
[public] Stop Service Controller Service
  - result: svcs_errcode_t
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
    d_svcs_check_imdb_error (imdb_class_forall (sdata->hsvcs, NULL, svcctl_forall_stop)
	);

    d_svcs_check_svcs_error (imdb_class_destroy (sdata->hsvcs)
	);
    d_svcs_check_svcs_error (imdb_class_destroy (sdata->svcres.hdata)
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
svcctl_forall_get_info (void *ptr, void *data)
{
    svcs_service_t *svc = d_pointer_as (svcs_service_t, ptr);
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

    d_svcs_check_imdb_error (imdb_class_forall (sdata->hsvcs, (void *) &info_ctx, svcctl_forall_get_info)
	);
    *(count) = info_ctx.count;

    return IMDB_ERR_SUCCESS;
}

/*
[public] Install Service
  - service_id: Service Identity
  - name: Service Name
  - result: svcs_errcode_t
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

    d_svcs_check_imdb_error (imdb_clsobj_insert (sdata->hsvcs, (void **) &svc, sizeof (svcs_service_t) + sdef->varsize)
	);

    os_memset (svc, 0, sizeof (svcs_service_t) + sdef->varsize);
    svc->on_message = sdef->on_message;
    svc->on_start = sdef->on_start;
    svc->on_stop = sdef->on_stop;
    svc->on_cfgupd = sdef->on_cfgupd;
    svc->info.service_id = service_id;
    svc->info.state = SVCS_STATE_STOPPED;
    svc->info.enabled = sdef->enabled;
    os_memcpy (svc->info.name, name, MIN (os_strlen (name), sizeof (service_name_t)));

    ret = SVCS_ERR_SUCCESS;
    if (svc->info.enabled) {
	ret = svcctl_svc_start (svc);
    }

    return ret;
}

/*
[public] Uninstall Service
  - name: Service Name
  - result: svcs_errcode_t
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

    d_svcs_check_imdb_error (imdb_clsobj_delete (sdata->hsvcs, svc)
	);

    d_log_iprintf (SERVICES_SERVICE_NAME, "\"%s\" has been uninstalled", name);

    return SVCS_ERR_SUCCESS;
}

/*
[public] Send signal to Start Service
  - name: Service Name
  - result: svcs_errcode_t
*/
svcs_errcode_t  ICACHE_FLASH_ATTR
svcctl_service_start (service_ident_t service_id, const char *name)
{
    svcs_service_t *svc = NULL;
    svcs_errcode_t  ret = svcctl_find (service_id, name, &svc);
    d_svcs_check_svcs_error (ret);

    ret = svcctl_svc_start (svc);
    return ret;
}

/*
[public] Send signal to Stop Service
  - name: Service Name
  - result: svcs_errcode_t
*/
svcs_errcode_t  ICACHE_FLASH_ATTR
svcctl_service_stop (service_ident_t service_id, const char *name)
{
    svcs_service_t *svc = NULL;
    svcs_errcode_t  ret = svcctl_find (service_id, name, &svc);
    d_svcs_check_svcs_error (ret);

    ret = svcctl_svc_stop (svc);
    return ret;
}

svcs_errcode_t   ICACHE_FLASH_ATTR
encode_service_result_ext (dtlv_ctx_t * msg_out, uint8 ext_code) 
{
    char       *errmsg = get_last_error ();
    d_svcs_check_dtlv_error (dtlv_avp_encode_uint8 (msg_out, COMMON_AVP_RESULT_EXT_CODE, ext_code)
                             || ((*errmsg) ? dtlv_avp_encode_char (msg_out, COMMON_AVP_RESULT_MESSAGE, errmsg) : 0) );

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

    // broadcast
    if (dest_id == 0) {
	d_log_iprintf (SERVICES_SERVICE_NAME, "broadcast message:%u", msgtype);
	svcs_message_ctx_t ctx;

	ctx.orig_id = orig_id;
	ctx.msgtype = msgtype;
	ctx.ctxdata = ctxdata;
	ctx.msg_in = msg_in;
	ctx.msg_out = msg_out;

	return imdb_class_forall (sdata->hsvcs, &ctx, svcctl_forall_message);
    }

    svcs_service_t *svc = NULL;
    svcs_errcode_t  ret = svcctl_find (dest_id, NULL, &svc);
    d_svcs_check_svcs_error (ret);
    if (svc->info.state != SVCS_STATE_RUNNING) {
	return SVCS_NOT_RUN;
    }
    ret = svc->on_message (orig_id, msgtype, ctxdata, msg_in, msg_out);
    if ((ret != SVCS_ERR_SUCCESS) && (ret != SVCS_MSGTYPE_INVALID))
	d_log_wprintf (SERVICES_SERVICE_NAME, "message error:%u, id:%u", ret, dest_id);

    return ret;
}

svcs_errcode_t  ICACHE_FLASH_ATTR
svcctl_service_conf_get (service_ident_t service_id, dtlv_ctx_t * conf)
{
    return SVCS_ERR_SUCCESS;
}

svcs_errcode_t  ICACHE_FLASH_ATTR
svcctl_service_conf_set (service_ident_t service_id, dtlv_ctx_t * conf)
{
    return SVCS_ERR_SUCCESS;
}
