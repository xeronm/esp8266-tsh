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

#include "sysinit.h"
#include "core/utils.h"
#include "core/logging.h"
#include "core/system.h"
#include "system/comavp.h"
#include "system/imdb.h"
#include "service/ntp.h"
#ifdef ARCH_XTENSA
#include "espconn.h"
#endif

#define NTPv1	1               /* NTPv1 */
#define NTPv2	2               /* NTPv2 */
#define NTPv3	3               /* NTPv3 */

#define NTP_LEAP_WARNING	0
#define NTP_LEAP_NOT_SYNC	3

#define NTP_MODE_CLIENT		3
#define NTP_MODE_SERVER		4

#define NTP_EPOCH_1970_DELTA	0x83AA7E80UL    // difference between 01.01.1970 and POSIX 01.01.1900

#define USEC2FRAC_FACTOR	(0xFFFFFFFF/USEC_PER_SEC)
#define MSEC2FRAC_FACTOR	(0xFFFFFFFF/MSEC_PER_SEC)

#define d_frac_to_usec(val) 	((val)/USEC2FRAC_FACTOR)
#define d_usec_to_frac(val) 	((val)*USEC2FRAC_FACTOR)

#define d_frac_to_msec(val) 	((val)/MSEC2FRAC_FACTOR)
#define d_msec_to_frac(val) 	((val)*MSEC2FRAC_FACTOR)

#define betstoh(val)	{ (val).seconds = be32toh((val).seconds); (val).fraction = be32toh((val).fraction); }
#define htobets(val)	{ (val).seconds = htobe32((val).seconds); (val).fraction = htobe32((val).fraction); }

typedef struct ntp_short_s {
    uint16          seconds;
    uint16          fraction;
} ntp_short_t;

/*
 * NTP Configuration
 *  - local_port: local port
 *  - poll_timeout: poll timeout in minutes
 *  - time_zone: local time zone
 *  - hostname: NTP server hosts
 */
typedef struct ntp_conf_s {
    ip_port_t       local_port;
    uint8           poll_timeout;
    sint8           time_zone;
    ntp_hostname_t  hostname[NTP_MAX_PEERS];
} ntp_conf_t;

typedef struct ntp_peer_s {
    ipv4_addr_t     ipaddr;
    ntp_peer_state_t state;
    sint64          rtt_mean;
    sint64          rtt_m2;     // Welford variance algorythm
    uint8           rcnt:4;
    uint8           acnt:4;
    ntp_timestamp_t local_ts;
    ntp_timestamp_t peer_ts;
} ntp_peer_t;

typedef struct ntp_data_s {
    const svcs_resource_t *svcres;

    ip_conn_t       ntpconn;
    ip_conn_t       dnsconn;
#ifdef ARCH_XTENSA
    esp_udp         ntpudp;
    esp_tcp         dnstcp;
#endif
    ntp_tx_state_t  tx_state;
    os_time_t       tx_state_time;
#ifdef ARCH_XTENSA
    os_timer_t      tx_timer;
    os_timer_t      poll_timer;
#endif
    ntp_query_cb_func_t tx_done_cb;

    ntp_peer_t      peers[NTP_MAX_PEERS];
    uint8           precision;

    ntp_conf_t      conf;
} ntp_data_t;


/*
       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |LI | VN  |Mode |    Stratum     |     Poll      |  Precision   |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                         Root Delay                            |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                         Root Dispersion                       |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                          Reference ID                         |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      +                     Reference Timestamp (64)                  +
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      +                      Origin Timestamp (64)                    +
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      +                      Receive Timestamp (64)                   +
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      +                      Transmit Timestamp (64)                  +
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/

typedef struct ntp_packet_s {
    uint8           mode:3;
    uint8           version:3;
    uint8           leap:2;
    uint8           stratum;
    uint8           poll;
    uint8           precision;
    ntp_short_t     root_delay;
    ntp_short_t     root_dispersion;
    uint32          reference_id;
    ntp_timestamp_t reference_ts;
    ntp_timestamp_t origin_ts;
    ntp_timestamp_t receive_ts;
    ntp_timestamp_t transmit_ts;
} ntp_packet_t;

LOCAL ntp_data_t *sdata = NULL;

LOCAL void      dns_result (const char *name, ip_addr_t * ipaddr, void *callback_arg);

/*
LOCAL void ICACHE_FLASH_ATTR
ntp_time_add(ntp_timestamp_t *dst, const ntp_timestamp_t *src) {
	dst->seconds += src->seconds;
	if (~dst->fraction < src->fraction) {
		dst->seconds += 1;
	}
	dst->fraction += src->fraction;
}
*/

