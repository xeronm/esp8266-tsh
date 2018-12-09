/* 
 * ESP8266 Network Time Protocol Client Service
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

#ifndef __NTP_H__
#define __NTP_H__	1

#include "sysinit.h"
#include "core/utils.h"
#include "system/services.h"

#define NTP_SERVICE_ID		6
#define NTP_SERVICE_NAME	"ntp"

#define NTP_PORT		123	/* Remote NTP port */

#define NTP_DEFAULT_LOCAL_PORT		0
#define NTP_DEFAULT_POLL_TIMEOUT_MIN	15
#define NTP_DEFAULT_TIME_ZONE		0
#define NTP_DEFAULT_SERVER_0		"0.pool.ntp.org"
#define NTP_DEFAULT_SERVER_1		"1.pool.ntp.org"

#define NTP_ADJUST_MIN_MSEC	20
#define NTP_REQ_TIMEOUT_SEC	5
#define NTP_REQ_COUNT		5
#define NTP_ANS_COUNT_MIN	3

#define NTP_MAX_PEERS		2
#define NTP_SERVER_MAX_LEN	24

typedef enum ntp_msgtype_e {
    NTP_MSGTYPE_SETDATE = 10,
} ntp_msgtype_t;

typedef enum ntp_avp_code_e {
    NTP_AVP_QUERY_STATE = 101,
    NTP_AVP_QUERY_STATE_TIME = 102,
    NTP_AVP_POLL_INTERVAL = 103,
    NTP_AVP_PEER = 104,
    NTP_AVP_PEER_STATE = 105,
    NTP_AVP_PEER_RTT_MEAN = 106,
    NTP_AVP_PEER_RTT_VARIANCE = 107,
    NTP_AVP_PEER_OFFSET = 108,
} ntp_avp_code_t;

typedef enum ntp_tx_state_e {
    NTP_TX_STATE_NONE = 0,
    NTP_TX_STATE_IDLE = 1,
    NTP_TX_STATE_PENDING = 2,
    NTP_TX_STATE_FAILED = 3,
} ntp_tx_state_t;

typedef enum ntp_peer_state_e {
    NTP_PEER_STATE_NONE = 0,
    NTP_PEER_STATE_DNS = 1,
    NTP_PEER_STATE_DNSANS = 2,
    NTP_PEER_STATE_REQ = 3,
    NTP_PEER_STATE_SUCCESS = 4,
    NTP_PEER_STATE_TIMEOUT = 5,
    NTP_PEER_STATE_ERROR = 6
} ntp_peer_state_t;

typedef char    ntp_hostname_t[NTP_SERVER_MAX_LEN];

typedef struct ntp_timestamp_s {
    uint32          seconds;
    uint32          fraction;
} ntp_timestamp_t;

typedef void    (*ntp_query_cb_func_t) (const ntp_timestamp_t * local_ts, const ntp_timestamp_t * peer_ts);

bool            ntp_query (ntp_query_cb_func_t cb);
bool            ntp_query_break (void);
bool            ntp_set_date (void);

// used by services
svcs_errcode_t  ntp_service_install ();
svcs_errcode_t  ntp_service_uninstall ();
svcs_errcode_t  ntp_on_start (const svcs_resource_t * svcres, dtlv_ctx_t * conf);
svcs_errcode_t  ntp_on_stop ();
svcs_errcode_t  ntp_on_cfgupd (dtlv_ctx_t * conf);

svcs_errcode_t  ntp_on_message (service_ident_t orig_id,
				service_msgtype_t msgtype, void *ctxdata, dtlv_ctx_t * msg_in, dtlv_ctx_t * msg_out);


#endif
