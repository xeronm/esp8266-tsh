/* 
 * ESP8266 UDP Cotrol Protocol Service
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


/*
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

#include "sysinit.h"
#include "core/utils.h"
#include "core/logging.h"
#include "core/system.h"
#include "system/comavp.h"
#include "system/imdb.h"
#include "crypto/sha.h"
#include "proto/dtlv.h"
#include "service/udpctl.h"
#ifdef ARCH_XTENSA
#include "espconn.h"
#endif

#define UDPCTL_STORAGE_PAGES		1
#define UDPCTL_STORAGE_PAGE_BLOCKS	1
#define UDPCTL_MESSAGE_SIZE		1440    // should less than 1472, seems depends from MTU
#define UDPCTL_CLIENTS_MAX		4

/*
 * UDPCTL Configuration
 *  - port: listening port
 *  - secret: shared secret key
 *  - clients_limit: maximum authorized connections
 *  - auth_tx: authorization timer (in seconds)
 *  - idle_tx: idle timer (in seconds)
 *  - recycle_tx: connection timer (in seconds)
 *  - maddr: multicast remote addr TODO:
 */
typedef struct udpctl_conf_s {
    ip_port_t       port;
    udpctl_secret_t secret;
    uint8           secret_len;
    uint8           clients_limit;
    uint8           auth_tx;
    uint8           idle_tx;
    uint8           recycle_tx;
    ipv4_addr_t     maddr;
} udpctl_conf_t;

typedef struct udpctl_data_s {
    const svcs_resource_t *svcres;
    uint8           client_count;
    ip_conn_t       conn;
#ifdef ARCH_XTENSA
    esp_udp         proto_udp;
#endif
    udpctl_client_t clients[UDPCTL_CLIENTS_MAX];
    udpctl_conf_t   conf;
} udpctl_data_t;

LOCAL udpctl_data_t *sdata = NULL;

typedef struct udpctl_msgctx_s {
    ip_port_t       rport;
    ipv4_addr_t     raddr;
    udpctl_client_t *cli;
    os_time_t       curr_time;
    udpctl_packet_t *packet_out;
    udpctl_packet_t *packet_in;
    dtlv_ctx_t      encoder_out;
    dtlv_ctx_t      decoder_in;
    size_t          length_in;
    size_t          length_out;
} udpctl_msgctx_t;

LOCAL const char *sz_udpctl_error[] RODATA = {
    "",
    "internal error",
    "requires secured messages",
    "requires not secured messages",
    "clients limit exceeded",
    "client not exists",
    "client must authenticate first",
    "invalid message digest",
    "invalid packet length: %s - %u",
    "invalid protocol command: %s",
    "invalid message flags: %s",
    "unsupported command: %u",
    "dtlv decoding error: %u",
};


LOCAL bool      ICACHE_FLASH_ATTR
udpctl_cli_check_state (udpctl_client_t * cli, os_time_t curr_time)
{
    os_time_t       timeout;
    switch (cli->state) {
    case UCTL_CLNT_STATE_AUTH:
        timeout = sdata->conf.auth_tx;
        break;
    case UCTL_CLNT_STATE_OPEN:
        timeout = sdata->conf.idle_tx;
        break;
    case UCTL_CLNT_STATE_NONE:
        return false;
    default:
        timeout = sdata->conf.recycle_tx;
    }

    if (curr_time - cli->last_time > timeout) {
        if (cli->state == UCTL_CLNT_STATE_TIMEOUT) {
            cli->state = UCTL_CLNT_STATE_NONE;
            return false;
        }
        cli->state = UCTL_CLNT_STATE_TIMEOUT;
    }
    return true;
}

/*
 * [private] Get client slot for addr:ip
 *  - remote_port: Client Port
 *  - remote_ip: Client IP Address
 *  - cli: result pointer to udpctl_client_t slot
 *  - fupdate: update last time
 *  - curr_time: current time
 */