LOCAL void      ICACHE_FLASH_ATTR
ntp_time_sub (ntp_timestamp_t * dst, const ntp_timestamp_t * src)
{
    dst->seconds -= src->seconds;
    if (dst->fraction < src->fraction) {
        dst->seconds -= 1;
    }
    dst->fraction -= src->fraction;
}

LOCAL void      ICACHE_FLASH_ATTR
ntp_time (ntp_timestamp_t * ts)
{
    lt_timestamp_t  ts2;
    lt_get_time (&ts2);

    ts->seconds = ts2.sec + NTP_EPOCH_1970_DELTA;
    ts->fraction = d_usec_to_frac (ts2.usec);
}

LOCAL sint64_t  ICACHE_FLASH_ATTR
ntp_time_diff_usec (const ntp_timestamp_t * x, const ntp_timestamp_t * y)
{
    sint64_t        res = (sint64_t) d_frac_to_usec (x->fraction) - (sint64_t) d_frac_to_usec (y->fraction);
    res += ((sint64_t) x->seconds - (sint64_t) y->seconds) * USEC_PER_SEC;
    return res;
}


LOCAL void      ICACHE_FLASH_ATTR
ntp_time_from_usec (ntp_timestamp_t * dst, const uint64_t usec)
{
    dst->seconds = (uint32) usec / USEC_PER_SEC;
    dst->fraction = d_usec_to_frac (usec % USEC_PER_SEC);
}


LOCAL void      ICACHE_FLASH_ATTR
tx_timer_set (void)
{
#ifdef ARCH_XTENSA
    os_timer_disarm (&sdata->tx_timer);
    os_timer_arm (&sdata->tx_timer, NTP_REQ_TIMEOUT_SEC * MSEC_PER_SEC, true);
#endif
}

LOCAL void      ICACHE_FLASH_ATTR
tx_timer_reset (void)
{
#ifdef ARCH_XTENSA
    os_timer_disarm (&sdata->tx_timer);
#endif
}


LOCAL void      ICACHE_FLASH_ATTR
tx_state_set (uint8 tx_state)
{
    sdata->tx_state_time = lt_ctime ();
    sdata->tx_state = tx_state;
}

LOCAL void      ICACHE_FLASH_ATTR
ntp_query_done (void)
{
    tx_timer_reset ();

    int             i;
    ntp_peer_t     *best_peer = NULL;

    for (i = 0; i < NTP_MAX_PEERS; i += 1) {
        ntp_peer_t     *peer = &sdata->peers[i];
        if (peer->state == NTP_PEER_STATE_SUCCESS) {
            if ((!best_peer) || (best_peer->rtt_m2 > peer->rtt_m2))
                best_peer = peer;
        }
    }

    tx_state_set ((best_peer) ? NTP_TX_STATE_IDLE : NTP_TX_STATE_FAILED);

    if (sdata->tx_done_cb) {
        if (best_peer)
            sdata->tx_done_cb (&best_peer->local_ts, &best_peer->peer_ts);
        else
            sdata->tx_done_cb (NULL, NULL);
    }
}


