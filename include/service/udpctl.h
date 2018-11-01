/* Copyright (c) 2018 by Denis Muratov <xeronm@gmail.com>. All rights reserved
 *
 *  FileName: udpctl.c
 *  Source: https://dtec.pro/gitbucket/git/esp8266/esp8266_lsh.git
 *
 *  Description: UDP Cotrol Protocol Support
 *
 *   Message Flow:
 *	Auth Request #0:
 *		Auth0 := hmac(Random)
 *		H0 := hmac( Header0, 0, Auth0, Body0 )
 *		M0 := (Header0, H0, Auth0, Body0)
 *	Auth Answer #1:
 *		Auth1 := hmac(Random)
 *		H1 = hmac( Header1, H0, Auth1, Body1 )
 *		M1 = (Header1, H1, Auth1, Body1)
 *	Control Request #2:
 *		H2 = hmac( Header2, H1, Body2 )
 *		M2 = (Header2, H2, Body1)
 *	Control Answer #3:
 *		H2 = hmac( Header2, H1, Body2 )
 *		M2 = (Header2, H2, Body1)
 *	...
 *
 */

/*

	 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|          Service-Id           |             Length            |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|R S E x x x x x|    Cmd Code   |          Identifier           |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                    Message Digest (256 bits)                  |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                              ...                              |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                      Message Digest (end)                     |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                     Authenticator (256 bits)                  |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                              ...                              |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                       Authenticator (end)                     |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                              ...                              |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

*/


#ifndef __UDPCTL_H__
#define __UDPCTL_H__	1

#include "sysinit.h"
#include "core/utils.h"
#include "system/services.h"
#include "proto/dtlv.h"

#define UDPCTL_DEFAULT_PORT		3901
#define UDPCTL_DEFAULT_IDLE_TX		60
#define UDPCTL_DEFAULT_RECYCLE_TX	60
#define UDPCTL_DEFAULT_AUTH_TX		10
#undef UDPCTL_DEFAULT_SECRET

#define UDPCTL_SERVICE_ID	4
#define UDPCTL_SERVICE_NAME	"udpctl"
#define UDPCTL_SECRET_LEN	32	//

#define UDPCTL_PROTOCOL_VERSION		0x0100

typedef digest256_t udpctl_digest_t;
typedef unsigned short packet_size_t;

typedef uint8   udpctl_secret_t[UDPCTL_SECRET_LEN];

#define PACKET_FLAG_REQUEST	(1 << 7)
#define PACKET_FLAG_SECURED	(1 << 6)
#define PACKET_FLAG_ERROR	(1 << 5)

typedef struct udpctl_packet_s {
    // 1-st 4 bytes
    service_ident_t service_id;
    packet_size_t   length;
    // 2-nd 4 bytes
    uint8           flags;
    uint8           code;
    uint16          identifier;
} udpctl_packet_t;

typedef struct udpctl_packet_sec_s {
    udpctl_packet_t base;
    udpctl_digest_t digest;	// 32 bytes
} udpctl_packet_sec_t;

typedef struct udpctl_packet_auth_s {
    udpctl_packet_sec_t base_sec;
    udpctl_digest_t auth;	// 32 bytes
} udpctl_packet_auth_t;

typedef enum PACKED udpctl_clnt_state_e {
    UCTL_CLNT_STATE_NONE = 0,
    UCTL_CLNT_STATE_FAIL,
    UCTL_CLNT_STATE_TIMEOUT,
    UCTL_CLNT_STATE_AUTH,
    UCTL_CLNT_STATE_OPEN,
} udpctl_clnt_state_t;

typedef enum PACKED udpctl_errcode_e {
    UDPCTL_ERR_SUCCESS = 0,
    UDPCTL_INTERNAL_ERROR = 1,
    UDPCTL_SERVER_SECURED = 2,
    UDPCTL_SERVER_NOTSECURED = 3,
    UDPCTL_CLIENTS_LIMIT_EXCEEDED = 4,
    UDPCTL_CLIENT_NOT_EXISTS = 5,
    UDPCTL_CLIENT_NOAUTH = 6,
    UDPCTL_INVALID_DIGEST = 7,
    UDPCTL_INVALID_LENGTH = 8,
    UDPCTL_INVALID_COMMAND = 9,
    UDPCTL_INVALID_FLAGS = 10,
    UDPCTL_UNSUPPORTED_COMMAND = 11,
    UDPCTL_DECODING_ERROR = 12,
} udpctl_errcode_t;

typedef enum PACKED udpctl_cmd_code_e {
    UCTL_CMD_CODE_AUTH = 1,
    UCTL_CMD_CODE_TERMINATE = 2,
    UCTL_CMD_CODE_SRVMSG = 3,
} udpctl_cmd_code_t;

typedef enum PACKED udpctl_avp_code_e {
    UDPCTL_AVP_PROTOCOL = 100,
    UDPCTL_AVP_IDLE_TIMEOUT = 102,
    UDPCTL_AVP_AUTH_TIMEOUT = 103,
    UDPCTL_AVP_RECYCLE_TIMEOUT = 104,
    UDPCTL_AVP_SECRET = 105,
    UDPCTL_AVP_CLIENTS_LIMIT = 106,
    UDPCTL_AVP_CLIENT = 107,
    UDPCTL_AVP_CLIENT_STATE = 108,
    UDPCTL_AVP_CLIENT_FIRST_TIME = 109,
    UDPCTL_AVP_CLIENT_LAST_TIME = 110,
} udpctl_avp_code_t;

typedef enum PACKED udpctl_result_code_e {
    RESULT_CODE_SUCCESS = 1,
    RESULT_CODE_COMMAND_ERROR = 2,
    RESULT_CODE_SERVICE_ERROR = 3,
    RESULT_CODE_PROTOCOL_ERROR = 4,
    RESULT_CODE_INTERNAL_ERROR = 5,
} udpctl_result_code_t;

typedef struct udpctl_client_s {
    udpctl_clnt_state_t state;
    ip_port_t       remote_port;
    ipv4_addr_t     remote_ip;
    udpctl_digest_t auth;
    os_time_t       first_time;
    os_time_t       last_time;
} udpctl_client_t;

// used by services
svcs_errcode_t  udpctl_service_install ();
svcs_errcode_t  udpctl_service_uninstall ();
svcs_errcode_t  udpctl_on_start (imdb_hndlr_t himdb, imdb_hndlr_t hdata, dtlv_ctx_t * conf);
svcs_errcode_t  udpctl_on_stop ();
svcs_errcode_t  udpctl_on_cfgupd (dtlv_ctx_t * conf);

svcs_errcode_t  udpctl_on_message (service_ident_t orig_id,
				   service_msgtype_t msgtype, void *ctxdata, dtlv_ctx_t * msg_in, dtlv_ctx_t * msg_out);


udpctl_errcode_t udpctl_sync_request (ipv4_addr_t * addr, ip_port_t * port,
				      char *data_in, packet_size_t length_in, char *data_out,
				      packet_size_t * length_out);

#define d_udpctl_check_udpctl_error(ret) \
	{ \
		udpctl_errcode_t r = (ret); \
		if (r != UDPCTL_ERR_SUCCESS) return r; \
	}

#define d_udpctl_check_dtlv_error(ret) \
	if ((ret) != DTLV_ERR_SUCCESS) return UDPCTL_INTERNAL_ERROR;

#endif