LOCAL udpctl_errcode_t ICACHE_FLASH_ATTR
udpctl_client_slot (ip_port_t remote_port, ipv4_addr_t * remote_ip, udpctl_client_t ** cli, bool reuse_flag,
                    os_time_t curr_time)
{
    int             i;
    *cli = NULL;
    udpctl_client_t *cli_empty = NULL;
    udpctl_client_t *cli_target = NULL;
    for (i = 0; i < MIN (UDPCTL_CLIENTS_MAX, sdata->conf.clients_limit); i++) {
        udpctl_client_t *cli0 = &sdata->clients[i];
        if (!udpctl_cli_check_state (cli0, curr_time)) {
            if ((!cli_empty) || (cli_empty->state != UCTL_CLNT_STATE_NONE))
                cli_empty = cli0;
            continue;
        }

        if ((cli0->remote_ip.addr == remote_ip->addr) && (cli0->remote_port == remote_port)) {
            cli_target = cli0;
        }
    }

    if (cli_target && reuse_flag) {
        cli_empty = cli_target;
        cli_target = NULL;
    }

    if (cli_target) {
        cli_target->last_time = curr_time;

        *cli = cli_target;
    }
    else if (cli_empty) {
        os_memset (cli_empty, 0, sizeof (udpctl_client_t));

        cli_empty->state = UCTL_CLNT_STATE_NONE;
        cli_empty->first_time = curr_time;
        cli_empty->last_time = curr_time;
        cli_empty->remote_ip.addr = remote_ip->addr;
        cli_empty->remote_port = remote_port;

        *cli = cli_empty;
    }
    else {
        return UDPCTL_CLIENTS_LIMIT_EXCEEDED;
    }
    d_assert (*cli, "client not NULL")

        return UDPCTL_ERR_SUCCESS;
}

LOCAL udpctl_errcode_t ICACHE_FLASH_ATTR
udpctl_packet_check_digest (udpctl_client_t * client, udpctl_packet_sec_t * packet, size_t length)
{
    udpctl_digest_t digest_in;
    udpctl_digest_t digest_comp;

    os_memcpy (digest_in, packet->digest, sizeof (udpctl_digest_t));
    os_memcpy (packet->digest, client->auth, sizeof (udpctl_digest_t));

    hmac (SHA256, (unsigned char *) packet, length, sdata->conf.secret, sdata->conf.secret_len, digest_comp);

    os_memcpy (packet->digest, digest_in, sizeof (udpctl_digest_t));

    // HMAC before compare
    hmac (SHA256, digest_comp, sizeof (udpctl_digest_t), sdata->conf.secret, sdata->conf.secret_len, digest_comp);
    hmac (SHA256, digest_in, sizeof (udpctl_digest_t), sdata->conf.secret, sdata->conf.secret_len, digest_in);

    if (os_strncmp ((char *) digest_comp, (char *) digest_in, sizeof (udpctl_digest_t)) == 0) {
        return UDPCTL_ERR_SUCCESS;
    }

    return UDPCTL_INVALID_DIGEST;
}

LOCAL udpctl_errcode_t ICACHE_FLASH_ATTR
udpctl_packet_answer_digest (udpctl_client_t * client, udpctl_packet_sec_t * packet, dtlv_size_t length,
                             udpctl_digest_t req_auth)
{
    if (packet->base.code == UCTL_CMD_CODE_AUTH) {
        // generate Initial Auth
        udpctl_packet_auth_t *auth_packet = d_pointer_as (udpctl_packet_auth_t, packet);

        udpctl_digest_t initial;
#ifdef ARCH_XTENSA
        os_random_buffer (initial, sizeof (udpctl_digest_t));
#endif
        hmac (SHA256, initial, sizeof (udpctl_digest_t), sdata->conf.secret, sdata->conf.secret_len, auth_packet->auth);
    }

    udpctl_digest_t digest_out;
    os_memcpy (packet->digest, req_auth, sizeof (udpctl_digest_t));

    hmac (SHA256, (unsigned char *) packet, length, sdata->conf.secret, sdata->conf.secret_len, digest_out);

    os_memcpy (packet->digest, digest_out, sizeof (udpctl_digest_t));

    if (client->state != UCTL_CLNT_STATE_FAIL)
        // store as auth for next sequenced message
        os_memcpy (client->auth, digest_out, sizeof (udpctl_digest_t));

    return UDPCTL_ERR_SUCCESS;
}

