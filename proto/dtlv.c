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

#include "sysinit.h"
#include "core/utils.h"
#include "proto/dtlv.h"

#define d_check_dtlv_error(ret)	if ((ret) != DTLV_ERR_SUCCESS) { return (ret); }

#define AVP_LENGTH_MASK			0x1FFF	// 13 bytes
#define AVP_NSID_MASK			0xFC00	// 6 bytes
#define AVP_CODE_MASK			0x03FF	// 10 bytes
#define AVP_FLAG_LIST			0x2000	// AVP is list of ...
#define AVP_FLAG_DATATYPE		0xC000	// 2-bytes datatype dtlv_datatype_t

#define d_havp_encode(havpd, havpe)	\
	{	\
		(havpe)->flags_and_length = htobe16( ((havpd)->datatype << 14) | ((havpd)->is_list << 13) | (havpd)->length );	\
		(havpe)->namespace_and_code = htobe16( ((havpd)->nscode.comp.namespace_id << 10) | (havpd)->nscode.comp.code );	\
	}

#define d_havp_decode(havpe, havpd)	\
	{	\
		dtlv_havpe_t havpe1;	\
		havpe1.flags_and_length = be16toh((havpe)->flags_and_length);	\
		havpe1.namespace_and_code = be16toh((havpe)->namespace_and_code);	\
		(havpd)->datatype = (havpe1.flags_and_length & AVP_FLAG_DATATYPE) >> 14;	\
		(havpd)->is_list = havpe1.flags_and_length & AVP_FLAG_LIST;	\
		(havpd)->length = havpe1.flags_and_length & AVP_LENGTH_MASK;	\
		(havpd)->nscode.comp.namespace_id = (havpe1.namespace_and_code & AVP_NSID_MASK) >> 10;	\
		(havpd)->nscode.comp.code = havpe1.namespace_and_code & AVP_CODE_MASK;	\
	}

#define d_ctxpath_push(ctx, havpd) 	\
	{	\
		os_memcpy(&(ctx)->path[(ctx)->depth - 1], havpd, sizeof(dtlv_havpd_t));	\
		(ctx)->depth++;	\
	}

#define d_ctxpath_trim(ctx) 	\
	{	\
		(ctx)->depth--;	\
		os_memset(&(ctx)->path[(ctx)->depth - 1], 0, sizeof(dtlv_havpd_t));	\
	}


INLINED void    ICACHE_FLASH_ATTR
dtlv_ctx_add_length (dtlv_ctx_t * ctx, dtlv_size_t addlen)
{
    dtlv_size_t     align_len = d_align (addlen);
    if (align_len > addlen)
	os_memset (d_pointer_add (char, ctx->buf, ctx->datalen + addlen), 0xFF, align_len - addlen);

    ctx->datalen += align_len;
}

dtlv_errcode_t  ICACHE_FLASH_ATTR
dtlv_ctx_init_encode (dtlv_ctx_t * ctx, char *buf, dtlv_size_t buflen)
{
    os_memset (ctx, 0, sizeof (dtlv_ctx_t));
    ctx->buf = buf;
    ctx->buflen = buflen;
    ctx->depth = 1;

    return DTLV_ERR_SUCCESS;
}

dtlv_errcode_t  ICACHE_FLASH_ATTR
dtlv_ctx_init_decode (dtlv_ctx_t * ctx, char *buf, dtlv_size_t datalen)
{
    os_memset (ctx, 0, sizeof (dtlv_ctx_t));
    ctx->buf = buf;
    ctx->buflen = datalen;
    ctx->depth = 1;
    ctx->datalen = datalen;

    return DTLV_ERR_SUCCESS;
}

dtlv_errcode_t  ICACHE_FLASH_ATTR
dtlv_ctx_reset_encode (dtlv_ctx_t * ctx)
{
    ctx->position = 0;
    ctx->depth = 1;
    os_memset (ctx->path, 0, sizeof (ctx->path));

    return DTLV_ERR_SUCCESS;
}

dtlv_errcode_t  ICACHE_FLASH_ATTR
dtlv_ctx_reset_decode (dtlv_ctx_t * ctx)
{
    ctx->position = 0;
    ctx->depth = 1;
    os_memset (ctx->path, 0, sizeof (ctx->path));

    return DTLV_ERR_SUCCESS;
}

