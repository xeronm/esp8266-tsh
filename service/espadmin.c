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
#include "fwupgrade.h"
#include "flashmap.h"
#include "core/logging.h"
#include "core/utils.h"
#include "core/config.h"
#include "core/system.h"
#include "system/services.h"
#include "system/comavp.h"
#include "service/espadmin.h"

#define IMDB_INFO_ARRAY_SIZE	10

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
    APP_INIT_DIGEST
};

svcs_errcode_t  ICACHE_FLASH_ATTR
espadmin_on_start (imdb_hndlr_t hmdb, imdb_hndlr_t hdata, dtlv_ctx_t * conf)
{
    espadmin_on_cfgupd (conf);

    return SVCS_ERR_SUCCESS;
}

svcs_errcode_t  ICACHE_FLASH_ATTR
espadmin_on_stop ()
{
    return SVCS_ERR_SUCCESS;
}

#define VERSION_BUFFER_SIZE	30

LOCAL svcs_errcode_t ICACHE_FLASH_ATTR
espadmin_on_msg_product (dtlv_ctx_t * msg_out)
{
    char            version[VERSION_BUFFER_SIZE];
    os_snprintf (version, VERSION_BUFFER_SIZE, FW_VERSTR, FW_VER2STR (&fw_info));
    d_svcs_check_dtlv_error (dtlv_avp_encode_nchar
			     (msg_out, COMMON_AVP_APP_PRODUCT, sizeof (fw_info.product), fw_info.product)
			     || dtlv_avp_encode_nchar (msg_out, COMMON_AVP_APP_VERSION, VERSION_BUFFER_SIZE, version));

    return SVCS_ERR_SUCCESS;
}

LOCAL svcs_errcode_t ICACHE_FLASH_ATTR
espadmin_on_msg_firmware (dtlv_ctx_t * msg_out)
{
    dtlv_avp_t     *gavp;
    // FIRMWARE
    flash_ota_map_t * fwmap = get_flash_ota_map ();
    d_svcs_check_dtlv_error (dtlv_avp_encode_grouping (msg_out, 0, ESPADMIN_AVP_FIRMWARE, &gavp) ||
			     dtlv_avp_encode_uint32 (msg_out, ESPADMIN_AVP_FW_ADDR, system_get_userbin_addr ()) ||
			     dtlv_avp_encode_uint8 (msg_out, ESPADMIN_AVP_FW_SIZE_MAP, system_get_flash_size_map ()) ||
			     dtlv_avp_encode_uint32 (msg_out, ESPADMIN_AVP_FW_BIN_SIZE, fw_info.binsize) ||
			     dtlv_avp_encode_uint32 (msg_out, ESPADMIN_AVP_FW_USER_DATA_ADDR, d_flash_user2_data_addr (fwmap)) ||
			     dtlv_avp_encode_uint32 (msg_out, ESPADMIN_AVP_FW_USER_DATA_SIZE, fio_user_size ()) ||
			     dtlv_avp_encode_uint32 (msg_out, ESPADMIN_AVP_FW_RELEASE_DATE, fw_info.release_date) ||
			     dtlv_avp_encode_octets (msg_out, ESPADMIN_AVP_FW_DIGEST, sizeof (fw_info.digest),
						     (char *) fw_info.digest)
			     || dtlv_avp_encode_octets (msg_out, ESPADMIN_AVP_FW_INIT_DIGEST, sizeof (fw_info.digest),
							APP_INIT_DIGEST) || dtlv_avp_encode_group_done (msg_out, gavp));

    return SVCS_ERR_SUCCESS;
}

LOCAL svcs_errcode_t ICACHE_FLASH_ATTR
espadmin_on_msg_system (dtlv_ctx_t * msg_out)
{
    dtlv_avp_t     *gavp;
    // SYSTEM
    struct rst_info *rsti = system_get_rst_info ();

    d_svcs_check_dtlv_error (dtlv_avp_encode_grouping (msg_out, 0, ESPADMIN_AVP_SYSTEM, &gavp) ||
			     dtlv_avp_encode_char (msg_out, ESPADMIN_AVP_SYS_SDKVERSION, system_get_sdk_version ()) ||
			     dtlv_avp_encode_uint32 (msg_out, ESPADMIN_AVP_SYS_CHIP_ID, system_get_chip_id ()) ||
			     dtlv_avp_encode_uint32 (msg_out, ESPADMIN_AVP_SYS_FLASH_ID, spi_flash_get_id ()) ||
			     dtlv_avp_encode_uint32 (msg_out, ESPADMIN_AVP_SYS_UPTIME, lt_ctime ()) ||
			     dtlv_avp_encode_uint32 (msg_out, ESPADMIN_AVP_SYS_HEAP_FREE, system_get_free_heap_size ()) ||
			     dtlv_avp_encode_uint8 (msg_out, ESPADMIN_AVP_SYS_RST_REASON, rsti->reason));
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

    return SVCS_ERR_SUCCESS;
}