LOCAL size_t    ICACHE_FLASH_ATTR
udpctl_answer_result (udpctl_msgctx_t * msgctx)
{
    packet_size_t   len = msgctx->encoder_out.datalen;
    if (msgctx->packet_out->flags & PACKET_FLAG_SECURED) {
        len +=
            (msgctx->packet_out->code ==
             UCTL_CMD_CODE_AUTH ? sizeof (udpctl_packet_auth_t) : sizeof (udpctl_packet_sec_t));

        udpctl_packet_answer_digest (msgctx->cli,
                                     d_pointer_as (udpctl_packet_sec_t, msgctx->packet_out), len,
                                     d_pointer_as (udpctl_packet_sec_t, msgctx->packet_in)->digest);
    }
    else
        len += sizeof (udpctl_packet_t);

    msgctx->packet_out->length = htobe16 (len);

    return len;
}

/*
 * [private] Prepare ERR Response and send back
 *  - msgctx: message processing context
 *  - rescode: Result-Code AVP
 *  - errcode: Result-Ext-Code AVP
 *  - ...: error message formatting string arguments
 */
LOCAL udpctl_errcode_t ICACHE_FLASH_ATTR
udpctl_answer_err (udpctl_msgctx_t * msgctx, uint8 rescode, udpctl_errcode_t errcode, ...)
{
    msgctx->cli->state = UCTL_CLNT_STATE_FAIL;
    msgctx->packet_out->flags |= PACKET_FLAG_ERROR;

    char            errmsg[128];
    //os_memset(errmsg, 0, sizeof(errmsg));

    va_list         al;
    va_start (al, errcode);
    os_vsnprintf (errmsg, sizeof (errmsg), sz_udpctl_error[errcode], al);
    va_end (al);

    d_log_wprintf (UDPCTL_SERVICE_NAME, IPSTR ":%u %s", IP2STR (&msgctx->raddr), msgctx->rport, errmsg);

    dtlv_ctx_reset_encode (&msgctx->encoder_out);
    d_udpctl_check_dtlv_error (dtlv_avp_encode_uint32
                               (&msgctx->encoder_out, COMMON_AVP_EVENT_TIMESTAMP, lt_time (&msgctx->curr_time))
                               || dtlv_avp_encode_uint16 (&msgctx->encoder_out, UDPCTL_AVP_PROTOCOL,
                                                          UDPCTL_PROTOCOL_VERSION)
                               || dtlv_avp_encode_uint8 (&msgctx->encoder_out, COMMON_AVP_RESULT_CODE, rescode)
                               || dtlv_avp_encode_char (&msgctx->encoder_out, COMMON_AVP_RESULT_MESSAGE, errmsg));
    if (errcode > 0)
        d_udpctl_check_dtlv_error (dtlv_avp_encode_uint8 (&msgctx->encoder_out, COMMON_AVP_RESULT_EXT_CODE, errcode));

    return UDPCTL_ERR_SUCCESS;
}


/*
 * [private] Check authenticator and prepare decoder and encoder. If not successed - send ERR Response.
 *  - msgctx: message processing context
 *  - pusrdata: buffer with packet body
 *  - length: packet length
 *  - result: Success Flag (true/false)
 */