LOCAL bool      ICACHE_FLASH_ATTR
ntp_peer_send (uint8 peer_idx, ntp_peer_t * peer)
{
    ntp_packet_t    packet;
    os_memset (&packet, 0, sizeof (ntp_packet_t));

    peer->rcnt += 1;

    packet.leap = NTP_LEAP_NOT_SYNC;
    packet.version = NTPv2;
    packet.mode = NTP_MODE_CLIENT;
    packet.stratum = 0;         // unspecified or invalid
    packet.poll = 10;           // 1024 sec
    packet.precision = sdata->precision;

    ntp_time (&packet.transmit_ts);
    os_memcpy (&peer->local_ts, &packet.transmit_ts, sizeof (ntp_timestamp_t));

    // converting endianess
    htobets (packet.transmit_ts);
    os_memcpy (&packet.origin_ts, &packet.transmit_ts, sizeof (ntp_timestamp_t));

#ifdef ARCH_XTENSA
    os_memcpy (sdata->ntpudp.remote_ip, peer->ipaddr.bytes, sizeof (ipv4_addr_t));
    sdata->ntpudp.remote_port = NTP_PORT;
    sint16          cres = espconn_sendto (&sdata->ntpconn, d_pointer_as (uint8, &packet), sizeof (ntp_packet_t));
#else
    sint16          cres = 0;
#endif

    if (cres) {
        peer->state = NTP_PEER_STATE_ERROR;
        d_log_wprintf (NTP_SERVICE_NAME, "ntp peer #%d:" IPSTR " sent failed:%u", peer_idx, IP2STR (&peer->ipaddr),
                       cres);
        return false;
    }
    else {
        peer->state = NTP_PEER_STATE_REQ;
        tx_timer_set ();
        return true;
    }
}

LOCAL void      ICACHE_FLASH_ATTR
ntp_peer_done (ntp_peer_t * peer)
{
    ntp_timestamp_t offs;
    ntp_time_from_usec (&offs, (uint64) (peer->rtt_mean / 2));  // adjust time with RTT median
    ntp_time_sub (&peer->local_ts, &offs);
    peer->state = (peer->acnt < NTP_ANS_COUNT_MIN) ? NTP_PEER_STATE_ERROR : NTP_PEER_STATE_SUCCESS;
}

LOCAL bool      ICACHE_FLASH_ATTR
ntp_query_next (void)
{
    int             i;
    bool            freqsend = false;

    for (i = 0; i < NTP_MAX_PEERS; i += 1) {
        ntp_peer_t     *peer = &sdata->peers[i];
        ntp_hostname_t *hostname = &sdata->conf.hostname[i];

        if (!hostname[0])
            continue;

        switch (peer->state) {
        case NTP_PEER_STATE_SUCCESS:
            break;
        case NTP_PEER_STATE_NONE:
            {
                peer->rcnt = 0;
                peer->acnt = 0;
                peer->rtt_mean = 0;
                peer->rtt_m2 = 0;

#ifdef ARCH_XTENSA
                err_t           dnsres =
                    espconn_gethostbyname (&sdata->dnsconn, (char *) &hostname[0], &peer->ipaddr.ip, dns_result);
                switch (dnsres) {
                case ESPCONN_INPROGRESS:
                    tx_timer_set ();
                    peer->state = NTP_PEER_STATE_DNS;
                    freqsend = true;
                    break;
                case ESPCONN_OK:
                    peer->state = NTP_PEER_STATE_DNSANS;
                    break;
                default:
                    d_log_wprintf (NTP_SERVICE_NAME, "dns host:%s error:%u", hostname, dnsres);
                    peer->state = NTP_PEER_STATE_ERROR;
                    break;
                }
#endif
            }
            break;
        case NTP_PEER_STATE_DNS:
            peer->state = NTP_PEER_STATE_TIMEOUT;
            break;
        case NTP_PEER_STATE_REQ:
            if (peer->rcnt == NTP_REQ_COUNT) {
                ntp_peer_done (peer);
                break;
            }
        case NTP_PEER_STATE_DNSANS:
            if (peer->ipaddr.addr != IPADDR_NONE) {
                freqsend = ntp_peer_send (i, peer);
                break;
            }
        default:
            peer->state = NTP_PEER_STATE_ERROR;
            break;
        }

        if (freqsend)
            break;
    }

    if (!freqsend)
        ntp_query_done ();

    return freqsend;
}