LOCAL svcs_errcode_t ICACHE_FLASH_ATTR
espadmin_on_msg_wireless (dtlv_ctx_t * msg_out)
{
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
	    d_svcs_check_dtlv_error (dtlv_avp_encode_nchar
				     (msg_out, ESPADMIN_AVP_WIFI_SSID, config.ssid_len, (char *) config.ssid)
				     || dtlv_avp_encode_char (msg_out, ESPADMIN_AVP_WIFI_PASSWORD,
							      (char *) config.password)
				     || dtlv_avp_encode_uint8 (msg_out, ESPADMIN_AVP_WIFI_AUTH_MODE, config.authmode));
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

    return SVCS_ERR_SUCCESS;
}

LOCAL svcs_errcode_t ICACHE_FLASH_ATTR
espadmin_on_msg_imdb (dtlv_ctx_t * msg_out)
{
    dtlv_avp_t     *gavp;
    // IMDB
    d_svcs_check_dtlv_error (dtlv_avp_encode_grouping (msg_out, 0, ESPADMIN_AVP_IMDB, &gavp));

    imdb_info_t     _imdb_info;
    imdb_class_info_t info_array[IMDB_INFO_ARRAY_SIZE];
    imdb_errcode_t  imdb_res = imdb_info (get_hmdb (), &_imdb_info, info_array, IMDB_INFO_ARRAY_SIZE);

    if (imdb_res == IMDB_ERR_SUCCESS) {
	dtlv_avp_t     *gavp_in;

	d_svcs_check_dtlv_error (dtlv_avp_encode_uint16
				 (msg_out, ESPADMIN_AVP_IMDB_BLOCK_SIZE, _imdb_info.db_def.block_size)
				 || dtlv_avp_encode_list (msg_out, 0, ESPADMIN_AVP_IMDB_CLASS, DTLV_TYPE_OBJECT,
							  &gavp_in));
	int             i;
	size_t          total_free = 0;
	uint16          total_blocks = 0;

	for (i = 0; i < MIN (_imdb_info.class_count, IMDB_INFO_ARRAY_SIZE); i++) {
	    dtlv_avp_t     *gavp_in2;
	    d_svcs_check_dtlv_error (dtlv_avp_encode_grouping (msg_out, 0, ESPADMIN_AVP_IMDB_CLASS, &gavp_in2));

	    uint32          objcount = 0;
	    imdb_class_forall (info_array[i].hclass, (void *) &objcount, forall_count);

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
								info_array[i].blocks_free * _imdb_info.db_def.block_size)
				     || dtlv_avp_encode_uint16 (msg_out, ESPADMIN_AVP_IMDB_OBJECT_COUNT, objcount)
				     || dtlv_avp_encode_group_done (msg_out, gavp_in2));
	}

	d_svcs_check_dtlv_error (dtlv_avp_encode_group_done (msg_out, gavp_in)
                                 || dtlv_avp_encode_uint16 (msg_out, ESPADMIN_AVP_IMDB_BLOCK_COUNT, total_blocks)
                                 || dtlv_avp_encode_uint32 (msg_out, ESPADMIN_AVP_IMDB_FREE_SIZE, total_free));
    }

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
	    default:
		continue;
	    }

	    d_svcs_check_svcs_error (res);
	}
    }

    return SVCS_ERR_SUCCESS;
}

LOCAL svcs_errcode_t ICACHE_FLASH_ATTR
espadmin_on_msg_fwupdate_info (dtlv_ctx_t * msg_out, upgrade_err_t ures)
{
    upgrade_info_t  info;
    fwupdate_info (&info);

    d_svcs_check_dtlv_error (dtlv_avp_encode_uint8 (msg_out, COMMON_AVP_RESULT_EXT_CODE, ures) ||
			     dtlv_avp_encode_uint8 (msg_out, ESPADMIN_AVP_OTA_STATE, info.state) ||
			     dtlv_avp_encode_uint32 (msg_out, ESPADMIN_AVP_FW_ADDR, info.fwbin_start_addr) ||
			     dtlv_avp_encode_uint32 (msg_out, ESPADMIN_AVP_OTA_CURRENT_ADDR, info.fwbin_curr_addr));

    return SVCS_ERR_SUCCESS;
}

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

LOCAL void      ICACHE_FLASH_ATTR
system_restart_timeout (void *args)
{
    system_shutdown ();
    system_restart ();
}

