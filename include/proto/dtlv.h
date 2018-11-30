/* 
 * TLV (Diameter-based) Protocol Support
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
 * Foobar is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Foobar.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

/*
	 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|D D L|       AVP Length        |   NS-Id   |      AVP Code     |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                             Data                              |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

*/

#ifndef __DTLV_H__
#define __DTLV_H__

#include "sysinit.h"

#define DTLV_MAX_PATH_LENGTH	16

typedef uint16  dtlv_size_t;
typedef uint16  avp_code_t;
typedef uint8   namespaceid_t;

typedef enum PACKED dtlv_datatype_s {
    DTLV_TYPE_OCTETS = 0,
    DTLV_TYPE_OBJECT = 1,
    DTLV_TYPE_INTEGER = 2,
    DTLV_TYPE_CHAR = 3
} dtlv_datatype_t;

/* Encoded AVP Header */
typedef struct dtlv_havpe_s {
    uint16          flags_and_length;
    uint16          namespace_and_code;
} dtlv_havpe_t;

typedef union dtlv_nscode_u {
    struct {
	namespaceid_t   namespace_id:6;
	avp_code_t      code:10;
    } comp;
    uint16          nscode;
} dtlv_nscode_t;

/* Decoded AVP Header */
typedef struct dtlv_havpd_s {
    dtlv_datatype_t datatype:2;
    bool            is_list:1;
    dtlv_size_t     length:13;
    dtlv_nscode_t   nscode;
} dtlv_havpd_t;

/* AVP */
typedef struct dtlv_avp_s {
    dtlv_havpe_t    havpe;
    char            data[];
} dtlv_avp_t;

typedef enum dtlv_errcode_e {
    DTLV_ERR_SUCCESS = 0,
    DTLV_AVP_NOT_GROUPING = 1,
    DTLV_AVP_INVALID_LEN = 2,
    DTLV_AVP_INV_TYPE = 3,
    DTLV_BUFFER_OVERFLOW = 4,
    DTLV_AVP_OUT_OF_BOUNDS = 5,
    DTLV_END_OF_DATA = 6,
    DTLV_PATH_ERROR = 7,
    DTLV_FORALL_BREAK = 8,
    DTLV_FORALL_STEP_OVER = 9,
    DTLV_FORALL_FUNC = 10,
} dtlv_errcode_t;

typedef struct PACKED dtlv_ctx_s {
    char           *buf;
    dtlv_size_t     buflen;
    dtlv_size_t     datalen;
    dtlv_size_t     position;
    uint8           depth;
    dtlv_havpd_t    path[DTLV_MAX_PATH_LENGTH];
} dtlv_ctx_t;

/* Decoded AVP */
typedef struct dtlv_davp_s {
    dtlv_havpd_t    havpd;	// decoded AVP Header
    dtlv_avp_t     *avp;	// pointer to encoded AVP
} dtlv_davp_t;

dtlv_errcode_t  dtlv_ctx_init_decode (dtlv_ctx_t * ctx, char *buf, dtlv_size_t datalen);
dtlv_errcode_t  dtlv_ctx_reset_decode (dtlv_ctx_t * ctx);
dtlv_errcode_t  dtlv_ctx_init_encode (dtlv_ctx_t * ctx, char *buf, dtlv_size_t buflen);
dtlv_errcode_t  dtlv_ctx_reset_encode (dtlv_ctx_t * ctx);

#define d_avp_data_length(length)	((length) - sizeof(dtlv_havpe_t))
#define d_avp_full_length(dlen)		((dlen) + sizeof(dtlv_havpe_t))

#define d_ctx_next_avp_data_ptr(ctx)	((ctx)->buf + (ctx)->datalen + sizeof(dtlv_havpe_t))
#define d_ctx_length_left(ctx)		((ctx)->buflen - (ctx)->datalen)

#define dtlv_avp_encode_list(ctx, namespace_id, avp_code, data_type, avp) \
	dtlv_avp_encode((ctx), (namespace_id), (avp_code), (data_type), 0, true, (avp))

#define dtlv_avp_encode_grouping(ctx, namespace_id, avp_code, avp) \
	dtlv_avp_encode((ctx), (namespace_id), (avp_code), DTLV_TYPE_OBJECT, 0, false, (avp))

#define dtlv_check_namespace(davp, nsid) ( ((davp)->havpd.nscode.comp.namespace_id == 0) || ((davp)->havpd.nscode.comp.namespace_id == (nsid)) )