/*
[public] encode avp
  - ctx: encoder context
  - namespace_id: Namesapce identity
  - avp_code: AVP code
  - data_type: Data type
  - data_legth: exact data length
  - avp: avp pointers
*/
dtlv_errcode_t  ICACHE_FLASH_ATTR
dtlv_avp_encode (dtlv_ctx_t * ctx,
		 const namespaceid_t namespace_id,
		 const avp_code_t avp_code,
		 const dtlv_datatype_t data_type, const dtlv_size_t data_length, const bool is_list, dtlv_avp_t ** avp)
{
    dtlv_size_t     length = d_avp_full_length (data_length);
    if (ctx->datalen + length > ctx->buflen)
	return DTLV_BUFFER_OVERFLOW;

    dtlv_havpd_t    havpd;
    havpd.length = length;
    havpd.datatype = data_type;
    havpd.is_list = is_list;
    havpd.nscode.comp.namespace_id = namespace_id;
    havpd.nscode.comp.code = avp_code;

    // if parent AVP is list check code & type
    if (ctx->depth >= 2) {
	dtlv_havpd_t   *parent = &ctx->path[ctx->depth - 2];
	if ((parent->is_list)
	    && ((parent->nscode.nscode != havpd.nscode.nscode) || (parent->datatype != havpd.datatype)))
	    return DTLV_PATH_ERROR;
    }

    if ((data_type == DTLV_TYPE_OBJECT) || (is_list)) {
	if (ctx->depth >= DTLV_MAX_PATH_LENGTH - 1)
	    return DTLV_PATH_ERROR;
	d_ctxpath_push (ctx, &havpd);
    }

    *avp = d_pointer_add (dtlv_avp_t, ctx->buf, ctx->datalen);
    d_havp_encode (&havpd, &(*avp)->havpe);

    dtlv_ctx_add_length (ctx, length);
    return DTLV_ERR_SUCCESS;
}


dtlv_errcode_t  ICACHE_FLASH_ATTR
dtlv_avp_encode_group_done (dtlv_ctx_t * ctx, dtlv_avp_t * avp)
{
    dtlv_havpd_t    havpd;
    d_havp_decode (&avp->havpe, &havpd);

    if ((ctx->depth < 2) && (ctx->path[ctx->depth - 2].nscode.nscode != havpd.nscode.nscode))
	return DTLV_PATH_ERROR;
    if (!havpd.is_list && (havpd.datatype != DTLV_TYPE_OBJECT))
	return DTLV_AVP_NOT_GROUPING;

    havpd.length = ctx->datalen - d_pointer_diff (avp, ctx->buf);
    if (havpd.length < sizeof (dtlv_havpe_t))
	return DTLV_AVP_INVALID_LEN;	// length less than header, internal error

    d_ctxpath_trim (ctx);

    d_havp_encode (&havpd, &avp->havpe);

    return DTLV_ERR_SUCCESS;
}

dtlv_errcode_t  ICACHE_FLASH_ATTR
dtlv_avp_encode_uint8 (dtlv_ctx_t * ctx, const avp_code_t avp_code, const uint8 data)
{
    dtlv_avp_t     *avp;
    dtlv_errcode_t  ret = dtlv_avp_encode (ctx, 0, avp_code, DTLV_TYPE_INTEGER, sizeof (uint8), false, &avp);
    d_check_dtlv_error (ret);
    d_avp_set_uint8 (avp, data);
    return DTLV_ERR_SUCCESS;
}

dtlv_errcode_t  ICACHE_FLASH_ATTR
dtlv_avp_encode_uint16 (dtlv_ctx_t * ctx, const avp_code_t avp_code, const uint16 data)
{
    dtlv_avp_t     *avp;
    dtlv_errcode_t  ret = dtlv_avp_encode (ctx, 0, avp_code, DTLV_TYPE_INTEGER, sizeof (uint16), false, &avp);
    d_check_dtlv_error (ret);
    d_avp_set_uint16 (avp, data);
    return DTLV_ERR_SUCCESS;
}

dtlv_errcode_t  ICACHE_FLASH_ATTR
dtlv_avp_encode_uint32 (dtlv_ctx_t * ctx, const avp_code_t avp_code, const uint32 data)
{
    dtlv_avp_t     *avp;
    dtlv_errcode_t  ret = dtlv_avp_encode (ctx, 0, avp_code, DTLV_TYPE_INTEGER, sizeof (uint32), false, &avp);
    d_check_dtlv_error (ret);
    d_avp_set_uint32 (avp, data);
    return DTLV_ERR_SUCCESS;
}