LOCAL udpctl_errcode_t ICACHE_FLASH_ATTR
udpctl_init_message_context (udpctl_msgctx_t * msgctx, char *data_in, char *data_out, size_t length_in,
                             size_t length_out)
{
    msgctx->packet_in = d_pointer_as (udpctl_packet_t, data_in);
    msgctx->packet_out = d_pointer_as (udpctl_packet_t, data_out);
    msgctx->length_in = length_in;
    msgctx->length_out = length_out;

    if (data_in == NULL || length_in < sizeof (udpctl_packet_t)) {
        d_udpctl_check_udpctl_error (udpctl_answer_err
                                     (msgctx, RESULT_CODE_PROTOCOL_ERROR, UDPCTL_INVALID_LENGTH, "minimum header",
                                      sizeof (udpctl_packet_t)));
        return UDPCTL_INVALID_LENGTH;
    }

    if (udpctl_client_slot (msgctx->rport, &msgctx->raddr, &msgctx->cli,
                            (msgctx->packet_in->code == UCTL_CMD_CODE_AUTH), msgctx->curr_time) != UDPCTL_ERR_SUCCESS) {
        d_udpctl_check_udpctl_error (udpctl_answer_err
                                     (msgctx, RESULT_CODE_PROTOCOL_ERROR, UDPCTL_CLIENTS_LIMIT_EXCEEDED));
        return UDPCTL_CLIENTS_LIMIT_EXCEEDED;
    }

    size_t          hdrlen;
    if (sdata->conf.secret_len == 0) {
        hdrlen = sizeof (udpctl_packet_t);
        os_memset (msgctx->packet_out, 0, hdrlen);
    }
    else {
        hdrlen =
            (msgctx->packet_in->code ==
             UCTL_CMD_CODE_AUTH ? sizeof (udpctl_packet_auth_t) : sizeof (udpctl_packet_sec_t));
        os_memset (msgctx->packet_out, 0, hdrlen);
        msgctx->packet_out->flags |= PACKET_FLAG_SECURED;
    }

    if (length_in < hdrlen) {
        d_udpctl_check_udpctl_error (udpctl_answer_err
                                     (msgctx, RESULT_CODE_PROTOCOL_ERROR, UDPCTL_INVALID_LENGTH, "less than header",
                                      hdrlen));
        return UDPCTL_INVALID_LENGTH;
    }

    dtlv_ctx_init_encode (&msgctx->encoder_out, d_pointer_add (char, msgctx->packet_out, hdrlen), length_out - hdrlen);

    msgctx->packet_out->service_id = msgctx->packet_in->service_id;
    msgctx->packet_out->code = msgctx->packet_in->code;
    msgctx->packet_out->identifier = msgctx->packet_in->identifier;

    if ((msgctx->packet_in->flags & PACKET_FLAG_REQUEST) == 0) {
        d_udpctl_check_udpctl_error (udpctl_answer_err
                                     (msgctx, RESULT_CODE_COMMAND_ERROR, UDPCTL_INVALID_FLAGS, "REQ flag absent"));
        return UDPCTL_INVALID_FLAGS;
    }

    dtlv_size_t     msglen = (dtlv_size_t) htobe16 (msgctx->packet_in->length);
    if (msglen != length_in) {
        d_udpctl_check_udpctl_error (udpctl_answer_err
                                     (msgctx, RESULT_CODE_PROTOCOL_ERROR, UDPCTL_INVALID_LENGTH, "message length",
                                      msglen));
        return UDPCTL_INVALID_LENGTH;
    }

    if (sdata->conf.secret_len == 0) {
        if (msgctx->packet_in->flags & PACKET_FLAG_SECURED) {
            d_udpctl_check_udpctl_error (udpctl_answer_err
                                         (msgctx, RESULT_CODE_PROTOCOL_ERROR, UDPCTL_SERVER_NOTSECURED));
            return UDPCTL_SERVER_NOTSECURED;
        }
    }
    else {
        if ((msgctx->packet_in->flags & PACKET_FLAG_SECURED) == 0) {
            d_udpctl_check_udpctl_error (udpctl_answer_err (msgctx, RESULT_CODE_PROTOCOL_ERROR, UDPCTL_SERVER_SECURED));
            return UDPCTL_SERVER_SECURED;
        }
        if (udpctl_packet_check_digest (msgctx->cli, d_pointer_as (udpctl_packet_sec_t, data_in), length_in) !=
            UDPCTL_ERR_SUCCESS) {
            d_udpctl_check_udpctl_error (udpctl_answer_err (msgctx, RESULT_CODE_PROTOCOL_ERROR, UDPCTL_INVALID_DIGEST));
            return UDPCTL_INVALID_DIGEST;
        }
    }

    dtlv_ctx_init_decode (&msgctx->decoder_in, d_pointer_add (char, msgctx->packet_in, hdrlen), length_in - hdrlen);

    return UDPCTL_ERR_SUCCESS;
}