#define dtlv_seq_decode_begin(ctx, nsid) \
	{ \
	    dtlv_davp_t     davp; \
	    while (dtlv_avp_decode ((ctx), &davp) == DTLV_ERR_SUCCESS) { \
		if (dtlv_check_namespace (&davp, (nsid) )) {\
		switch (davp.havpd.nscode.comp.code) {

#define dtlv_seq_decode_ns(nsid) \
                }} \
		if (dtlv_check_namespace (&davp, (nsid) )) { \
		switch (davp.havpd.nscode.comp.code) {

#define dtlv_seq_decode_ptr(code, trg, type) \
		case code: \
		    trg = d_pointer_as( type, davp.avp->data ); \
		    break;

#define dtlv_seq_decode_uint8(code, trg) \
		case code: \
		    dtlv_avp_get_uint8 (&davp, trg); \
		    break;

#define dtlv_seq_decode_octets(code, trg, buflen, outlen) \
		case code: \
		    outlen = d_avp_data_length(davp.havpd.length); \
		    os_memcpy (trg, davp.avp->data, MIN ((outlen), (buflen))); \
		    break;

#define dtlv_seq_decode_group(code, trg, outlen) \
		case code: \
		    outlen = d_avp_data_length(davp.havpd.length); \
		    trg = davp.avp->data; \
		    break;

#define dtlv_seq_decode_uint16(code, trg) \
		case code: \
		    dtlv_avp_get_uint16 (&davp, trg); \
		    break;

#define dtlv_seq_decode_uint32(code, trg) \
		case code: \
		    dtlv_avp_get_uint32 (&davp, trg); \
		    break;

#define dtlv_seq_decode_end(ctx) 	}}}}


/*
[public] encode avp
  - ctx: encoder context
  - namespace_id: Namesapce identity
  - avp_code: AVP code
  - data_type: Data type
  - data_legth: exact data length
  - is_list: flag indicates that this AVP is a list
  - avp: avp pointers
*/
dtlv_errcode_t  dtlv_avp_encode (dtlv_ctx_t * ctx,
				 const namespaceid_t namespace_id,
				 const avp_code_t avp_code,
				 const dtlv_datatype_t data_type,
				 const dtlv_size_t data_length, const bool is_list, dtlv_avp_t ** avp);

dtlv_errcode_t  dtlv_avp_encode_group_done (dtlv_ctx_t * ctx, dtlv_avp_t * avp);
dtlv_errcode_t  dtlv_avp_encode_uint8 (dtlv_ctx_t * ctx, const avp_code_t avp_code, const uint8 data);
dtlv_errcode_t  dtlv_avp_encode_uint16 (dtlv_ctx_t * ctx, const avp_code_t avp_code, const uint16 data);
dtlv_errcode_t  dtlv_avp_encode_uint32 (dtlv_ctx_t * ctx, const avp_code_t avp_code, const uint32 data);
dtlv_errcode_t  dtlv_avp_encode_octets (dtlv_ctx_t * ctx, const avp_code_t avp_code, const size_t length,
					const char *data);
dtlv_errcode_t  dtlv_avp_encode_char (dtlv_ctx_t * ctx, const avp_code_t avp_code, const char *data);
dtlv_errcode_t  dtlv_avp_encode_nchar (dtlv_ctx_t * ctx, const avp_code_t avp_code, const size_t maxlen,
				       const char *data);
dtlv_errcode_t  dtlv_raw_encode (dtlv_ctx_t * ctx, char * buf, dtlv_size_t datalen);

#define d_ctx_left_size(ctx)		((ctx)->buflen - (ctx)->datalen)

dtlv_errcode_t  dtlv_avp_decode (dtlv_ctx_t * ctx, dtlv_davp_t * avp);
dtlv_errcode_t  dtlv_avp_decode_bypath (dtlv_ctx_t * ctx, dtlv_nscode_t * path, dtlv_davp_t avp_array[],
					uint16 arry_len, bool limit_count, uint16 * total_count);

typedef         dtlv_errcode_t (*dtlv_forall_avp_func) (dtlv_davp_t * avp, const dtlv_ctx_t * ctx, const void *data,
							const bool group_exit);
dtlv_errcode_t  dtlv_decode_forall (dtlv_ctx_t * ctx, const void *data, dtlv_nscode_t * path,
				    dtlv_forall_avp_func forall_func);
dtlv_errcode_t  dtlv_decode_to_json (dtlv_ctx_t * ctx, char *buf);

dtlv_errcode_t  dtlv_avp_get_uint8 (dtlv_davp_t * avp, uint8 * data);
dtlv_errcode_t  dtlv_avp_get_uint16 (dtlv_davp_t * avp, uint16 * data);
dtlv_errcode_t  dtlv_avp_get_uint32 (dtlv_davp_t * avp, uint32 * data);
dtlv_errcode_t  dtlv_avp_get_uint (dtlv_davp_t * avp, uint32 * data);
dtlv_errcode_t  dtlv_avp_get_char (dtlv_davp_t * avp, char *data);

#define d_avp_set_uint8(avp, value)		*(uint8*)((avp)->data) = (value);
#define d_avp_set_uint16(avp, value)		*(uint16*)((avp)->data) = htobe16(value);
#define d_avp_set_uint32(avp, value)		*(uint32*)((avp)->data) = htobe32(value);

#endif
