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


#ifndef SERVICES_H_
#define SERVICES_H_ 1

#include "sysinit.h"
#include "core/ltime.h"
#include "system/imdb.h"
#include "proto/dtlv.h"

#define SERVICE_SERVICE_ID		1

typedef enum svcs_cfgtype_e {
    SVCS_CFGTYPE_CURRENT = 0,
    SVCS_CFGTYPE_NEW = 1,
    SVCS_CFGTYPE_BACKUP = 2,
} svcs_cfgtype_t;

typedef enum svcs_errcode_e {
    SVCS_ERR_SUCCESS = 0,
    SVCS_INTERNAL_ERROR = 1,
    SVCS_SERVICE_ERROR = 2,
    SVCS_NOT_RUN = 3,
    SVCS_ALREADY_RUN = 4,
    SVCS_ALREADY_EXISTS = 5,
    SVCS_NOT_EXISTS = 6,
    SVCS_MSGTYPE_INVALID = 7,
    SVCS_INVALID_MESSAGE = 8,
} svcs_errcode_t;

typedef enum svcs_state_e {
    SVCS_STATE_STOPPED = 0,
    SVCS_STATE_RUNNING = 1,
    SVCS_STATE_FAILED = 2,
    SVCS_STATE_STOPING = 3,
    SVCS_STATE_STARTING = 4,
} svcs_state_t;

typedef enum svcs_msgtype_e {
    SVCS_MSGTYPE_INFO = 1,
    SVCS_MSGTYPE_CONTROL = 2,
    SVCS_MSGTYPE_CONFIG_GET = 3,
    SVCS_MSGTYPE_CONFIG_SET = 4,
    SVCS_MSGTYPE_CONFIG_SAVE = 5,
    // Multicast Messages
    SVCS_MSGTYPE_MULTICAST_MIN = 32,
    SVCS_MSGTYPE_SYSTEM_START = 32,
    SVCS_MSGTYPE_SYSTEM_STOP = 33,
    SVCS_MSGTYPE_NETWORK = 34,
    SVCS_MSGTYPE_NETWORK_LOSS = 35,
    SVCS_MSGTYPE_ADJTIME = 36,
    SVCS_MSGTYPE_MCAST_SIG1 = 37,
    SVCS_MSGTYPE_MCAST_SIG2 = 38,
    SVCS_MSGTYPE_MCAST_SIG3 = 39,
    SVCS_MSGTYPE_MCAST_SIG4 = 40,
    SVCS_MSGTYPE_MULTICAST_MAX = 63,
} svcs_msgtype_t;

typedef enum svcs_avp_code_e {
    SVCS_AVP_SERVICE = 100,
    SVCS_AVP_SERVICE_ID = 101,
    SVCS_AVP_SERVICE_ENABLED = 103,
    SVCS_AVP_SERVICE_STATE = 104,
} svcs_avp_code_t;

typedef struct svcs_resource_s {
    imdb_hndlr_t    hmdb;
    imdb_hndlr_t    hfdb;
    imdb_hndlr_t    hdata;
} svcs_resource_t;

#define SERVICE_NAME_LEN		16      //
typedef char    service_name_t[SERVICE_NAME_LEN];

typedef uint16  service_ident_t;
typedef uint16  service_msgtype_t;

// handlers functions
typedef         svcs_errcode_t (*svcs_on_start_t) (const svcs_resource_t * svcres, dtlv_ctx_t * conf);
typedef         svcs_errcode_t (*svcs_on_stop_t) ();
typedef         svcs_errcode_t (*svcs_on_cfgupd_t) (dtlv_ctx_t * conf);
typedef         svcs_errcode_t (*svcs_on_message_t) (service_ident_t orig_id,
                                                     service_msgtype_t msgtype,
                                                     void *ctxdata, dtlv_ctx_t * msg_in, dtlv_ctx_t * msg_out);

/*
Service Definition
  - fautorun: autorun service with system startup
  - on_start: service start handler
  - on_stop: service stop handler
  - on_message: incoming message handler
  - on_cfgupd: configuration update handler
  - varsize: variable data size
  - vardata: variable data pointer
*/
typedef struct svcs_service_def_s {
    bool            enabled: 1;
    bool            multicast: 1;
    svcs_on_start_t on_start;
    svcs_on_stop_t  on_stop;
    svcs_on_message_t on_message;
    svcs_on_cfgupd_t on_cfgupd;
    obj_size_t      varsize;
    char           *vardata;
} svcs_service_def_t;

typedef struct svcs_service_info_s {
    service_name_t  name;
    service_ident_t service_id;
    bool            enabled:1;
    svcs_state_t    state:3;
    svcs_errcode_t  errcode:4;
    os_time_t       state_time;
} svcs_service_info_t;

svcs_errcode_t  svcctl_start (imdb_hndlr_t hmdb, imdb_hndlr_t hfdb);
svcs_errcode_t  svcctl_stop ();
svcs_errcode_t  svcctl_info (uint8 * info_count, svcs_service_info_t * info_array, uint8 array_len);

// Service functions
svcs_errcode_t  svcctl_service_install (service_ident_t service_id, const char *name, svcs_service_def_t * sdef);
svcs_errcode_t  svcctl_service_uninstall (const char *name);
svcs_errcode_t  svcctl_service_start (service_ident_t service_id, const char *name);
svcs_errcode_t  svcctl_service_stop (service_ident_t service_id, const char *name);

svcs_errcode_t  svcctl_service_conf_get (service_ident_t service_id, dtlv_ctx_t * conf, svcs_cfgtype_t cfgtype);
svcs_errcode_t  svcctl_service_conf_set (service_ident_t service_id, dtlv_ctx_t * conf);
svcs_errcode_t  svcctl_service_conf_save (service_ident_t service_id);

svcs_errcode_t  svcctl_service_message (service_ident_t orig_id,
                                        service_ident_t dest_id,
                                        void *ctxdata,
                                        service_msgtype_t msgtype, dtlv_ctx_t * msg_in, dtlv_ctx_t * msg_out);

svcs_errcode_t  encode_service_result_ext (dtlv_ctx_t * msg_out, uint8 ext_code, const char *errmsg);


#define d_svcs_check_svcs_error(ret) \
	{ \
		svcs_errcode_t r = (ret); \
		if (r != SVCS_ERR_SUCCESS) return r; \
	}

#define d_svcs_check_imdb_error(ret) \
	{ \
		imdb_errcode_t r = (ret); \
		if ((r != IMDB_ERR_SUCCESS) && (r != IMDB_CURSOR_BREAK) && (r != IMDB_CURSOR_NO_DATA_FOUND)) return SVCS_INTERNAL_ERROR; \
	}

#define d_svcs_check_dtlv_error(ret) \
	if ((ret) != DTLV_ERR_SUCCESS) return SVCS_INTERNAL_ERROR;

#endif