svcs_errcode_t  ICACHE_FLASH_ATTR
espadmin_on_message (service_ident_t orig_id, service_msgtype_t msgtype, void *ctxdata, dtlv_ctx_t * msg_in,
		     dtlv_ctx_t * msg_out)
{
    svcs_errcode_t  res = SVCS_ERR_SUCCESS;
    switch (msgtype) {
    case SVCS_MSGTYPE_INFO:
	res = espadmin_on_msg_info (msg_in, msg_out);
	break;
    case ESPADMIN_MSGTYPE_RESTART:
        {
            os_timer_t     *timer;
            st_zalloc(timer, os_timer_t);

            os_timer_disarm (timer);
            os_timer_setfn (timer, system_restart_timeout, NULL);
            os_timer_arm (timer, 10, false);
            d_log_wprintf (ESPADMIN_SERVICE_NAME, "restart timeout:%d msec", 10);
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
    case ESPADMIN_MSGTYPE_FW_VERIFY:
	{
	    espadmin_on_msg_product (msg_out);
	    espadmin_on_msg_firmware (msg_out);
	    dtlv_avp_encode_uint8 (msg_out, COMMON_AVP_RESULT_EXT_CODE,
				   fw_verify (&fw_info, (firmware_digest_t *) APP_INIT_DIGEST));
	}
	break;
    default:
	res = SVCS_MSGTYPE_INVALID;
    }

    return res;
}

svcs_errcode_t  ICACHE_FLASH_ATTR
espadmin_on_cfgupd (dtlv_ctx_t * conf)
{
    uint8           wifi_mode = ESPADMIN_DEFAULT_WIFI_MODE;
    uint8           wifi_st_ssid[32] = ESPADMIN_DEFAULT_WIFI_ST_SSID;
    uint8           wifi_st_password[64] = ESPADMIN_DEFAULT_WIFI_ST_PASSWORD;
    uint8           wifi_autoconnect = ESPADMIN_DEFAULT_WIFI_AUTO_CONNECT;
#ifdef ESPADMIN_DEFAULT_WIFI_AP_PASSWORD
    uint8           wifi_ap_password[64] = ESPADMIN_DEFAULT_WIFI_AP_PASSWORD;
#else
    uint8           wifi_ap_password[64];
    os_memset (wifi_ap_password, 0, sizeof (wifi_ap_password));
    system_get_default_secret (wifi_ap_password, sizeof (wifi_ap_password));
#endif
    uint8           wifi_ap_ssid_hidden = ESPADMIN_DEFAULT_WIFI_AP_HIDDEN;
    AUTH_MODE       wifi_ap_auth_mode = ESPADMIN_DEFAULT_WIFI_AP_AUTH_MODE;

    if (conf) {
	dtlv_errcode_t  dtlv_res = DTLV_ERR_SUCCESS;
	dtlv_davp_t     davp;
	while (dtlv_res == DTLV_ERR_SUCCESS) {
	    dtlv_res = dtlv_avp_decode (conf, &davp);
	    switch (davp.havpd.nscode.nscode) {
	    case ESPADMIN_AVP_WIRELESS:
		// TODO: ...
		break;
	    default:
		continue;
	    }
	}
    }

    // setup WiFi
    d_log_iprintf (ESPADMIN_SERVICE_NAME, "setup wifi mode:%u", wifi_mode);

    wifi_set_opmode (wifi_mode);

    if ((wifi_mode == STATION_MODE) || (wifi_mode == STATIONAP_MODE)) {
	struct station_config config;
	wifi_station_get_config (&config);
	os_memcpy (config.ssid, wifi_st_ssid, sizeof (wifi_st_ssid));
	os_memcpy (config.password, wifi_st_password, sizeof (wifi_st_password));

	d_log_iprintf (ESPADMIN_SERVICE_NAME, "\tstation ssid:%s, auto:%u", config.ssid, wifi_autoconnect);
	wifi_station_set_config (&config);
	wifi_station_set_auto_connect (wifi_autoconnect);
    }
    if ((wifi_mode == SOFTAP_MODE) || (wifi_mode == STATIONAP_MODE)) {
	struct softap_config config;
	wifi_softap_get_config (&config);
	os_memcpy (config.password, wifi_ap_password, sizeof (wifi_ap_password));
	config.ssid_hidden = wifi_ap_ssid_hidden;
	config.authmode = wifi_ap_auth_mode;

	d_log_iprintf (ESPADMIN_SERVICE_NAME, "\tsoftap ssid:%s, password:%s", config.ssid, config.password);
	wifi_softap_set_config (&config);
    }

    return SVCS_ERR_SUCCESS;
}


svcs_errcode_t  ICACHE_FLASH_ATTR
espadmin_service_install (void)
{
    svcs_service_def_t sdef;
    os_memset (&sdef, 0, sizeof (sdef));
    sdef.enabled = true;
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