dtlv_errcode_t  ICACHE_FLASH_ATTR
dtlv_avp_encode_octets (dtlv_ctx_t * ctx, const avp_code_t avp_code, const size_t length, const char *data)
{
    dtlv_avp_t     *avp;
    dtlv_errcode_t  ret = dtlv_avp_encode (ctx, 0, avp_code, DTLV_TYPE_OCTETS, length, false, &avp);
    d_check_dtlv_error (ret);
    os_memcpy (avp->data, data, length);
    return DTLV_ERR_SUCCESS;
}

dtlv_errcode_t  ICACHE_FLASH_ATTR
dtlv_avp_encode_char (dtlv_ctx_t * ctx, const avp_code_t avp_code, const char *data)
{
    dtlv_avp_t     *avp;
    dtlv_size_t     length = os_strlen (data);
    dtlv_errcode_t  ret = dtlv_avp_encode (ctx, 0, avp_code, DTLV_TYPE_CHAR, length + 1, false, &avp);
    d_check_dtlv_error (ret);
    os_memcpy (avp->data, data, length);
    avp->data[length] = 0x0;
    return DTLV_ERR_SUCCESS;
}

dtlv_errcode_t  ICACHE_FLASH_ATTR
dtlv_avp_encode_nchar (dtlv_ctx_t * ctx, const avp_code_t avp_code, const size_t maxlen, const char *data)
{
    dtlv_avp_t     *avp;
    size_t          length = MIN (maxlen, os_strnlen (data, maxlen));
    dtlv_errcode_t  ret = dtlv_avp_encode (ctx, 0, avp_code, DTLV_TYPE_CHAR, length + 1, false, &avp);
    d_check_dtlv_error (ret);
    os_memcpy (avp->data, data, length);
    avp->data[length] = 0x0;
    return DTLV_ERR_SUCCESS;
}

dtlv_errcode_t  ICACHE_FLASH_ATTR
dtlv_raw_encode (dtlv_ctx_t * ctx, char * buf, dtlv_size_t datalen)
{
    if (ctx->datalen + datalen > ctx->buflen)
	return DTLV_BUFFER_OVERFLOW;
    os_memcpy ((ctx->buf + ctx->datalen), buf, datalen);
    ctx->datalen += datalen;

    return DTLV_ERR_SUCCESS;
}


dtlv_errcode_t  ICACHE_FLASH_ATTR
dtlv_avp_decode (dtlv_ctx_t * ctx, dtlv_davp_t * davp)
{
    if (ctx->position == ctx->datalen)
	return DTLV_END_OF_DATA;

    if (ctx->position + sizeof (dtlv_havpe_t) > ctx->datalen)
	return DTLV_AVP_OUT_OF_BOUNDS;

    os_memset (davp, 0, sizeof (dtlv_davp_t));
    davp->avp = d_pointer_add (dtlv_avp_t, ctx->buf, ctx->position);
    d_havp_decode (&davp->avp->havpe, &davp->havpd);

    if (davp->havpd.length < sizeof (dtlv_havpe_t))
	return DTLV_AVP_INVALID_LEN;

    if (ctx->position + davp->havpd.length > ctx->datalen)
	return DTLV_AVP_OUT_OF_BOUNDS;

    ctx->position += d_align (davp->havpd.length);

    return DTLV_ERR_SUCCESS;
}

typedef struct dtlv_decode_bypath_s {
    dtlv_davp_t    *avp_array;
    uint16          array_len;
    uint16          total_count;
    bool            limit_count;	// exit when array_len is reached by total_count
} dtlv_decode_bypath_t;

LOCAL dtlv_errcode_t ICACHE_FLASH_ATTR
forall_avp_decode_bypath (dtlv_davp_t * avp, const dtlv_ctx_t * ctx, const void *data, const bool group_exit)
{
    if (group_exit)
	return DTLV_ERR_SUCCESS;

    dtlv_decode_bypath_t *data2 = d_pointer_as (dtlv_decode_bypath_t, data);
    data2->total_count++;
    if (data2->total_count <= data2->array_len) {
	os_memcpy (data2->avp_array, avp, sizeof (dtlv_davp_t));
	data2->avp_array = d_pointer_add (dtlv_davp_t, data2->avp_array, sizeof (dtlv_davp_t));
    }
    if ((data2->limit_count) && (data2->total_count == data2->array_len))
	return DTLV_FORALL_BREAK;
    return DTLV_FORALL_STEP_OVER;
}

