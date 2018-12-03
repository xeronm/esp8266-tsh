/* 
 * ESP8266 System Logging Service
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

#ifndef _SYSLOG_H_
#define _SYSLOG_H_ 1

#include "sysinit.h"
#include "core/logging.h"
#include "system/services.h"
#include "proto/dtlv.h"

#define SYSLOG_SERVICE_ID		2
#define SYSLOG_SERVICE_NAME		"syslog"

#define SYSLOG_DEFAULT_SEVERITY		LOG_INFO

typedef struct syslog_logrec_s {
    uint16          rec_no;
    os_time_t       rec_ctime;
    log_severity_t  severity;
    service_name_t  service;
    char            vardata[];
} syslog_logrec_t;

typedef enum PACKED syslog_msgtype_e {
    SYSLOG_MSGTYPE_WRITE = 10,
    SYSLOG_MSGTYPE_QUERY = 11,
    SYSLOG_MSGTYPE_PURGE = 12,
} syslog_msgtype_t;

typedef enum PACKED syslog_avp_code_e {
    SYSLOG_AVP_LOG_ENTRY = 101,
    SYSLOG_AVP_LOG_SEVERITY = 102,
    SYSLOG_AVP_LOG_MESSAGE = 103,
    SYSLOG_AVP_LOG_TIMESTAMP = 104,
    SYSLOG_AVP_LOG_RECNO = 105,
    SYSLOG_AVP_LOG_SERVICE = 106,
} syslog_avp_code_t;

// used by services
svcs_errcode_t  syslog_service_install (void);
svcs_errcode_t  syslog_service_uninstall (void);
svcs_errcode_t  syslog_on_start (const svcs_resource_t * svcres, dtlv_ctx_t * conf);
svcs_errcode_t  syslog_on_stop (void);
svcs_errcode_t  syslog_on_message (service_ident_t orig_id, service_msgtype_t msgtype, void *ctxdata,
				   dtlv_ctx_t * msg_in, dtlv_ctx_t * msg_out);

bool            syslog_available (void);
svcs_errcode_t  syslog_query(imdb_hndlr_t* hcur);
svcs_errcode_t  syslog_write (const log_severity_t severity, const char *svc, size_t * length, char **buf);
svcs_errcode_t  syslog_write_msg (const log_severity_t severity, const char *svc, char *msg);
svcs_errcode_t  syslog_vprintf (const log_severity_t severity, const char *svc, const char *fmt, va_list al);
svcs_errcode_t  syslog_vbprintf (const log_severity_t severity, const char *svc, const char *buf, size_t len,
				 const char *fmt, va_list al);


#endif /* _SYSLOG_H_ */