#define d_udpctl_answer_dtlv_error(expr)	\
	{	\
		dtlv_errcode_t r2 = (expr);	\
		if (r2 != DTLV_ERR_SUCCESS) {	\
			d_udpctl_check_udpctl_error(	\
				udpctl_answer_err(&msgctx, RESULT_CODE_PROTOCOL_ERROR, UDPCTL_DECODING_ERROR, r2));	\
			goto answer_result;	\
		}	\
	}


udpctl_errcode_t ICACHE_FLASH_ATTR
udpctl_sync_request (ipv4_addr_t * addr, ip_port_t * port, char *data_in, packet_size_t length_in, char *data_out,
                     packet_size_t * length_out)
{
    if (!sdata) {
        d_log_eprintf (UDPCTL_SERVICE_NAME, "not started");
        return UDPCTL_INTERNAL_ERROR;
    }

    udpctl_errcode_t res = UDPCTL_ERR_SUCCESS;
    udpctl_msgctx_t msgctx;
    os_memset (&msgctx, 0, sizeof (udpctl_msgctx_t));
    os_memcpy (&msgctx.rport, port, sizeof (ip_port_t));
    os_memcpy (&msgctx.raddr, addr, sizeof (ipv4_addr_t));
    msgctx.curr_time = lt_ctime ();

    res = udpctl_init_message_context (&msgctx, data_in, data_out, length_in, *length_out);
    if (res != UDPCTL_ERR_SUCCESS)
        goto answer_result;

    service_ident_t serv_id = htobe16 (msgctx.packet_in->service_id);
    if (msgctx.packet_in->code != UCTL_CMD_CODE_AUTH) {
        if (msgctx.cli->state == UCTL_CLNT_STATE_AUTH)
            msgctx.cli->state = UCTL_CLNT_STATE_OPEN;
        else if (msgctx.cli->state != UCTL_CLNT_STATE_OPEN) {
            res = UDPCTL_CLIENT_NOAUTH;
            d_udpctl_check_udpctl_error (udpctl_answer_err (&msgctx, RESULT_CODE_PROTOCOL_ERROR, res));
            goto answer_result;
        }
    }

    d_udpctl_check_dtlv_error (dtlv_avp_encode_uint32
                               (&msgctx.encoder_out, COMMON_AVP_EVENT_TIMESTAMP, lt_time (&msgctx.curr_time)));

    switch (msgctx.packet_in->code) {
    case UCTL_CMD_CODE_SRVMSG:
        {
            res = UDPCTL_INVALID_COMMAND;

            dtlv_nscode_t   path[] = { {{serv_id, COMMON_AVP_SVC_MESSAGE}
                                        }
            , {{0, 0}
               }
            };

            dtlv_davp_t     avp_array[1];
            uint16          total_count;
            d_udpctl_answer_dtlv_error (dtlv_avp_decode_bypath
                                        (&msgctx.decoder_in, path, avp_array, 1, true, &total_count));
            if (total_count != 1) {
                d_udpctl_check_udpctl_error (udpctl_answer_err
                                             (&msgctx, RESULT_CODE_COMMAND_ERROR, res, "AVP Message is absent"));
                goto answer_result;
            }

            dtlv_ctx_t      dtlv_ctx;
            d_udpctl_answer_dtlv_error (dtlv_ctx_init_decode
                                        (&dtlv_ctx, avp_array[0].avp->data,
                                         d_avp_data_length (avp_array[0].havpd.length)));

            service_msgtype_t msgtype;
            {
                dtlv_davp_t     avp_msgtype;
                if ((dtlv_avp_decode (&dtlv_ctx, &avp_msgtype) != DTLV_ERR_SUCCESS) ||
                    (avp_msgtype.havpd.nscode.comp.code != COMMON_AVP_SVC_MESSAGE_TYPE)
                    ) {
                    d_udpctl_check_udpctl_error (udpctl_answer_err
                                                 (&msgctx, RESULT_CODE_COMMAND_ERROR, res,
                                                  "AVP Message-Type must be first"));
                    goto answer_result;
                }
                if (dtlv_avp_get_uint16 (&avp_msgtype, &msgtype) != DTLV_ERR_SUCCESS) {
                    d_udpctl_check_udpctl_error (udpctl_answer_err
                                                 (&msgctx, RESULT_CODE_COMMAND_ERROR, res,
                                                  "AVP Message-Type is invalid"));
                    goto answer_result;
                }
            }

            res = UDPCTL_ERR_SUCCESS;
            dtlv_avp_t     *gavp_srv;
            d_udpctl_check_dtlv_error (dtlv_avp_encode_grouping
                                       (&msgctx.encoder_out, serv_id, COMMON_AVP_SVC_MESSAGE, &gavp_srv));

            svcs_errcode_t  ret =
                svcctl_service_message (UDPCTL_SERVICE_ID, serv_id, &msgctx, msgtype, &dtlv_ctx, &msgctx.encoder_out);

            d_udpctl_check_dtlv_error (dtlv_avp_encode_group_done (&msgctx.encoder_out, gavp_srv));

            if (ret == SVCS_ERR_SUCCESS) {
                d_udpctl_check_dtlv_error (dtlv_avp_encode_uint8
                                           (&msgctx.encoder_out, COMMON_AVP_RESULT_CODE, RESULT_CODE_SUCCESS));
            }
            else {
                d_udpctl_check_dtlv_error (dtlv_avp_encode_uint8
                                           (&msgctx.encoder_out, COMMON_AVP_RESULT_CODE, RESULT_CODE_SERVICE_ERROR)
                                           || dtlv_avp_encode_uint8 (&msgctx.encoder_out, COMMON_AVP_RESULT_EXT_CODE,
                                                                     ret));
            }
        }
        break;
    case UCTL_CMD_CODE_AUTH:
        if ((serv_id == UDPCTL_SERVICE_ID) && (msgctx.cli->state == UCTL_CLNT_STATE_NONE)) {
            msgctx.cli->state = UCTL_CLNT_STATE_AUTH;
            d_udpctl_check_dtlv_error (dtlv_avp_encode_uint16
                                       (&msgctx.encoder_out, UDPCTL_AVP_PROTOCOL, UDPCTL_PROTOCOL_VERSION)
                                       || dtlv_avp_encode_uint8 (&msgctx.encoder_out, COMMON_AVP_RESULT_CODE,
                                                                 RESULT_CODE_SUCCESS)
                                       || dtlv_avp_encode_uint16 (&msgctx.encoder_out, UDPCTL_AVP_IDLE_TIMEOUT,
                                                                  sdata->conf.idle_tx));
            break;
        }
    default:
        d_udpctl_check_udpctl_error (udpctl_answer_err
                                     (&msgctx, RESULT_CODE_COMMAND_ERROR, UDPCTL_UNSUPPORTED_COMMAND,
                                      msgctx.packet_in->code));
        break;
    }

  answer_result:
    *length_out = udpctl_answer_result (&msgctx);
    return res;
}