LOCAL void      ICACHE_FLASH_ATTR
dns_result (const char *name, ip_addr_t * ipaddr, void *callback_arg)
{
    tx_timer_reset ();

    if (sdata->tx_state != NTP_TX_STATE_PENDING) {
        d_log_dprintf (NTP_SERVICE_NAME, "dns_result, ntp invalid state:%u", sdata->tx_state);
        return;
    }

    bool            reqsend = false;
    int             i;
    for (i = 0; i < NTP_MAX_PEERS; i += 1) {
        ntp_peer_t     *peer = &sdata->peers[i];
        if (os_strncmp (sdata->conf.hostname[i], name, sizeof (ntp_hostname_t)) == 0) {
            if (ipaddr == NULL) {
                d_log_wprintf (NTP_SERVICE_NAME, "dns host:%s, failed", name);
                peer->state = NTP_PEER_STATE_ERROR;
            }
            else {
                d_log_dprintf (NTP_SERVICE_NAME, "dns host:%s, peer:%u, ip:" IPSTR, name, i, IP2STR (&ipaddr->addr));

                peer->ipaddr.addr = ipaddr->addr;
                peer->state = NTP_PEER_STATE_DNSANS;
                reqsend = ntp_peer_send (i, peer);
            }
            break;
        }
    }

    if (!reqsend)
        ntp_query_next ();
}

LOCAL void      ICACHE_FLASH_ATTR
ntp_peer_recv (uint8 peer_idx, ntp_peer_t * peer, ntp_packet_t * packet, ntp_timestamp_t * recv_ts)
{
    tx_timer_reset ();

    if (packet->version != NTPv2) {
        d_log_wprintf (NTP_SERVICE_NAME, IPSTR " invalid ntp version: %u", IP2STR (&peer->ipaddr), packet->version);
        peer->state = NTP_PEER_STATE_ERROR;
    }
    else {
        // calculate RTT median and variance
        sint64_t        _x = ntp_time_diff_usec (recv_ts, &packet->origin_ts);
        sint64_t        _y = ntp_time_diff_usec (&packet->transmit_ts, &packet->receive_ts);
        //os_printf("-- origin=%d, recv=%d:%d, trans=%d:%d, recv2=%d\n", packet->origin_ts.seconds, packet->receive_ts.seconds,  packet->receive_ts.fraction, 
        //    packet->transmit_ts.seconds, packet->transmit_ts.fraction, recv_ts->seconds);

        if ((packet->root_delay.seconds == 0 && packet->root_delay.fraction ==0) || 
             (packet->root_dispersion.seconds == 0 && packet->root_dispersion.fraction ==0) ||
             (packet->reference_ts.seconds == 0)) 
        {
            // wrong response
        }
        else if ((_x > 0) || (_y > 0) || (_x > _y)) {        // wrong times
            os_memcpy (&peer->peer_ts, &packet->transmit_ts, sizeof (ntp_timestamp_t));
            os_memcpy (&peer->local_ts, recv_ts, sizeof (ntp_timestamp_t));

            _x -= _y;
            peer->acnt++;
            peer->rtt_mean += (_x - peer->rtt_mean) / peer->acnt;
            sint64_t        _d2 = _x - peer->rtt_mean;
            peer->rtt_m2 += _d2 * _d2;  // M2
        }

        if (peer->state == NTP_PEER_STATE_REQ) {
            if (peer->rcnt == NTP_REQ_COUNT)
                ntp_peer_done (peer);
            else
                ntp_peer_send (peer_idx, peer); // next request
        }

    }

    if (peer->state != NTP_PEER_STATE_REQ) {
        ntp_query_next ();
    }
}

