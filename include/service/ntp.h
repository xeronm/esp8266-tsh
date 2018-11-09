/* Copyright (c) 2018 by Denis Muratov <xeronm@gmail.com>. All rights reserved

   FileName: ntp.h
   Source: https://dtec.pro/gitbucket/git/esp8266/esp8266_lsh.git

   Description: Network Time Protocol Service

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
#define NTP_DEFAULT_POLL_TIMEOUT_MIN	20
#define NTP_DEFAULT_TIME_ZONE		0
#define NTP_DEFAULT_SERVER_0		"0.pool.ntp.org"
#define NTP_DEFAULT_SERVER_1		"1.pool.ntp.org"

#define NTP_ADJUST_MIN_MSEC	50
#define NTP_REQ_TIMEOUT_SEC	10
#define NTP_REQ_COUNT		5

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
    NTP_PEER_STATE_IDLE = 1,
    NTP_PEER_STATE_DNS = 2,
    NTP_PEER_STATE_DNSANS = 3,
    NTP_PEER_STATE_REQ = 4,
    NTP_PEER_STATE_SUCCESS = 5,
    NTP_PEER_STATE_TIMEOUT = 6,
    NTP_PEER_STATE_ERROR = 7
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
svcs_errcode_t  ntp_on_start (imdb_hndlr_t himdb, imdb_hndlr_t hdata, dtlv_ctx_t * conf);
svcs_errcode_t  ntp_on_stop ();
svcs_errcode_t  ntp_on_cfgupd (dtlv_ctx_t * conf);

svcs_errcode_t  ntp_on_message (service_ident_t orig_id,
				service_msgtype_t msgtype, void *ctxdata, dtlv_ctx_t * msg_in, dtlv_ctx_t * msg_out);


#endif