dtlv_errcode_t  ICACHE_FLASH_ATTR
dtlv_avp_decode_bypath (dtlv_ctx_t * ctx, dtlv_nscode_t * path, dtlv_davp_t avp_array[], uint16 array_len,
			bool limit_count, uint16 * total_count)
{
    dtlv_decode_bypath_t data;
    os_memset (&data, 0, sizeof (dtlv_decode_bypath_t));
    data.avp_array = d_pointer_as (dtlv_davp_t, &avp_array[0]);
    data.array_len = array_len;
    data.limit_count = limit_count;

    dtlv_errcode_t  res = dtlv_decode_forall (ctx, d_pointer_as (void, &data), path, forall_avp_decode_bypath);
    *total_count = data.total_count;

    return res;
}

dtlv_errcode_t  ICACHE_FLASH_ATTR
dtlv_decode_forall (dtlv_ctx_t * ctx, const void *data, dtlv_nscode_t * path, dtlv_forall_avp_func forall_func)
{
    dtlv_davp_t     davp;

    dtlv_nscode_t  *path_next = NULL;
    if (path && path->nscode) {
	path_next = d_pointer_add (dtlv_nscode_t, path, sizeof (dtlv_nscode_t));
	if (!path_next->nscode)
	    path_next = NULL;
    }

    dtlv_errcode_t  ret = DTLV_ERR_SUCCESS;
    while (ret == DTLV_ERR_SUCCESS) {
	ret = dtlv_avp_decode (ctx, &davp);
	if (ret != DTLV_ERR_SUCCESS)
	    break;

	if (path) {
	    if ((path->nscode != davp.havpd.nscode.nscode) &&
		((path->comp.namespace_id) || (path->comp.code != davp.havpd.nscode.comp.code)))
		continue;
	}

	dtlv_errcode_t  ret2 = DTLV_ERR_SUCCESS;
	if (!path_next) {
	    ret2 = forall_func (&davp, ctx, data, false);
	    switch (ret2) {
	    case DTLV_ERR_SUCCESS:
	    case DTLV_FORALL_STEP_OVER:
		break;
	    case DTLV_FORALL_BREAK:
		return (ctx->depth > 1) ? DTLV_FORALL_BREAK : DTLV_ERR_SUCCESS;
	    default:
		return DTLV_FORALL_FUNC;
	    }
	}
	if ((davp.havpd.is_list || davp.havpd.datatype == DTLV_TYPE_OBJECT) && ret2 != DTLV_FORALL_STEP_OVER
	    && davp.havpd.length > sizeof (dtlv_havpe_t)) {
	    dtlv_size_t     datalen = ctx->datalen;
	    dtlv_size_t     pos = ctx->position;

	    d_ctxpath_push (ctx, &davp.havpd);

	    ctx->position = d_pointer_diff (davp.avp, ctx->buf) + sizeof (dtlv_havpe_t);
	    ctx->datalen = pos;
	    ret = dtlv_decode_forall (ctx, data, path_next, forall_func);
	    // second callback for same avp
	    if (!path_next) {
		ret2 = forall_func (&davp, ctx, data, true);
		switch (ret2) {
		case DTLV_ERR_SUCCESS:
		    break;
		case DTLV_FORALL_BREAK:
		    return (ctx->depth > 1) ? DTLV_FORALL_BREAK : DTLV_ERR_SUCCESS;
		default:
		    return DTLV_FORALL_FUNC;
		}
	    }

	    // restore context
	    d_ctxpath_trim (ctx);

	    ctx->datalen = datalen;
	    ctx->position = pos;
	}
    }

    if (ret == DTLV_END_OF_DATA) {
	return DTLV_ERR_SUCCESS;
    }
    else {
	return ret;
    }
}


typedef struct dtlv_tojson_s {
    char           *buf;
    bool            separate;
} dtlv_tojson_t;


