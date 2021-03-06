/* 
 * ESP8266 Administation service
 * Copyright (c) 2016-2018 Denis Muratov <xeronm@gmail.com>.
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
#ifdef ARCH_XTENSA
#include "fwupgrade.h"
#include "flashmap.h"
#endif
#include "core/logging.h"
#include "core/utils.h"
#include "core/config.h"
#include "core/system.h"
#include "system/services.h"
#include "system/comavp.h"
#include "service/espadmin.h"

#define IMDB_INFO_ARRAY_SIZE	10

#ifdef ARCH_XTENSA
firmware_info_t fw_info RODATA = {
    APP_PRODUCT,
    {{APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_VERSION_PATCH}
     }
    ,
    APP_VERSION_SUFFIX,
    BUILD_NUMBER,
    0,
    APP_VERSION_RELEASE_DATE,
    0xFFFFFFFF,
    0xFFFFFFFF,
    0xFFFFFFFF,
    APP_INIT_DIGEST
};
#endif

svcs_errcode_t  ICACHE_FLASH_ATTR
espadmin_on_start (const svcs_resource_t * svcres, dtlv_ctx_t * conf)
{
    espadmin_on_cfgupd (conf);

    return SVCS_ERR_SUCCESS;
}

svcs_errcode_t  ICACHE_FLASH_ATTR
espadmin_on_stop ()
{
    return SVCS_ERR_SUCCESS;
}

LOCAL svcs_errcode_t ICACHE_FLASH_ATTR
espadmin_on_msg_product (dtlv_ctx_t * msg_out)
{
#ifdef ARCH_XTENSA
    char            version[VERSION_BUFFER_SIZE];
    os_snprintf (version, VERSION_BUFFER_SIZE, FW_VERSTR, FW_VER2STR (&fw_info));
    char           *hostname = wifi_station_get_hostname ();

    d_svcs_check_dtlv_error (dtlv_avp_encode_nchar
                             (msg_out, COMMON_AVP_APP_PRODUCT, sizeof (fw_info.product), fw_info.product)
                             || dtlv_avp_encode_nchar (msg_out, COMMON_AVP_APP_VERSION, VERSION_BUFFER_SIZE, version)
                             || ((hostname) ? dtlv_avp_encode_char (msg_out, COMMON_AVP_HOST_NAME, hostname) : false)
                             || dtlv_avp_encode_char (msg_out, COMMON_AVP_SYSTEM_DESCRIPTION, system_get_description ())
                             || dtlv_avp_encode_uint32 (msg_out, COMMON_AVP_SYS_UPTIME, lt_ctime ())
        );
#endif
    return SVCS_ERR_SUCCESS;
}

LOCAL svcs_errcode_t ICACHE_FLASH_ATTR
espadmin_on_msg_firmware (dtlv_ctx_t * msg_out)
{
#ifdef ARCH_XTENSA
    dtlv_avp_t     *gavp;
    // FIRMWARE
    flash_ota_map_t *fwmap = get_flash_ota_map ();
    d_svcs_check_dtlv_error (dtlv_avp_encode_grouping (msg_out, 0, ESPADMIN_AVP_FIRMWARE, &gavp) ||
                             dtlv_avp_encode_uint32 (msg_out, ESPADMIN_AVP_FW_ADDR, system_get_userbin_addr ()) ||
                             dtlv_avp_encode_uint8 (msg_out, ESPADMIN_AVP_FW_SIZE_MAP, system_get_flash_size_map ()) ||
                             dtlv_avp_encode_uint32 (msg_out, ESPADMIN_AVP_FW_BIN_SIZE, fw_info.binsize) ||
                             dtlv_avp_encode_uint32 (msg_out, ESPADMIN_AVP_FW_BIN_DATE, fw_info.bindate) ||
                             dtlv_avp_encode_uint32 (msg_out, ESPADMIN_AVP_FW_USER_DATA_ADDR,
                                                     d_flash_user2_data_addr (fwmap))
                             || dtlv_avp_encode_uint32 (msg_out, ESPADMIN_AVP_FW_USER_DATA_SIZE,
                                                        d_flash_user2_data_addr_end (fwmap) -
                                                        d_flash_user2_data_addr (fwmap))
                             || dtlv_avp_encode_uint32 (msg_out, ESPADMIN_AVP_FW_RELEASE_DATE, fw_info.release_date)
                             || dtlv_avp_encode_octets (msg_out, ESPADMIN_AVP_FW_DIGEST, sizeof (fw_info.digest),
                                                        (char *) fw_info.digest)
                             || dtlv_avp_encode_octets (msg_out, ESPADMIN_AVP_FW_INIT_DIGEST, sizeof (fw_info.digest),
                                                        APP_INIT_DIGEST) || dtlv_avp_encode_group_done (msg_out, gavp));
#endif
    return SVCS_ERR_SUCCESS;
}

LOCAL svcs_errcode_t ICACHE_FLASH_ATTR
espadmin_on_msg_system (dtlv_ctx_t * msg_out)
{
#ifdef ARCH_XTENSA
    dtlv_avp_t     *gavp;
    // SYSTEM
    struct rst_info *rsti = system_get_rst_info ();

    d_svcs_check_dtlv_error (dtlv_avp_encode_grouping (msg_out, 0, ESPADMIN_AVP_SYSTEM, &gavp) ||
                             dtlv_avp_encode_char (msg_out, ESPADMIN_AVP_SYS_SDKVERSION, system_get_sdk_version ()) ||
                             dtlv_avp_encode_uint32 (msg_out, ESPADMIN_AVP_SYS_CHIP_ID, system_get_chip_id ()) ||
                             dtlv_avp_encode_uint32 (msg_out, ESPADMIN_AVP_SYS_FLASH_ID, spi_flash_get_id ()) ||
                             dtlv_avp_encode_uint32 (msg_out, ESPADMIN_AVP_SYS_HEAP_FREE, system_get_free_heap_size ())
                             || dtlv_avp_encode_uint8 (msg_out, ESPADMIN_AVP_SYS_RST_REASON, rsti->reason));
    if ((rsti->reason == REASON_EXCEPTION_RST) || (rsti->exccause)) {
        dtlv_avp_t     *gavp_in;
        dtlv_avp_t     *gavp_epc;
        d_svcs_check_dtlv_error (dtlv_avp_encode_grouping (msg_out, 0, ESPADMIN_AVP_SYS_EXCEPTION, &gavp_in) ||
                                 dtlv_avp_encode_uint32 (msg_out, ESPADMIN_AVP_SYSEXC_CAUSE, rsti->exccause) ||
                                 dtlv_avp_encode_uint32 (msg_out, ESPADMIN_AVP_SYSEXC_ADDR, rsti->excvaddr) ||
                                 dtlv_avp_encode_list (msg_out, 0, ESPADMIN_AVP_SYSEXC_EPC, DTLV_TYPE_INTEGER,
                                                       &gavp_epc)
                                 || dtlv_avp_encode_uint32 (msg_out, ESPADMIN_AVP_SYSEXC_EPC, rsti->epc1)
                                 || dtlv_avp_encode_uint32 (msg_out, ESPADMIN_AVP_SYSEXC_EPC, rsti->epc2)
                                 || dtlv_avp_encode_uint32 (msg_out, ESPADMIN_AVP_SYSEXC_EPC, rsti->epc3)
                                 || dtlv_avp_encode_uint32 (msg_out, ESPADMIN_AVP_SYSEXC_EPC, rsti->depc)
                                 || dtlv_avp_encode_group_done (msg_out, gavp_epc)
                                 || dtlv_avp_encode_group_done (msg_out, gavp_in));
    }

    d_svcs_check_dtlv_error (dtlv_avp_encode_uint8 (msg_out, ESPADMIN_AVP_SYS_CPUFREQ, system_get_cpu_freq ()) ||
                             dtlv_avp_encode_uint8 (msg_out, ESPADMIN_AVP_SYS_BOOTVER, system_get_boot_version ()) ||
                             dtlv_avp_encode_group_done (msg_out, gavp));
#endif
    return SVCS_ERR_SUCCESS;
}


LOCAL svcs_errcode_t ICACHE_FLASH_ATTR
espadmin_on_msg_wireless (dtlv_ctx_t * msg_out)
{
#ifdef ARCH_XTENSA
    dtlv_avp_t     *gavp;
    // WIRELESS
    uint8           wifi_mode = wifi_get_opmode ();

    d_svcs_check_dtlv_error (dtlv_avp_encode_grouping (msg_out, 0, ESPADMIN_AVP_WIRELESS, &gavp) ||
                             dtlv_avp_encode_uint8 (msg_out, ESPADMIN_AVP_WIFI_OPMODE, wifi_mode) ||
                             dtlv_avp_encode_uint8 (msg_out, ESPADMIN_AVP_WIFI_SLEEP_TYPE, wifi_get_sleep_type ()));

    if ((wifi_mode == STATION_MODE) || (wifi_mode == STATIONAP_MODE)) {
        dtlv_avp_t     *gavp_in;
        d_svcs_check_dtlv_error (dtlv_avp_encode_grouping (msg_out, 0, ESPADMIN_AVP_WIFI_STATION, &gavp_in));

        struct station_config config;
        os_memset (&config, 0, sizeof (struct station_config));
        if (wifi_station_get_config (&config)) {
            d_svcs_check_dtlv_error (dtlv_avp_encode_char (msg_out, ESPADMIN_AVP_WIFI_SSID, (char *) config.ssid) ||
                                     dtlv_avp_encode_char (msg_out, ESPADMIN_AVP_WIFI_PASSWORD,
                                                           (char *) config.password));
        }

        d_svcs_check_dtlv_error (dtlv_avp_encode_uint8
                                 (msg_out, ESPADMIN_AVP_WIFI_AUTO_CONNECT, wifi_station_get_auto_connect ())
                                 || dtlv_avp_encode_uint8 (msg_out, ESPADMIN_AVP_WIFI_CONNECT_STATUS,
                                                           wifi_station_get_connect_status ()));

        uint8           macaddr[6];
        if (wifi_get_macaddr (STATION_IF, macaddr)) {
            d_svcs_check_dtlv_error (dtlv_avp_encode_octets
                                     (msg_out, COMMON_AVP_MAC48, sizeof (macaddr), (char *) macaddr));
        }
        struct ip_info  info;
        if (wifi_get_ip_info (STATION_IF, &info)) {
            d_svcs_check_dtlv_error (dtlv_avp_encode_octets
                                     (msg_out, COMMON_AVP_IPV4_ADDRESS, sizeof (struct ip_info), (char *) &info));
        }

        d_svcs_check_dtlv_error (dtlv_avp_encode_group_done (msg_out, gavp_in));
    }

    if ((wifi_mode == SOFTAP_MODE) || (wifi_mode == STATIONAP_MODE)) {
        dtlv_avp_t     *gavp_in;
        d_svcs_check_dtlv_error (dtlv_avp_encode_grouping (msg_out, 0, ESPADMIN_AVP_WIFI_SOFTAP, &gavp_in));

        struct softap_config config;
        os_memset (&config, 0, sizeof (struct softap_config));
        if (wifi_softap_get_config (&config)) {
            uint16 timeout = softap_timeout_get_last();
            d_svcs_check_dtlv_error (dtlv_avp_encode_nchar
                                     (msg_out, ESPADMIN_AVP_WIFI_SSID, config.ssid_len, (char *) config.ssid)
                                     || dtlv_avp_encode_char (msg_out, ESPADMIN_AVP_WIFI_PASSWORD,
                                                              (char *) config.password)
                                     || dtlv_avp_encode_uint8 (msg_out, ESPADMIN_AVP_WIFI_AUTH_MODE, config.authmode)
                                     || (timeout ? dtlv_avp_encode_uint16 (msg_out, ESPADMIN_AVP_WIFI_SOFTAP_TIMEOUT, timeout) : 0) );
        }

        uint8           macaddr[6];
        if (wifi_get_macaddr (SOFTAP_IF, macaddr)) {
            d_svcs_check_dtlv_error (dtlv_avp_encode_octets
                                     (msg_out, COMMON_AVP_MAC48, sizeof (macaddr), (char *) macaddr));
        }
        struct ip_info  info;
        if (wifi_get_ip_info (SOFTAP_IF, &info)) {
            d_svcs_check_dtlv_error (dtlv_avp_encode_octets
                                     (msg_out, COMMON_AVP_IPV4_ADDRESS, sizeof (struct ip_info), (char *) &info));
        }

        d_svcs_check_dtlv_error (dtlv_avp_encode_group_done (msg_out, gavp_in));
    }
    dtlv_avp_encode_group_done (msg_out, gavp);
#endif
    return SVCS_ERR_SUCCESS;
}


LOCAL svcs_errcode_t ICACHE_FLASH_ATTR
espadmin_info_xdb (dtlv_ctx_t * msg_out, imdb_hndlr_t hmdb)
{
    imdb_info_t     _imdb_info;
    imdb_class_info_t info_array[IMDB_INFO_ARRAY_SIZE];
    imdb_errcode_t  imdb_res = imdb_info (hmdb, &_imdb_info, info_array, IMDB_INFO_ARRAY_SIZE);

    if (imdb_res == IMDB_ERR_SUCCESS) {
        dtlv_avp_t     *gavp_in;

        d_svcs_check_dtlv_error (dtlv_avp_encode_uint16
                                 (msg_out, ESPADMIN_AVP_IMDB_BLOCK_SIZE, _imdb_info.db_def.block_size)
                                 || dtlv_avp_encode_uint32 (msg_out, ESPADMIN_AVP_IMDB_MEM_USED,
                                                            _imdb_info.stat.mem_alloc - _imdb_info.stat.mem_free)
                                 || dtlv_avp_encode_list (msg_out, 0, ESPADMIN_AVP_IMDB_CLASS, DTLV_TYPE_OBJECT,
                                                          &gavp_in));
        int             i;
        size_t          total_free = 0;
        uint16          total_blocks = 0;

        for (i = 0; i < MIN (_imdb_info.class_count, IMDB_INFO_ARRAY_SIZE); i++) {
            dtlv_avp_t     *gavp_in2;
            d_svcs_check_dtlv_error (dtlv_avp_encode_grouping (msg_out, 0, ESPADMIN_AVP_IMDB_CLASS, &gavp_in2));

            uint32          objcount = 0;
            imdb_class_forall (hmdb, info_array[i].hclass, (void *) &objcount, imdb_forall_count);

            total_blocks += info_array[i].blocks;
            total_free += info_array[i].slots_free_size + info_array[i].blocks_free * _imdb_info.db_def.block_size;

            d_svcs_check_dtlv_error (dtlv_avp_encode_nchar
                                     (msg_out, ESPADMIN_AVP_IMDB_CLASS_NAME, sizeof (class_name_t),
                                      info_array[i].cdef.name)
                                     || dtlv_avp_encode_uint8 (msg_out, ESPADMIN_AVP_IMDB_PAGE_COUNT,
                                                               info_array[i].pages)
                                     || dtlv_avp_encode_uint16 (msg_out, ESPADMIN_AVP_IMDB_BLOCK_COUNT,
                                                                info_array[i].blocks)
                                     || dtlv_avp_encode_uint16 (msg_out, ESPADMIN_AVP_IMDB_FREE_SLOTS,
                                                                info_array[i].slots_free)
                                     || dtlv_avp_encode_uint32 (msg_out, ESPADMIN_AVP_IMDB_FREE_SIZE,
                                                                info_array[i].slots_free_size +
                                                                info_array[i].blocks_free *
                                                                _imdb_info.db_def.block_size)
                                     || dtlv_avp_encode_uint16 (msg_out, ESPADMIN_AVP_IMDB_OBJECT_COUNT, objcount)
                                     || dtlv_avp_encode_group_done (msg_out, gavp_in2));
        }

        d_svcs_check_dtlv_error (dtlv_avp_encode_group_done (msg_out, gavp_in)
                                 || dtlv_avp_encode_uint16 (msg_out, ESPADMIN_AVP_IMDB_BLOCK_COUNT, total_blocks)
                                 || dtlv_avp_encode_uint32 (msg_out, ESPADMIN_AVP_IMDB_FREE_SIZE, total_free));
    }

    return SVCS_ERR_SUCCESS;
}

LOCAL svcs_errcode_t ICACHE_FLASH_ATTR
espadmin_on_msg_imdb (dtlv_ctx_t * msg_out)
{
    dtlv_avp_t     *gavp;
    // IMDB
    d_svcs_check_dtlv_error (dtlv_avp_encode_grouping (msg_out, 0, ESPADMIN_AVP_IMDB, &gavp));

    espadmin_info_xdb (msg_out, get_hmdb ());

    d_svcs_check_dtlv_error (dtlv_avp_encode_group_done (msg_out, gavp));

    return SVCS_ERR_SUCCESS;
}

LOCAL svcs_errcode_t ICACHE_FLASH_ATTR
espadmin_on_msg_fdb (dtlv_ctx_t * msg_out)
{
    dtlv_avp_t     *gavp;
    dtlv_avp_t     *gavp_in;
    // FDB
    d_svcs_check_dtlv_error (dtlv_avp_encode_grouping (msg_out, 0, ESPADMIN_AVP_FDB, &gavp) ||
                             dtlv_avp_encode_grouping (msg_out, 0, ESPADMIN_AVP_FDB_INFO, &gavp_in));

    imdb_file_t     hdr_file;
    if (fdb_header_read (&hdr_file) == IMDB_ERR_SUCCESS) {
#ifdef ARCH_XTENSA
        flash_ota_map_t *fwmap = get_flash_ota_map ();
#endif
        d_svcs_check_dtlv_error (
#ifdef ARCH_XTENSA
                                    dtlv_avp_encode_uint32 (msg_out, ESPADMIN_AVP_FDB_DATA_ADDR,
                                                            d_flash_user2_data_addr (fwmap)) ||
#endif
                                    dtlv_avp_encode_uint32 (msg_out, ESPADMIN_AVP_FDB_DATA_SIZE, fio_user_size ())
                                    || dtlv_avp_encode_uint32 (msg_out, ESPADMIN_AVP_FDB_FILE_SIZE,
                                                               hdr_file.file_size * hdr_file.block_size)
                                    || dtlv_avp_encode_uint32 (msg_out, ESPADMIN_AVP_FDB_FILE_HWM,
                                                               hdr_file.file_hwm * hdr_file.block_size)
            );
    }
    d_svcs_check_dtlv_error (dtlv_avp_encode_group_done (msg_out, gavp_in));

    espadmin_info_xdb (msg_out, get_fdb ());

    d_svcs_check_dtlv_error (dtlv_avp_encode_group_done (msg_out, gavp));

    return SVCS_ERR_SUCCESS;
}

LOCAL svcs_errcode_t ICACHE_FLASH_ATTR
espadmin_on_msg_info (dtlv_ctx_t * msg_in, dtlv_ctx_t * msg_out)
{
    d_svcs_check_svcs_error (espadmin_on_msg_product (msg_out));


    if (msg_in) {
        dtlv_errcode_t  dtlv_res = DTLV_ERR_SUCCESS;
        dtlv_davp_t     davp;
        svcs_errcode_t  res = SVCS_ERR_SUCCESS;
        while (dtlv_res == DTLV_ERR_SUCCESS) {

            dtlv_res = dtlv_avp_decode (msg_in, &davp);
            if ((dtlv_res != DTLV_ERR_SUCCESS) && (!dtlv_check_namespace (&davp, ESPADMIN_SERVICE_ID)))
                break;

            switch (davp.havpd.nscode.comp.code) {
            case ESPADMIN_AVP_SYSTEM:
                res = espadmin_on_msg_system (msg_out);
                break;
            case ESPADMIN_AVP_FIRMWARE:
                res = espadmin_on_msg_firmware (msg_out);
                break;
            case ESPADMIN_AVP_WIRELESS:
                res = espadmin_on_msg_wireless (msg_out);
                break;
            case ESPADMIN_AVP_IMDB:
                res = espadmin_on_msg_imdb (msg_out);
                break;
            case ESPADMIN_AVP_FDB:
                res = espadmin_on_msg_fdb (msg_out);
                break;
            default:
                continue;
            }

            d_svcs_check_svcs_error (res);
        }
    }

    return SVCS_ERR_SUCCESS;
}

#ifdef ARCH_XTENSA
LOCAL svcs_errcode_t ICACHE_FLASH_ATTR
espadmin_on_msg_fwupdate_info (dtlv_ctx_t * msg_out, upgrade_err_t ures)
{
    upgrade_info_t  info;
    fwupdate_info (&info);

    d_svcs_check_dtlv_error (dtlv_avp_encode_uint8 (msg_out, COMMON_AVP_RESULT_EXT_CODE, ures) ||
                             dtlv_avp_encode_uint8 (msg_out, ESPADMIN_AVP_OTA_STATE, info.state) ||
                             ((info.state) ? (dtlv_avp_encode_uint32 (msg_out, ESPADMIN_AVP_FW_ADDR, info.fwbin_start_addr) ||
                                             dtlv_avp_encode_uint32 (msg_out, ESPADMIN_AVP_OTA_CURRENT_ADDR, info.fwbin_curr_addr)) : false) );
    return SVCS_ERR_SUCCESS;
}
#endif

#ifdef ARCH_XTENSA
LOCAL svcs_errcode_t ICACHE_FLASH_ATTR
espadmin_on_msg_fwupdate_init (dtlv_ctx_t * msg_in, dtlv_ctx_t * msg_out)
{
    if (!msg_in)
        return SVCS_INVALID_MESSAGE;

    firmware_info_t fwinfo;
    os_memset (&fwinfo, 0, sizeof (firmware_info_t));
    firmware_digest_t init_digest;

    dtlv_davp_t     davp;
    while (dtlv_avp_decode (msg_in, &davp) == DTLV_ERR_SUCCESS) {
        if (!dtlv_check_namespace (&davp, ESPADMIN_SERVICE_ID))
            continue;

        switch (davp.havpd.nscode.comp.code) {
        case ESPADMIN_AVP_FW_INIT_DIGEST:
            if (davp.havpd.length != sizeof (firmware_digest_t) + sizeof (dtlv_havpe_t))
                return SVCS_INVALID_MESSAGE;
            os_memcpy (init_digest, davp.avp->data, sizeof (firmware_digest_t));
            break;
        case ESPADMIN_AVP_FW_INFO:
            if (davp.havpd.length != sizeof (firmware_info_t) + sizeof (dtlv_havpe_t))
                return SVCS_INVALID_MESSAGE;
            os_memcpy (&fwinfo, davp.avp->data, sizeof (firmware_info_t));
            break;
        }
    }

    upgrade_err_t   ures = fwupdate_init (&fwinfo, &init_digest);
    d_svcs_check_svcs_error (espadmin_on_msg_fwupdate_info (msg_out, ures));

    return SVCS_ERR_SUCCESS;
}
#endif

#ifdef ARCH_XTENSA
LOCAL svcs_errcode_t ICACHE_FLASH_ATTR
espadmin_on_msg_fwupdate_upload (dtlv_ctx_t * msg_in, dtlv_ctx_t * msg_out)
{
    if (!msg_in)
        return SVCS_INVALID_MESSAGE;

    dtlv_davp_t     davp;
    while (dtlv_avp_decode (msg_in, &davp) == DTLV_ERR_SUCCESS) {
        if (!dtlv_check_namespace (&davp, ESPADMIN_SERVICE_ID))
            break;
        if (davp.havpd.nscode.comp.code == ESPADMIN_AVP_OTA_BIN_DATA)
            goto upload_final;
    }

    return SVCS_INVALID_MESSAGE;

  upload_final:
    {
        upgrade_err_t   ures = fwupdate_upload ((uint8 *) davp.avp->data, davp.havpd.length - sizeof (dtlv_havpe_t));
        d_svcs_check_svcs_error (espadmin_on_msg_fwupdate_info (msg_out, ures));
    }
    return SVCS_ERR_SUCCESS;
}
#endif

#ifdef ARCH_XTENSA
LOCAL void      ICACHE_FLASH_ATTR
task_system_restart (void *args)
{
    system_shutdown ();
    system_restart ();
}

LOCAL void      ICACHE_FLASH_ATTR
task_fdb_truncate (void *args)
{
    system_shutdown ();
    fio_user_format (1);
    system_restart ();
}
#endif

svcs_errcode_t  ICACHE_FLASH_ATTR
espadmin_on_message (service_ident_t orig_id, service_msgtype_t msgtype, void *ctxdata, dtlv_ctx_t * msg_in,
                     dtlv_ctx_t * msg_out)
{
    svcs_errcode_t  res = SVCS_ERR_SUCCESS;
    switch (msgtype) {
    case SVCS_MSGTYPE_INFO:
        res = espadmin_on_msg_info (msg_in, msg_out);
        break;
#ifdef ARCH_XTENSA
    case ESPADMIN_MSGTYPE_RESTART:
        system_post_delayed_cb (task_system_restart, NULL);
        break;
    case ESPADMIN_MSGTYPE_FW_OTA_ABORT:
        {
            upgrade_err_t   ures = fwupdate_abort ();
            d_svcs_check_svcs_error (espadmin_on_msg_fwupdate_info (msg_out, ures));
        }
        break;
    case ESPADMIN_MSGTYPE_FW_OTA_INIT:
        res = espadmin_on_msg_fwupdate_init (msg_in, msg_out);
        break;
    case ESPADMIN_MSGTYPE_FW_OTA_UPLOAD:
        res = espadmin_on_msg_fwupdate_upload (msg_in, msg_out);
        break;
    case ESPADMIN_MSGTYPE_FW_OTA_DONE:
        d_svcs_check_svcs_error (espadmin_on_msg_fwupdate_info (msg_out, fwupdate_done ()));
        break;
    case ESPADMIN_MSGTYPE_FW_OTA_VERIFY_DONE:
        d_svcs_check_svcs_error (espadmin_on_msg_fwupdate_info (msg_out, fwupdate_verify_done ()));
        break;
    case ESPADMIN_MSGTYPE_FW_VERIFY:
        {
            espadmin_on_msg_product (msg_out);
            espadmin_on_msg_firmware (msg_out);
            dtlv_avp_encode_uint8 (msg_out, COMMON_AVP_RESULT_EXT_CODE,
                                   fw_verify (&fw_info, (firmware_digest_t *) APP_INIT_DIGEST));
        }
        break;
    case ESPADMIN_MSGTYPE_FDB_TRUNC:
        system_post_delayed_cb (task_fdb_truncate, NULL);
        break;
#endif
    default:
        res = SVCS_MSGTYPE_INVALID;
    }

    return res;
}

svcs_errcode_t  ICACHE_FLASH_ATTR
espadmin_on_cfgupd (dtlv_ctx_t * conf)
{
#ifdef ARCH_XTENSA
    uint8           wifi_mode = ESPADMIN_DEFAULT_WIFI_MODE;
    uint8           wifi_st_ssid[32] = ESPADMIN_DEFAULT_WIFI_ST_SSID;
    uint8           wifi_st_password[64] = ESPADMIN_DEFAULT_WIFI_ST_PASSWORD;
    uint8           wifi_autoconnect = ESPADMIN_DEFAULT_WIFI_AUTO_CONNECT;
    uint8           wifi_sleep_type = ESPADMIN_DEFAULT_WIFI_SLEEP_TYPE;
    uint16          softap_timeout = ESPADMIN_DEFAULT_WIFI_SOFTAP_TIMEOUT;

#ifdef ESPADMIN_DEFAULT_WIFI_AP_PASSWORD
    uint8           wifi_ap_password[64] = ESPADMIN_DEFAULT_WIFI_AP_PASSWORD;
#else
    uint8           wifi_ap_password[64];
    system_get_default_secret (wifi_ap_password, sizeof (wifi_ap_password));
#endif
#ifdef ESPADMIN_DEFAULT_WIFI_AP_SSID
    uint8           wifi_ap_ssid[32] = ESPADMIN_DEFAULT_WIFI_AP_SSID;
    size_t          wifi_ap_ssid_len = sizeof (ESPADMIN_DEFAULT_WIFI_AP_SSID);
#else
    uint8           wifi_ap_ssid[32];
    size_t          wifi_ap_ssid_len = system_get_default_ssid (wifi_ap_ssid, sizeof (wifi_ap_ssid));
#endif
    uint8           wifi_ap_ssid_hidden = ESPADMIN_DEFAULT_WIFI_AP_HIDDEN;
    AUTH_MODE       wifi_ap_auth_mode = ESPADMIN_DEFAULT_WIFI_AP_AUTH_MODE;

    if (conf) {
        const char     *sysdescr = NULL;
        char     *hostname = NULL;

        dtlv_ctx_t      wifi_conf;
        os_memset (&wifi_conf, 0, sizeof (dtlv_ctx_t));

        dtlv_seq_decode_begin (conf, ESPADMIN_SERVICE_ID);
        dtlv_seq_decode_group (ESPADMIN_AVP_WIRELESS, wifi_conf.buf, wifi_conf.datalen);
        dtlv_seq_decode_ptr (COMMON_AVP_SYSTEM_DESCRIPTION, sysdescr, char);
        dtlv_seq_decode_ptr (COMMON_AVP_HOST_NAME, hostname, char);
        dtlv_seq_decode_end (conf);

        if (sysdescr)
            system_set_description (sysdescr);

        if (hostname && os_strlen (hostname)) {
            if (! wifi_station_set_hostname (hostname))
                d_log_eprintf (ESPADMIN_SERVICE_NAME, "failed set hostname: %s", hostname);
        }

        if (wifi_conf.buf) {
            dtlv_ctx_reset_decode (&wifi_conf);

            dtlv_ctx_t      softap_conf;
            os_memset (&softap_conf, 0, sizeof (dtlv_ctx_t));
            dtlv_ctx_t      station_conf;
            os_memset (&station_conf, 0, sizeof (dtlv_ctx_t));

            dtlv_seq_decode_begin (&wifi_conf, ESPADMIN_SERVICE_ID);
            dtlv_seq_decode_uint8 (ESPADMIN_AVP_WIFI_OPMODE, &wifi_mode);
            dtlv_seq_decode_uint8 (ESPADMIN_AVP_WIFI_SLEEP_TYPE, &wifi_sleep_type);
            dtlv_seq_decode_group (ESPADMIN_AVP_WIFI_SOFTAP, softap_conf.buf, softap_conf.datalen);
            dtlv_seq_decode_group (ESPADMIN_AVP_WIFI_STATION, station_conf.buf, station_conf.datalen);
            dtlv_seq_decode_end (&wifi_conf);

            if (softap_conf.buf) {
                char           *password = NULL;
                char           *ssid = NULL;

                dtlv_ctx_reset_decode (&softap_conf);
                dtlv_seq_decode_begin (&softap_conf, ESPADMIN_SERVICE_ID);
                dtlv_seq_decode_uint16 (ESPADMIN_AVP_WIFI_SOFTAP_TIMEOUT, & softap_timeout);
                dtlv_seq_decode_uint8 (ESPADMIN_AVP_WIFI_AUTH_MODE, (uint8 *) & wifi_ap_auth_mode);
                dtlv_seq_decode_ptr (ESPADMIN_AVP_WIFI_PASSWORD, password, char);
                dtlv_seq_decode_ptr (ESPADMIN_AVP_WIFI_SSID, ssid, char);
                dtlv_seq_decode_end (&softap_conf);

                if (password && *password)
                    os_strncpy ((char *) wifi_ap_password, (char *) password, sizeof (wifi_ap_password));

                wifi_ap_ssid_len = os_strlen (ssid);
                if (wifi_ap_ssid_len)
                    os_strncpy ((char *) wifi_ap_ssid, (char *) ssid, sizeof (wifi_ap_ssid));
            }

            if (station_conf.buf) {
                char           *password = NULL;
                char           *ssid = NULL;
                dtlv_ctx_reset_decode (&station_conf);
                dtlv_seq_decode_begin (&station_conf, ESPADMIN_SERVICE_ID);
                dtlv_seq_decode_uint8 (ESPADMIN_AVP_WIFI_AUTO_CONNECT, &wifi_autoconnect);
                dtlv_seq_decode_ptr (ESPADMIN_AVP_WIFI_PASSWORD, password, char);
                dtlv_seq_decode_ptr (ESPADMIN_AVP_WIFI_SSID, ssid, char);
                dtlv_seq_decode_end (&station_conf);

                if (password && *password)
                    os_strncpy ((char *) wifi_st_password, (char *) password, sizeof (wifi_st_password));

                if (ssid && *ssid)
                    os_strncpy ((char *) wifi_st_ssid, (char *) ssid, sizeof (wifi_st_ssid));
            }
        }
    }

    // setup WiFi
    d_log_iprintf (ESPADMIN_SERVICE_NAME, "setup wifi mode:%u, sleep_type:%u", wifi_mode, wifi_sleep_type);

    if (!wifi_set_opmode (wifi_mode))
        d_log_eprintf (ESPADMIN_SERVICE_NAME, "wifi set opmode:%u", wifi_mode);
    if (!wifi_set_sleep_type (wifi_sleep_type))
        d_log_eprintf (ESPADMIN_SERVICE_NAME, "wifi set sleep_type:%u", wifi_sleep_type);

    if ((wifi_mode == STATION_MODE) || (wifi_mode == STATIONAP_MODE)) {
        struct station_config config;
        wifi_station_get_config (&config);
        os_strncpy ((char *) config.ssid, (char *) wifi_st_ssid, sizeof (wifi_st_ssid));
        os_strncpy ((char *) config.password, (char *) wifi_st_password, sizeof (wifi_st_password));

        d_log_iprintf (ESPADMIN_SERVICE_NAME, "\tstation ssid:%s, password:%s, auto:%u", config.ssid, wifi_st_password, wifi_autoconnect);
        if (!wifi_station_set_config (&config))
            d_log_eprintf (ESPADMIN_SERVICE_NAME, "wifi set station config");
        wifi_station_set_auto_connect (wifi_autoconnect);
    }
    if ((wifi_mode == SOFTAP_MODE) || (wifi_mode == STATIONAP_MODE)) {
        struct softap_config config;
        wifi_softap_get_config (&config);
        if (wifi_ap_ssid_len) {
            os_strncpy ((char *) config.ssid, (char *) wifi_ap_ssid, sizeof (wifi_ap_ssid));
            config.ssid_len = wifi_ap_ssid_len;
        }
        os_strncpy ((char *) config.password, (char *) wifi_ap_password, sizeof (wifi_ap_password));
        config.ssid_hidden = wifi_ap_ssid_hidden;
        config.authmode = wifi_ap_auth_mode;

        d_log_iprintf (ESPADMIN_SERVICE_NAME, "\tsoftap ssid:%s, password:%s, auth:%u", config.ssid, config.password, config.authmode);
        if (!wifi_softap_set_config (&config))
            d_log_eprintf (ESPADMIN_SERVICE_NAME, "wifi set softap config");

        if (softap_timeout)
            softap_timeout_set (softap_timeout);
    }
#endif

    return SVCS_ERR_SUCCESS;
}


svcs_errcode_t  ICACHE_FLASH_ATTR
espadmin_service_install (bool enabled)
{
    svcs_service_def_t sdef;
    os_memset (&sdef, 0, sizeof (sdef));
    sdef.enabled = enabled;
    sdef.multicast = false;
    sdef.on_start = espadmin_on_start;
    sdef.on_stop = espadmin_on_stop;
    sdef.on_message = espadmin_on_message;
    sdef.on_cfgupd = espadmin_on_cfgupd;

    return svcctl_service_install (ESPADMIN_SERVICE_ID, ESPADMIN_SERVICE_NAME, &sdef);
}

svcs_errcode_t  ICACHE_FLASH_ATTR
espadmin_service_uninstall (void)
{
    return svcctl_service_uninstall (ESPADMIN_SERVICE_NAME);
}
