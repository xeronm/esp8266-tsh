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

#ifndef _ESPADMIN_H_
#define _ESPADMIN_H_ 1

#include "sysinit.h"
#include "core/config.h"
#include "system/services.h"
#include "proto/dtlv.h"

#define ESPADMIN_DEFAULT_WIFI_MODE		STATIONAP_MODE
#define ESPADMIN_DEFAULT_WIFI_SLEEP_TYPE	MODEM_SLEEP_T
#define ESPADMIN_DEFAULT_WIFI_SOFTAP_TIMEOUT	300

#ifdef APP_WIFI_SSID
#  define ESPADMIN_DEFAULT_WIFI_ST_SSID		APP_WIFI_SSID
#else
#  define ESPADMIN_DEFAULT_WIFI_ST_SSID		"ESPTSH"
#endif
#ifdef APP_WIFI_PASSWORD
#  define ESPADMIN_DEFAULT_WIFI_ST_PASSWORD	APP_WIFI_PASSWORD
#else
#  define ESPADMIN_DEFAULT_WIFI_ST_PASSWORD	"defaultpassword"
#endif

#define ESPADMIN_DEFAULT_WIFI_AUTO_CONNECT	1
#undef ESPADMIN_DEFAULT_WIFI_AP_PASSWORD
#define ESPADMIN_DEFAULT_WIFI_AP_HIDDEN		0
#define ESPADMIN_DEFAULT_WIFI_AP_AUTH_MODE	AUTH_WPA2_PSK

#define VERSION_BUFFER_SIZE	30

#define ESPADMIN_SERVICE_ID		3
#define ESPADMIN_SERVICE_NAME		"espadmin"

typedef enum espadmin_msgtype_e {
    ESPADMIN_MSGTYPE_RESTART = 10,
    ESPADMIN_MSGTYPE_FDB_TRUNC = 11,
    ESPADMIN_MSGTYPE_FW_OTA_INIT = 12,
    ESPADMIN_MSGTYPE_FW_OTA_UPLOAD = 13,
    ESPADMIN_MSGTYPE_FW_OTA_DONE = 14,
    ESPADMIN_MSGTYPE_FW_OTA_ABORT = 15,
    ESPADMIN_MSGTYPE_FW_OTA_VERIFY_DONE = 16,
    ESPADMIN_MSGTYPE_FW_VERIFY = 17,
} espadmin_msgtype_t;

typedef enum espadmin_avp_code_e {
    ESPADMIN_AVP_SYSTEM = 102,
    ESPADMIN_AVP_WIRELESS = 103,
    ESPADMIN_AVP_FIRMWARE = 104,
    ESPADMIN_AVP_IMDB = 105,
    ESPADMIN_AVP_WIFI_STATION = 106,
    ESPADMIN_AVP_WIFI_SOFTAP = 107,
    ESPADMIN_AVP_FDB = 108,
    // System
    ESPADMIN_AVP_SYS_SDKVERSION = 111,
    ESPADMIN_AVP_SYS_UPTIME = 112,
    ESPADMIN_AVP_SYS_CHIP_ID = 113,
    ESPADMIN_AVP_SYS_FLASH_ID = 114,
    ESPADMIN_AVP_SYS_CPUFREQ = 115,
    ESPADMIN_AVP_SYS_BOOTVER = 116,
    ESPADMIN_AVP_SYS_HEAP_FREE = 117,
    ESPADMIN_AVP_SYS_RST_REASON = 118,
    ESPADMIN_AVP_SYS_EXCEPTION = 119,
    ESPADMIN_AVP_SYSEXC_CAUSE = 120,
    ESPADMIN_AVP_SYSEXC_ADDR = 121,
    ESPADMIN_AVP_SYSEXC_EPC = 122,
    // Firmware
    ESPADMIN_AVP_FW_USER_BIN = 125,
    ESPADMIN_AVP_FW_ADDR = 126,
    ESPADMIN_AVP_FW_SIZE_MAP = 127,
    ESPADMIN_AVP_FW_BIN_SIZE = 128,
    ESPADMIN_AVP_FW_RELEASE_DATE = 129,
    ESPADMIN_AVP_FW_DIGEST = 130,
    // OTA Update
    ESPADMIN_AVP_FW_INIT_DIGEST = 131,
    ESPADMIN_AVP_FW_INFO = 132,
    ESPADMIN_AVP_FW_USER_DATA_ADDR = 133,
    ESPADMIN_AVP_FW_USER_DATA_SIZE = 134,
    // IMDB
    ESPADMIN_AVP_IMDB_BLOCK_SIZE = 135,
    ESPADMIN_AVP_IMDB_CLASS = 136,
    ESPADMIN_AVP_IMDB_CLASS_NAME = 137,
    ESPADMIN_AVP_IMDB_OBJECT_COUNT = 138,
    ESPADMIN_AVP_IMDB_PAGE_COUNT = 139,
    ESPADMIN_AVP_IMDB_BLOCK_COUNT = 140,
    ESPADMIN_AVP_IMDB_FREE_SLOTS = 141,
    ESPADMIN_AVP_IMDB_FREE_SIZE = 142,
    ESPADMIN_AVP_IMDB_MEM_USED = 143,
    // Wireless
    ESPADMIN_AVP_WIFI_OPMODE = 145,
    ESPADMIN_AVP_WIFI_SSID = 146,
    ESPADMIN_AVP_WIFI_PASSWORD = 147,
    ESPADMIN_AVP_WIFI_AUTH_MODE = 148,
    ESPADMIN_AVP_WIFI_SLEEP_TYPE = 149,
    ESPADMIN_AVP_WIFI_AUTO_CONNECT = 150,
    ESPADMIN_AVP_WIFI_CONNECT_STATUS = 151,
    ESPADMIN_AVP_WIFI_SOFTAP_TIMEOUT = 152,
    // OTA
    ESPADMIN_AVP_OTA_STATE = 165,
    ESPADMIN_AVP_OTA_BIN_DATA = 166,
    ESPADMIN_AVP_OTA_CURRENT_ADDR = 167,
    // 
    ESPADMIN_AVP_FW_BIN_DATE = 180,
    //
    ESPADMIN_AVP_FDB_INFO = 190,
    ESPADMIN_AVP_FDB_DATA_ADDR = 191,
    ESPADMIN_AVP_FDB_DATA_SIZE = 192,
    ESPADMIN_AVP_FDB_FILE_SIZE = 193,
    ESPADMIN_AVP_FDB_FILE_HWM = 194,
} espadmin_avp_code_t;

// used by services
svcs_errcode_t  espadmin_service_install (bool enabled);
svcs_errcode_t  espadmin_service_uninstall (void);
svcs_errcode_t  espadmin_on_start (const svcs_resource_t * svcres, dtlv_ctx_t * conf);
svcs_errcode_t  espadmin_on_stop (void);
svcs_errcode_t  espadmin_on_message (service_ident_t orig_id, service_msgtype_t msgtype, void *ctxdata,
                                     dtlv_ctx_t * msg_in, dtlv_ctx_t * msg_out);
svcs_errcode_t  espadmin_on_cfgupd (dtlv_ctx_t * conf);


#endif