LOCAL void      ICACHE_FLASH_ATTR
ntp_recv_cb (void *arg, char *pusrdata, unsigned short length)
{
    ntp_timestamp_t recv_ts;
    ntp_time (&recv_ts);

#ifdef ARCH_XTENSA
    struct espconn *conn = d_pointer_as (struct espconn, arg);
    remot_info     *con_info;
    espconn_get_connection_info (conn, &con_info, 0);
#endif

    ipv4_addr_t     remote_ipaddr;
#ifdef ARCH_XTENSA
    os_memcpy (remote_ipaddr.bytes, con_info->remote_ip, sizeof (ipv4_addr_t));
    d_log_dprintf (NTP_SERVICE_NAME, "recv len=%d from " IPSTR ":%d->%d", length, IP2STR (&remote_ipaddr),
                   con_info->remote_port, sdata->ntpudp.local_port);
#endif

    if (!pusrdata)
        return;

    //d_log_ebprintf (NTP_SERVICE_NAME, pusrdata, length, "-- recv from " IPSTR ":%d->%d", IP2STR (&remote_ipaddr),
    //               con_info->remote_port, sdata->ntpudp.local_port);

    if (sdata->tx_state != NTP_TX_STATE_PENDING) {
        d_log_dprintf (NTP_SERVICE_NAME, "invalid state: %u", sdata->tx_state);
        return;
    }

    if (length != sizeof (ntp_packet_t)) {
        d_log_dprintf (NTP_SERVICE_NAME, "invalid length: %u", length);
        return;
    }

    ntp_packet_t   *packet = d_pointer_as (ntp_packet_t, pusrdata);

    betstoh (packet->reference_ts);
    // converting endianess
    betstoh (packet->transmit_ts);
    betstoh (packet->origin_ts);
    betstoh (packet->receive_ts);

    int             i;
    for (i = 0; i < NTP_MAX_PEERS; i += 1) {
        ntp_peer_t     *peer = &(sdata->peers[i]);

        if ((peer->ipaddr.addr == remote_ipaddr.addr) &&
            (os_memcmp (&peer->local_ts, &packet->origin_ts, sizeof (ntp_timestamp_t)) == 0)) {
            if (peer->state != NTP_PEER_STATE_REQ) {
                d_log_dprintf (NTP_SERVICE_NAME, "peer #%d invalid state: %u", i, peer->state);
            }
            else
                ntp_peer_recv (i, peer, packet, &recv_ts);
            return;
        }
    }

    d_log_dprintf (NTP_SERVICE_NAME, "response from unknown peer");
}


LOCAL void      ICACHE_FLASH_ATTR
ntp_tx_timeout (void *args)
{
    ntp_query_next ();
}


bool            ICACHE_FLASH_ATTR
ntp_query (ntp_query_cb_func_t cb)
{
    if (!sdata) {
        d_log_eprintf (NTP_SERVICE_NAME, "query: not started");
        return false;
    }

    if (sdata->tx_state == NTP_TX_STATE_PENDING) {
        d_log_dprintf (NTP_SERVICE_NAME, "query in progress...");
        return false;
    }

    tx_state_set (NTP_TX_STATE_PENDING);

    sdata->tx_done_cb = cb;

    int             i;
    for (i = 0; i < NTP_MAX_PEERS; i += 1) {
        sdata->peers[i].state = NTP_PEER_STATE_NONE;
        sdata->peers[i].rcnt = 0;
        sdata->peers[i].acnt = 0;
    }

    uint32          rtc_prec = system_rtc_clock_cali_proc ();
    LOG2 (rtc_prec, sdata->precision);

    if (!ntp_query_next ()) {
        d_log_wprintf (NTP_SERVICE_NAME, "query: no peers");
        return false;
    }

    return true;
}


bool            ICACHE_FLASH_ATTR
ntp_query_break (void)
{
    if (!sdata) {
        d_log_eprintf (NTP_SERVICE_NAME, "break: not started");
        return false;
    }

    if (sdata->tx_state != NTP_TX_STATE_IDLE) {
        tx_timer_reset ();
        tx_state_set (NTP_TX_STATE_IDLE);
    }

    return true;
}