LOCAL dtlv_errcode_t ICACHE_FLASH_ATTR
forall_avp_to_json (dtlv_davp_t * davp, const dtlv_ctx_t * ctx, const void *data, const bool group_exit)
{
    dtlv_tojson_t  *data2 = d_pointer_as (dtlv_tojson_t, data);

    if (group_exit) {
	*(data2->buf) = (davp->havpd.is_list ? ']' : '}');
	data2->buf++;
	data2->separate = true;
	return DTLV_ERR_SUCCESS;
    }

    if (data2->separate) {
	*(data2->buf) = ',';
	data2->buf++;
    }
    else {
	data2->separate = true;
    }

    if ((ctx->depth < 2) || (!ctx->path[ctx->depth - 2].is_list)) {
	// if parent not list
	if (davp->havpd.nscode.comp.namespace_id) {
	    data2->buf +=
		os_sprintf (data2->buf, "\"%u.%u\":", davp->havpd.nscode.comp.namespace_id,
			    davp->havpd.nscode.comp.code);
	}
	else {
	    data2->buf += os_sprintf (data2->buf, "\"%u\":", davp->havpd.nscode.comp.code);
	}
    }

    size_t          data_length = d_avp_data_length (davp->havpd.length);
    if ((davp->havpd.is_list) || (davp->havpd.datatype == DTLV_TYPE_OBJECT)) {
	*(data2->buf) = (davp->havpd.is_list ? '[' : '{');
	data2->buf++;
	if (data_length == 0) {
	    *(data2->buf) = (davp->havpd.is_list ? ']' : '}');
	    data2->buf++;
	}
	else {
	    data2->separate = false;
	}
    }
    else if (data_length > 0) {
	switch (davp->havpd.datatype) {
	case DTLV_TYPE_INTEGER:
	    {
		uint32          intval;
		dtlv_avp_get_uint (davp, &intval);
		data2->buf += os_sprintf (data2->buf, "%u", intval);
		break;
	    }
	case DTLV_TYPE_CHAR:
	    data2->buf += os_sprintf (data2->buf, "\"%s\"", davp->avp->data);
	    break;
	case DTLV_TYPE_OCTETS:
	    *(data2->buf) = '"';
	    data2->buf++;
	    data2->buf += buf2hex (data2->buf, davp->avp->data, data_length);
	    *(data2->buf) = '"';
	    data2->buf++;
	    break;
	default:
	    data2->buf += os_sprintf (data2->buf, "null");
	}
    }
    else
	data2->buf += os_sprintf (data2->buf, "null");

    return DTLV_ERR_SUCCESS;
}

dtlv_errcode_t  ICACHE_FLASH_ATTR
dtlv_decode_to_json (dtlv_ctx_t * ctx, char *buf)
{
    dtlv_errcode_t  ret = DTLV_ERR_SUCCESS;
    dtlv_tojson_t   data;
    data.buf = buf;
    data.separate = false;
    *(data.buf) = '{';
    data.buf++;
    ret = dtlv_decode_forall (ctx, (void *) &data, NULL, forall_avp_to_json);
    *(data.buf) = '}';
    data.buf++;
    *data.buf = 0x0;
    return ret;
}

dtlv_errcode_t  ICACHE_FLASH_ATTR
dtlv_avp_get_uint8 (dtlv_davp_t * davp, uint8 * data)
{
    if (davp->havpd.length != d_avp_full_length (sizeof (uint8)))
	return DTLV_AVP_INVALID_LEN;

    *data = *(uint8 *) davp->avp->data;
    return DTLV_ERR_SUCCESS;
}

dtlv_errcode_t  ICACHE_FLASH_ATTR
dtlv_avp_get_uint16 (dtlv_davp_t * davp, uint16 * data)
{
    if (davp->havpd.length != d_avp_full_length (sizeof (uint16)))
	return DTLV_AVP_INVALID_LEN;

    *data = (uint16) htobe16 (*(uint16 *) davp->avp->data);
    return DTLV_ERR_SUCCESS;
}

dtlv_errcode_t  ICACHE_FLASH_ATTR
dtlv_avp_get_uint32 (dtlv_davp_t * davp, uint32 * data)
{
    if (davp->havpd.length != d_avp_full_length (sizeof (uint32)))
	return DTLV_AVP_INVALID_LEN;

    *data = (uint32) htobe32 (*(uint32 *) davp->avp->data);
    return DTLV_ERR_SUCCESS;
}

dtlv_errcode_t  ICACHE_FLASH_ATTR
dtlv_avp_get_uint (dtlv_davp_t * davp, uint32 * data)
{
    switch (davp->havpd.length) {
    case d_avp_full_length (sizeof (uint8)):
	*data = (uint8) (*(uint8 *) davp->avp->data);
	break;
    case d_avp_full_length (sizeof (uint16)):
	*data = (uint16) htobe16 (*(uint16 *) davp->avp->data);
	break;
    case d_avp_full_length (sizeof (uint32)):
	*data = (uint32) htobe32 (*(uint32 *) davp->avp->data);
	break;
    default:
	return DTLV_AVP_INVALID_LEN;
    }
    return DTLV_ERR_SUCCESS;
}

dtlv_errcode_t  ICACHE_FLASH_ATTR
dtlv_avp_get_char (dtlv_davp_t * davp, char *data)
{
    if (davp->havpd.datatype != DTLV_TYPE_CHAR)
	return DTLV_AVP_INV_TYPE;

    os_memcpy (data, davp->avp->data, d_avp_data_length (davp->havpd.length) + 1);
    return DTLV_ERR_SUCCESS;
}