LOCAL void      ICACHE_FLASH_ATTR
uctl_recv_cb (void *arg, char *pusrdata, unsigned short length)
{
#ifdef ARCH_XTENSA
    struct espconn *conn = d_pointer_as (struct espconn, arg);
    remot_info     *con_info;
    espconn_get_connection_info (conn, &con_info, 0);
    ipv4_addr_t     addr;
    ip_port_t       port = con_info->remote_port;
    os_memcpy (addr.bytes, con_info->remote_ip, sizeof (ipv4_addr_t));

    packet_size_t   length_out = UDPCTL_MESSAGE_SIZE;
    char            data_out[UDPCTL_MESSAGE_SIZE];

    udpctl_errcode_t res = udpctl_sync_request (&addr, &port, pusrdata, length, data_out, &length_out);
    if (res != UDPCTL_INTERNAL_ERROR) {
        d_log_dprintf (UDPCTL_SERVICE_NAME, IPSTR ":%u sent len:%u", IP2STR (&addr), port, length_out);

        os_memcpy (conn->proto.udp->remote_ip, addr.bytes, sizeof (ipv4_addr_t));
        conn->proto.udp->remote_port = port;
        sint16          cres = espconn_sendto (conn, (uint8 *) data_out, length_out);
        if (cres)
            d_log_wprintf (UDPCTL_SERVICE_NAME, IPSTR ":%u sent failed:%u", IP2STR (&addr), port, cres);
    }
#endif
}