LOCAL void      ICACHE_FLASH_ATTR
ntp_query_adjust (const ntp_timestamp_t * local_ts, const ntp_timestamp_t * peer_ts)
{
    if ((!local_ts) || (!peer_ts)) {
        d_log_wprintf (NTP_SERVICE_NAME, "adjust time failed");
        return;
    }

    uint32          big_diff_ms = SEC_PER_MIN * MSEC_PER_SEC;
    sint32          diff_ms = big_diff_ms;
    if ((2 * MAX (local_ts->seconds, peer_ts->seconds) - local_ts->seconds - peer_ts->seconds) < SEC_PER_MIN) {
        diff_ms = ntp_time_diff_usec (local_ts, peer_ts) / USEC_PER_MSEC;
        if (ABS (diff_ms) < NTP_ADJUST_MIN_MSEC)
            return;
    }

    lt_timestamp_t  ts;
    lt_get_time (&ts);

    lt_timestamp_t  lts = { local_ts->seconds - NTP_EPOCH_1970_DELTA, d_frac_to_usec (local_ts->fraction) };
    lt_timestamp_t  pts = { peer_ts->seconds - NTP_EPOCH_1970_DELTA, d_frac_to_usec (peer_ts->fraction) };

    lt_time_sub (&ts, &lts);
    lt_time_add (&ts, &pts);
    lt_set_time (&ts);

    ltm_t           _tm;
    lt_localtime (lt_time (NULL), &_tm, false);
    if (ABS (diff_ms) < big_diff_ms) {
        d_log_wprintf (NTP_SERVICE_NAME, "adjust time to: " TMSTR_TZ " offset:%d.%u", TM2STR_TZ (&_tm),
                       diff_ms / MSEC_PER_SEC, ABS (diff_ms) % MSEC_PER_SEC);
    }
    else {
        d_log_wprintf (NTP_SERVICE_NAME, "adjust time to:" TMSTR_TZ, TM2STR_TZ (&_tm));
        lt_timestamp_t  data[2];
        os_memcpy (&data[0], &lts, sizeof (lt_timestamp_t));
        os_memcpy (&data[1], &pts, sizeof (lt_timestamp_t));
        svcctl_service_message (0, 0, &data, SVCS_MSGTYPE_ADJTIME, NULL, NULL);
    }
}

LOCAL void      ICACHE_FLASH_ATTR
ntp_poll_timeout (void *args)
{
    ntp_set_date ();
}

LOCAL svcs_errcode_t ICACHE_FLASH_ATTR
ntp_on_msg_info (dtlv_ctx_t * msg_out)
{
    dtlv_avp_t     *gavp;
    d_svcs_check_imdb_error (dtlv_avp_encode_uint8 (msg_out, NTP_AVP_POLL_INTERVAL, sdata->conf.poll_timeout) ||
                             dtlv_avp_encode_uint8 (msg_out, COMMON_AVP_TIME_ZONE, sdata->conf.time_zone) ||
                             dtlv_avp_encode_uint8 (msg_out, NTP_AVP_QUERY_STATE, sdata->tx_state) ||
                             dtlv_avp_encode_uint32 (msg_out, NTP_AVP_QUERY_STATE_TIME, lt_time (&sdata->tx_state_time))
                             || dtlv_avp_encode_list (msg_out, 0, NTP_AVP_PEER, DTLV_TYPE_OBJECT, &gavp));

    int             i;
    for (i = 0; i < NTP_MAX_PEERS; i += 1) {
        ntp_peer_t     *peer = &(sdata->peers[i]);
        ntp_hostname_t *hostname = &sdata->conf.hostname[i];
        if (!*hostname)
            continue;

        dtlv_avp_t     *gavp_in;
        d_svcs_check_imdb_error (dtlv_avp_encode_grouping (msg_out, 0, NTP_AVP_PEER, &gavp_in) ||
                                 dtlv_avp_encode_nchar (msg_out, COMMON_AVP_HOST_NAME, sizeof (ntp_hostname_t),
                                                        (char *) hostname)
                                 || dtlv_avp_encode_octets (msg_out, COMMON_AVP_IPV4_ADDRESS, sizeof (peer->ipaddr),
                                                            (char *) &peer->ipaddr.bytes)
                                 || dtlv_avp_encode_uint8 (msg_out, NTP_AVP_PEER_STATE, peer->state)
                                 || ((peer->state == NTP_PEER_STATE_SUCCESS) ?
                                     (dtlv_avp_encode_uint32 (msg_out, NTP_AVP_PEER_RTT_MEAN, (uint32) peer->rtt_mean)
                                      || dtlv_avp_encode_uint32 (msg_out, NTP_AVP_PEER_RTT_VARIANCE,
                                                                 (uint32) (peer->rtt_m2 / peer->acnt)))
                                     : false)
            );
        if (peer->peer_ts.seconds)
            d_svcs_check_imdb_error (dtlv_avp_encode_uint32
                                     (msg_out, NTP_AVP_PEER_OFFSET,
                                      (uint32) (ntp_time_diff_usec (&peer->local_ts, &peer->peer_ts) / USEC_PER_MSEC)));

        d_svcs_check_imdb_error (dtlv_avp_encode_group_done (msg_out, gavp_in));
    }

    d_svcs_check_imdb_error (dtlv_avp_encode_group_done (msg_out, gavp));

    return SVCS_ERR_SUCCESS;
}

