/* Copyright (c) 2018 by Denis Muratov <xeronm@gmail.com>. All rights reserved

   FileName: services.h
   Source: https://dtec.pro/gitbucket/git/esp8266/esp8266_lsh.git

   Description: Services catalog and control
     Provides: 
	- common API for services
	- Storage for Configuration Data
	- Handling messagings between services

*/

#ifndef SERVICES_H_
#define SERVICES_H_ 1

#include "sysinit.h"
#include "core/ltime.h"
#include "system/imdb.h"
#include "proto/dtlv.h"

#define SERVICE_SERVICE_ID		1

typedef enum PACKED svcs_errcode_e {
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

typedef enum PACKED svcs_state_e {
    SVCS_STATE_STOPPED = 0,
    SVCS_STATE_RUNNING = 1,
    SVCS_STATE_FAILED = 2,
    SVCS_STATE_STOPING = 3,
    SVCS_STATE_STARTING = 4,
} svcs_state_t;

typedef enum PACKED svcs_msgtype_e {
    SVCS_MSGTYPE_INFO = 1,
    SVCS_MSGTYPE_CONTROL = 2,
    SVCS_MSGTYPE_CONFIG_GET = 3,
    SVCS_MSGTYPE_CONFIG_SET = 4,
    SVCS_MSGTYPE_NETWORK = 5,
    SVCS_MSGTYPE_ADJTIME = 6,
    SVCS_MSGTYPE_SYSTEM_START = 7,
    SVCS_MSGTYPE_SYSTEM_STOP = 8,
} svcs_msgtype_t;

typedef enum PACKED svcs_avp_code_e {
    SVCS_AVP_SERVICE = 100,
    SVCS_AVP_SERVICE_ID = 101,
    SVCS_AVP_SERVICE_ENABLED = 103,
    SVCS_AVP_SERVICE_STATE = 104,
} svcs_avp_code_t;

#define SERVICE_NAME_LEN		16	//
typedef char    service_name_t[SERVICE_NAME_LEN];

typedef uint16  service_ident_t;
typedef uint16  service_msgtype_t;

// handlers functions
typedef         svcs_errcode_t (*svcs_on_start_t) (imdb_hndlr_t himdb, imdb_hndlr_t hdata, dtlv_ctx_t * conf);
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
    bool            enabled;
    svcs_on_start_t on_start;
    svcs_on_stop_t  on_stop;
    svcs_on_message_t on_message;
    svcs_on_cfgupd_t on_cfgupd;
    obj_size_t      varsize;
    char           *vardata;
} svcs_service_def_t;

typedef struct svcs_service_info_s {
    service_ident_t service_id;
    service_name_t  name;
    bool            enabled:1;
    svcs_state_t    state:3;
    svcs_errcode_t  errcode:4;
    os_time_t       state_time;
} svcs_service_info_t;

svcs_errcode_t  svcctl_start (imdb_hndlr_t hmdb);
svcs_errcode_t  svcctl_stop ();
svcs_errcode_t  svcctl_info (uint8 * info_count, svcs_service_info_t * info_array, uint8 array_len);

// Service functions
svcs_errcode_t  svcctl_service_install (service_ident_t service_id, char *name, svcs_service_def_t * sdef);
svcs_errcode_t  svcctl_service_uninstall (char *name);
svcs_errcode_t  svcctl_service_start (service_ident_t service_id, char *name);
svcs_errcode_t  svcctl_service_stop (service_ident_t service_id, char *name);

svcs_errcode_t  svcctl_service_conf_get (service_ident_t service_id, dtlv_ctx_t * conf);
svcs_errcode_t  svcctl_service_conf_set (service_ident_t service_id, dtlv_ctx_t * conf);

svcs_errcode_t  svcctl_service_message (service_ident_t orig_id,
					service_ident_t dest_id,
					void *ctxdata,
					service_msgtype_t msgtype, dtlv_ctx_t * msg_in, dtlv_ctx_t * msg_out);

svcs_errcode_t  encode_service_result_ext (dtlv_ctx_t * msg_out, uint8 ext_code);


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