LOCAL void      ICACHE_FLASH_ATTR
udpctl_setup (void)
{
#ifdef ARCH_XTENSA
    if (sdata->proto_udp.local_port)
        os_conn_free (&sdata->conn);

    sdata->conn.type = ESPCONN_UDP;
    sdata->conn.proto.udp = &sdata->proto_udp;
    sdata->proto_udp.local_port = sdata->conf.port;
    d_log_iprintf (UDPCTL_SERVICE_NAME, "listen port:%u, secret length:%u", sdata->conf.port, sdata->conf.secret_len);
    if (os_conn_create (&sdata->conn) || os_conn_set_recvcb (&sdata->conn, uctl_recv_cb)) {
        d_log_eprintf (UDPCTL_SERVICE_NAME, "conn setup failed");
        return;
    }
#endif
}

svcs_errcode_t  ICACHE_FLASH_ATTR
udpctl_on_start (const svcs_resource_t * svcres, dtlv_ctx_t * conf)
{
    if (sdata)
        return SVCS_SERVICE_ERROR;

    d_svcs_check_imdb_error (imdb_clsobj_insert
                             (svcres->hmdb, svcres->hdata, d_pointer_as (void *, &sdata), sizeof (udpctl_data_t))
        );
    os_memset (sdata, 0, sizeof (udpctl_data_t));
    sdata->svcres = svcres;

    udpctl_on_cfgupd (conf);

    return SVCS_ERR_SUCCESS;
}

svcs_errcode_t  ICACHE_FLASH_ATTR
udpctl_on_stop ()
{
    if (!sdata)
        return SVCS_NOT_RUN;

#ifdef ARCH_XTENSA
    if (os_conn_free (&sdata->conn))
        d_log_eprintf (UDPCTL_SERVICE_NAME, "conn free error");
#endif
    d_svcs_check_imdb_error (imdb_clsobj_delete (sdata->svcres->hmdb, sdata->svcres->hdata, sdata));

    sdata = NULL;

    return SVCS_ERR_SUCCESS;
}

LOCAL svcs_errcode_t ICACHE_FLASH_ATTR
udpctl_on_msg_info (dtlv_ctx_t * msg_out)
{
    dtlv_avp_t     *gavp;
    d_svcs_check_imdb_error (dtlv_avp_encode_list (msg_out, 0, UDPCTL_AVP_CLIENT, DTLV_TYPE_OBJECT, &gavp));

    int             i;
    for (i = 0; i < MIN (UDPCTL_CLIENTS_MAX, sdata->conf.clients_limit); i++) {
        if (sdata->clients[i].state == UCTL_CLNT_STATE_NONE)
            continue;

        dtlv_avp_t     *gavp_in;
        d_svcs_check_imdb_error (dtlv_avp_encode_grouping (msg_out, 0, UDPCTL_AVP_CLIENT, &gavp_in) ||
                                 dtlv_avp_encode_octets (msg_out, COMMON_AVP_IPV4_ADDRESS,
                                                         sizeof (sdata->clients[i].remote_ip),
                                                         (char *) &sdata->clients[i].remote_ip.bytes)
                                 || dtlv_avp_encode_uint16 (msg_out, COMMON_AVP_IP_PORT, sdata->clients[i].remote_port)
                                 || dtlv_avp_encode_uint8 (msg_out, UDPCTL_AVP_CLIENT_STATE, sdata->clients[i].state)
                                 || dtlv_avp_encode_uint32 (msg_out, UDPCTL_AVP_CLIENT_FIRST_TIME,
                                                            lt_time (&sdata->clients[i].first_time))
                                 || dtlv_avp_encode_uint32 (msg_out, UDPCTL_AVP_CLIENT_LAST_TIME,
                                                            lt_time (&sdata->clients[i].last_time))
                                 || dtlv_avp_encode_group_done (msg_out, gavp_in));
    }

    d_svcs_check_imdb_error (dtlv_avp_encode_group_done (msg_out, gavp));

    return SVCS_ERR_SUCCESS;
}