bool            ICACHE_FLASH_ATTR
ntp_set_date (void)
{
    return ntp_query (ntp_query_adjust);
}

svcs_errcode_t  ICACHE_FLASH_ATTR
ntp_on_message (service_ident_t orig_id,
                service_msgtype_t msgtype, void *ctxdata, dtlv_ctx_t * msg_in, dtlv_ctx_t * msg_out)
{
    svcs_errcode_t  res = SVCS_ERR_SUCCESS;
    switch (msgtype) {
    case SVCS_MSGTYPE_INFO:
        res = ntp_on_msg_info (msg_out);
        break;
    case SVCS_MSGTYPE_NETWORK:
        if ((sdata->tx_state != NTP_TX_STATE_FAILED) && (sdata->tx_state != NTP_TX_STATE_NONE))
            break;
    case NTP_MSGTYPE_SETDATE:
        ntp_set_date ();
        break;
    default:
        res = SVCS_MSGTYPE_INVALID;
    }

    return res;
}

LOCAL void      ICACHE_FLASH_ATTR
ntp_setup (void)
{
    if (!lt_set_timezone (sdata->conf.time_zone))
        d_log_eprintf (NTP_SERVICE_NAME, "invalid timezone: %d", sdata->conf.time_zone);

#ifdef ARCH_XTENSA
    if (sdata->ntpudp.local_port)
        os_conn_free (&sdata->ntpconn);

    sdata->ntpconn.type = ESPCONN_UDP;
    sdata->ntpconn.proto.udp = &sdata->ntpudp;
    sdata->ntpudp.local_port = (sdata->conf.local_port) ? sdata->conf.local_port : espconn_port ();
    sdata->ntpudp.remote_port = NTP_PORT;

    sdata->dnsconn.type = ESPCONN_TCP;
    sdata->dnsconn.proto.tcp = &(sdata->dnstcp);
    d_log_iprintf (NTP_SERVICE_NAME, "timezone: " TZSTR ", localport: %u", TZ2STR (lt_get_timezone ()),
                   sdata->ntpudp.local_port);

    if (os_conn_create (&sdata->ntpconn) || os_conn_set_recvcb (&sdata->ntpconn, ntp_recv_cb)
        ) {
        d_log_eprintf (NTP_SERVICE_NAME, "conn setup failed");
        return;
    }

    d_log_iprintf (NTP_SERVICE_NAME, "poll timer: %u min", sdata->conf.poll_timeout);
    os_timer_disarm (&sdata->poll_timer);
    os_timer_arm (&sdata->poll_timer, sdata->conf.poll_timeout * MSEC_PER_MIN, true);
#endif
}

svcs_errcode_t  ICACHE_FLASH_ATTR
ntp_on_start (const svcs_resource_t * svcres, dtlv_ctx_t * conf)
{
    if (sdata)
        return SVCS_SERVICE_ERROR;

    d_svcs_check_imdb_error (imdb_clsobj_insert
                             (svcres->hmdb, svcres->hdata, d_pointer_as (void *, &sdata), sizeof (ntp_data_t))
        );
    os_memset (sdata, 0, sizeof (ntp_data_t));
    sdata->svcres = svcres;

    sdata->tx_state = NTP_TX_STATE_NONE;

#ifdef ARCH_XTENSA
    os_timer_disarm (&sdata->tx_timer);
    os_timer_setfn (&sdata->tx_timer, ntp_tx_timeout, NULL);

    os_timer_disarm (&sdata->poll_timer);
    os_timer_setfn (&sdata->poll_timer, ntp_poll_timeout, NULL);
#endif
    ntp_on_cfgupd (conf);

    return SVCS_ERR_SUCCESS;
}

