/* 
 * ESP8266 cron-like scheduler
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

#ifndef __SCHED_H__
#define __SCHED_H__

#include "sysinit.h"
#include "core/utils.h"
#include "system/services.h"
#include "proto/dtlv.h"
#include "service/lsh.h"

#define SCHED_SERVICE_ID		8
#define SCHED_SERVICE_NAME		"sched"

#define SCHEDULE_MINUTE_PARTS		4
#define SCHEDULE_MINUTE_PART_SECS	(SEC_PER_MIN/SCHEDULE_MINUTE_PARTS)
#define SCHEDULER_SZENTRY_MAX_LEN	80
#define SCHEDULER_ENTRY_NAME_LEN	30

typedef enum sched_errcode_e {
    SCHED_ERR_SUCCESS = 0,
    SCHED_INTERNAL_ERROR = 1,
    SCHED_ALLOCATION_ERROR = 2,
    SCHED_PARSE_ERROR = 3,
    SCHED_ENTRY_EXISTS = 4,
    SCHED_ENTRY_NOTEXISTS = 5,
    SCHED_STMT_NOTEXISTS = 6,
    SCHED_STMT_ERROR = 7,
    SCHED_ENTRY_SRC_NOTEXISTS = 8,
} sched_errcode_t;

typedef enum sched_entry_state_e {
    SCHED_ENTRY_STATE_NONE = 0,
    SCHED_ENTRY_STATE_RUNNING = 1,
    SCHED_ENTRY_STATE_QUEUE = 2,
    SCHED_ENTRY_STATE_FAILED = 3,
} sched_entry_state_t;

typedef enum sched_msgtype_e {
    SCHED_MSGTYPE_ENTRY_ADD = 10,
    SCHED_MSGTYPE_ENTRY_REMOVE = 11,
    SCHED_MSGTYPE_ENTRY_RUN = 12,
    SCHED_MSGTYPE_ENTRY_SOURCE = 13,
} sched_msgtype_t;

typedef enum sched_avp_code_e {
    SCHED_AVP_ENTRY = 100,
    SCHED_AVP_ENTRY_NAME = 101,
    SCHED_AVP_ENTRY_STATE = 102,
    SCHED_AVP_SCHEDULE_STRING = 103,
    SCHED_AVP_STMT_NAME = 104,
    SCHED_AVP_STMT_ARGUMENTS = 105,
    SCHED_AVP_LAST_RUN_TIME = 106,
    SCHED_AVP_NEXT_RUN_TIME = 107,
    SCHED_AVP_RUN_COUNT = 108,
    SCHED_AVP_FAIL_COUNT = 109,
    SCHED_AVP_PERSISTENT = 110,
    SCHED_AVP_ENTRY_SOURCE = 111,
} sched_avp_code_t;

typedef struct tsentry_s {
    uint8           flag_boot : 1;
    uint8           flag_network : 1;
    uint8           signal_id : 6;
    uint8           minpart[d_bitbuf_size (SCHEDULE_MINUTE_PARTS)];
    uint8           minute[d_bitbuf_size (MIN_PER_HOUR)];
    uint8           hour[d_bitbuf_size (HOUR_PER_DAY)];
    uint8           dow[d_bitbuf_size (DAY_PER_WEEK)];
    uint8           dom[d_bitbuf_size (DAY_PER_MONTH)];
} tsentry_t;

typedef char    entry_name_t[SCHEDULER_ENTRY_NAME_LEN];

typedef struct sched_entry_s {
    entry_name_t    entry_name;
    tsentry_t       ts;
    sh_stmt_name_t  stmt_name;
    os_time_t       last_ctime;
    os_time_t       next_ctime;
    uint16          run_count;
    uint16          fail_count;
    sched_entry_state_t state;
    size_t          varlen;
    ALIGN_DATA char vardata[];
} sched_entry_t;

typedef struct sched_entry_source_s {
    entry_name_t    name;
    lt_time_t       utime;
    size_t          varlen;
    ALIGN_DATA char vardata[];
} sched_entry_source_t;

sched_errcode_t sched_entry_get (const char * entry_name, sched_entry_t ** entry);
sched_errcode_t sched_entry_src_get (const char * entry_name, sched_entry_source_t ** entry_src);

sched_errcode_t sched_entry_run (const char * entry_name);
sched_errcode_t sched_entry_add (const char * entry_name, bool persistent, const char * sztsentry, const char * stmt_name, const char * vardata, size_t varlen);
sched_errcode_t sched_entry_remove (const char * entry_name);

// used by services
svcs_errcode_t  sched_service_install ();
svcs_errcode_t  sched_service_uninstall ();
svcs_errcode_t  sched_on_start (const svcs_resource_t * svcres, dtlv_ctx_t * conf);
svcs_errcode_t  sched_on_stop ();
svcs_errcode_t  sched_on_cfgupd (dtlv_ctx_t * conf);

svcs_errcode_t  sched_on_message (service_ident_t orig_id,
				  service_msgtype_t msgtype, void *ctxdata, dtlv_ctx_t * msg_in, dtlv_ctx_t * msg_out);


#define d_sched_check_imdb_error(ret) \
	{ \
		imdb_errcode_t r = (ret); \
		switch (r) { \
			case IMDB_ERR_SUCCESS: \
			case IMDB_CURSOR_BREAK: \
				break; \
			case IMDB_NOMEM: \
			case IMDB_ALLOC_PAGES_MAX: \
				return SCHED_ALLOCATION_ERROR; \
			default: \
				return SCHED_INTERNAL_ERROR; \
		} \
	}


#define d_sched_check_dtlv_error(ret) \
	if ((ret) != DTLV_ERR_SUCCESS) return SCHED_INTERNAL_ERROR;

#endif