svcs_errcode_t  ICACHE_FLASH_ATTR
udpctl_on_message (service_ident_t orig_id,
                   service_msgtype_t msgtype, void *ctxdata, dtlv_ctx_t * msg_in, dtlv_ctx_t * msg_out)
{
    svcs_errcode_t  res = SVCS_ERR_SUCCESS;
    switch (msgtype) {
    case SVCS_MSGTYPE_INFO:
        res = udpctl_on_msg_info (msg_out);
        break;
    default:
        res = SVCS_MSGTYPE_INVALID;
    }

    return res;
}


void ICACHE_FLASH_ATTR
task_udpctl_setup (void *arg)
{
    udpctl_setup ();
}

svcs_errcode_t  ICACHE_FLASH_ATTR
udpctl_on_cfgupd (dtlv_ctx_t * conf)
{
    os_memset (&sdata->conf, 0, sizeof (udpctl_conf_t));
    sdata->conf.clients_limit = UDPCTL_CLIENTS_MAX;
    sdata->conf.idle_tx = UDPCTL_DEFAULT_IDLE_TX;
    sdata->conf.recycle_tx = UDPCTL_DEFAULT_RECYCLE_TX;
    sdata->conf.auth_tx = UDPCTL_DEFAULT_AUTH_TX;
    sdata->conf.port = UDPCTL_DEFAULT_PORT;
#ifdef UDPCTL_DEFAULT_SECRET
    sdata->conf.secret_len = os_strlen (UDPCTL_DEFAULT_SECRET);
    os_memcpy (sdata->conf.secret, UDPCTL_DEFAULT_SECRET, sdata->conf.secret_len);
#else
    sdata->conf.secret_len = system_get_default_secret (sdata->conf.secret, sizeof (sdata->conf.secret));
#endif

    if (conf) {
        dtlv_seq_decode_begin (conf, UDPCTL_SERVICE_ID);
        dtlv_seq_decode_octets (UDPCTL_AVP_SECRET, sdata->conf.secret, sizeof (sdata->conf.secret),
                                sdata->conf.secret_len);
        dtlv_seq_decode_uint32 (UDPCTL_AVP_MULTICAST_ADDR, &sdata->conf.maddr.addr);
        dtlv_seq_decode_uint16 (COMMON_AVP_IP_PORT, &sdata->conf.port);
        dtlv_seq_decode_end (conf);
    }

#ifdef ARCH_XTENSA
    if (!system_post_delayed_cb (task_udpctl_setup, NULL))
        d_log_eprintf (UDPCTL_SERVICE_NAME, "task setup failed");
#endif

    return SVCS_ERR_SUCCESS;
}

svcs_errcode_t  ICACHE_FLASH_ATTR
udpctl_service_install ()
{
    svcs_service_def_t sdef;
    os_memset (&sdef, 0, sizeof (svcs_service_def_t));
    sdef.enabled = true;
    sdef.on_cfgupd = udpctl_on_cfgupd;
    sdef.on_message = udpctl_on_message;
    sdef.on_start = udpctl_on_start;
    sdef.on_stop = udpctl_on_stop;

    return svcctl_service_install (UDPCTL_SERVICE_ID, UDPCTL_SERVICE_NAME, &sdef);
}

svcs_errcode_t  ICACHE_FLASH_ATTR
udpctl_service_uninstall ()
{
    return svcctl_service_uninstall (UDPCTL_SERVICE_NAME);
}