svcs_errcode_t  ICACHE_FLASH_ATTR
ntp_on_stop ()
{
    if (!sdata)
        return SVCS_NOT_RUN;

#ifdef ARCH_XTENSA
    os_timer_disarm (&sdata->tx_timer);
    os_timer_disarm (&sdata->poll_timer);

    if (os_conn_free (&sdata->ntpconn)) //|| os_conn_free (&sdata->dnsconn))
        d_log_eprintf (NTP_SERVICE_NAME, "conn free error");
#endif
    d_svcs_check_imdb_error (imdb_clsobj_delete (sdata->svcres->hmdb, sdata->svcres->hdata, sdata));
    sdata = NULL;

    return SVCS_ERR_SUCCESS;
}

svcs_errcode_t  ICACHE_FLASH_ATTR
ntp_on_cfgupd (dtlv_ctx_t * conf)
{
    os_memset (&sdata->conf, 0, sizeof (ntp_conf_t));
    sdata->conf.local_port = NTP_DEFAULT_LOCAL_PORT;
    sdata->conf.poll_timeout = NTP_DEFAULT_POLL_TIMEOUT_MIN;
    sdata->conf.time_zone = NTP_DEFAULT_TIME_ZONE;
#ifdef NTP_DEFAULT_SERVER_0
    os_memcpy (sdata->conf.hostname[0], NTP_DEFAULT_SERVER_0,
               MIN (sizeof (ntp_hostname_t), os_strlen (NTP_DEFAULT_SERVER_0)));
#endif
#ifdef NTP_DEFAULT_SERVER_1
    os_memcpy (sdata->conf.hostname[1], NTP_DEFAULT_SERVER_1,
               MIN (sizeof (ntp_hostname_t), os_strlen (NTP_DEFAULT_SERVER_1)));
#endif

    if (conf) {
        dtlv_ctx_t      dtlv_peers;
        os_memset (&dtlv_peers, 0, sizeof (dtlv_ctx_t));

        dtlv_seq_decode_begin (conf, NTP_SERVICE_ID);
        dtlv_seq_decode_uint8 (COMMON_AVP_TIME_ZONE, (uint8 *) & sdata->conf.time_zone);
        dtlv_seq_decode_uint8 (NTP_AVP_POLL_INTERVAL, &sdata->conf.poll_timeout);
        dtlv_seq_decode_group (NTP_AVP_PEER, dtlv_peers.buf, dtlv_peers.datalen);
        dtlv_seq_decode_end (conf);


        dtlv_nscode_t   path[] = { {{0, NTP_SERVICE_ID}
                                    }
        , {{0, NTP_AVP_PEER}
           }
        , {{0, NTP_AVP_PEER}
           }
        ,
        {{0, COMMON_AVP_HOST_NAME}
         }
        , {{0, 0}
           }
        };
        dtlv_davp_t     avp_array[NTP_MAX_PEERS];
        uint16          total_count = 0;

        dtlv_avp_decode_bypath (conf, path, avp_array, NTP_MAX_PEERS, true, &total_count);
        uint8           i;
        for (i = 0; i < total_count; i++) {
            os_memcpy (sdata->conf.hostname[i], avp_array[i].avp->data,
                       MIN (sizeof (ntp_hostname_t), d_avp_data_length (avp_array[i].havpd.length)));
        }
    }

    sdata->conf.poll_timeout = MAX (3, sdata->conf.poll_timeout);

    ntp_setup ();

    return SVCS_ERR_SUCCESS;
}

svcs_errcode_t  ICACHE_FLASH_ATTR
ntp_service_install (bool enabled)
{
    svcs_service_def_t sdef;
    os_memset (&sdef, 0, sizeof (svcs_service_def_t));
    sdef.enabled = enabled;
    sdef.multicast = true;
    sdef.on_cfgupd = ntp_on_cfgupd;
    sdef.on_message = ntp_on_message;
    sdef.on_start = ntp_on_start;
    sdef.on_stop = ntp_on_stop;

    return svcctl_service_install (NTP_SERVICE_ID, NTP_SERVICE_NAME, &sdef);
}

svcs_errcode_t  ICACHE_FLASH_ATTR
ntp_service_uninstall ()
{
    return svcctl_service_uninstall (NTP_SERVICE_NAME);
}
