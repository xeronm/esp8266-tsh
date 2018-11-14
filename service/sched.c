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

#include <string.h>
#include "sysinit.h"
#include "core/utils.h"
#include "core/logging.h"
#include "core/system.h"
#include "system/services.h"
#include "system/comavp.h"
#include "service/sched.h"
#include "service/lsh.h"

#define SCHED_IMDB_CLS_ENTRY		"sched$entry"
#define SCHED_ENTRY_STORAGE_PAGES	1
#define SCHED_ENTRY_STORAGE_PAGE_BLOCKS	1

#define SCHED_ENTRYIDX_BUFFER_SIZE	512

#define SCHED_NEXT_CTIME_NONE		0x8FFFFFFF

#define SCHED_MAX_TIMEOUT_SEC		3600

typedef struct sched_data_s {
    const svcs_resource_t * svcres;
    imdb_hndlr_t    hentry;	// entry storage
    // Fixme: should make separate index segment in imdb
#ifdef ARCH_XTENSA
    os_timer_t      next_timer;
#endif
    os_time_t       next_ctime;
} sched_data_t;

LOCAL sched_data_t *sdata = NULL;

typedef enum parse_state_e {
    PS_NONE = 0,
    PS_NUMBER,
    PS_RANGE,
    PS_MASK,
    PS_WAIT_COMA,
    PS_WAIT_END,
    PS_END_OF_ITEM,
    PS_END_OF_EXPR,
} parse_state_t;

LOCAL const char *sz_sched_error[] RODATA = {
    "",
    "internal error",
    "memory allocation error",
    "parsing error",
    "\"%s\" already exist",
    "\"%s\" not exist",
    "\"%s\" stmt \"%s\" not exist",
    "\"%s\" stmt \"%s\" error: %d",
};

#define d_check_init()	    \
    if (! sdata) { \
        d_log_eprintf (SCHED_SERVICE_NAME, sz_sched_error[SCHED_INTERNAL_ERROR]); \
        return SCHED_INTERNAL_ERROR; \
    } \



LOCAL parse_state_t ICACHE_FLASH_ATTR
parse_tsmask (uint8 * buf, unsigned int vmin, unsigned int vmax, const char **szstr)
{
    const char     *ptr = *szstr;
    unsigned int    prev_num;
    unsigned int    last_num;
    parse_state_t   state = PS_NONE;
    parse_state_t   prev_state = PS_NONE;

    d_bitbuf_rclear (buf, vmin, vmax);
    d_skip_space (ptr);
    while (*ptr) {
	parse_state_t   _state = state;

	while (true) {
	    if (d_char_is_end (ptr)) {
		state = (state == PS_WAIT_END) ? PS_NONE : PS_END_OF_EXPR;
		goto parse_end;
	    }
	    if (state == PS_WAIT_END)
		goto parse_end;

	    if (*ptr == ',') {
		state = PS_END_OF_ITEM;
		break;
	    }
	    if (state == PS_WAIT_COMA)
		goto parse_end;

	    if ((*ptr == '-') || (*ptr == '/')) {
		if ( (state != PS_NUMBER) || ((*ptr == '-') && (last_num == ~0)) )
		    goto parse_end;
		else
		    state = (*ptr == '-') ? PS_RANGE : PS_MASK;

		break;
	    }

	    if (d_char_is_digit (ptr) || (*ptr == '*')) {
		if (*ptr == '*')
		    last_num = ~0;
		else {
		    if (parse_uint (&ptr, &last_num)) ptr--;
                    last_num = MIN(vmax, MAX(vmin, last_num));
		}

		if (state == PS_NONE)
		    state = PS_NUMBER;
		else {
		    prev_num = last_num;
		    state = PS_WAIT_COMA;
		}
	    }
	    else {
		goto parse_end;
	    }

	    break;
	}
	if ((_state != state) && (_state != PS_WAIT_COMA) && (_state != PS_WAIT_END)) {
	    prev_state = _state;
	}

	ptr++;
	if (d_char_is_end (ptr))
	    state = PS_END_OF_EXPR;

	if ((state == PS_END_OF_EXPR) || (state == PS_END_OF_ITEM)) {
	    switch (prev_state) {
	    case PS_RANGE:
		d_bitbuf_rset (buf, prev_num - vmin, last_num - vmin);
		break;
	    case PS_MASK:
	        {
		    int             i;
		    if (prev_num == ~0) {
		        prev_num = vmin;
		    }
		    for (i = prev_num - vmin; i <= vmax - vmin; i += last_num) {
		        d_bitbuf_set (buf, i);
		    }
		}
		break;
	    default:
		if (last_num == ~0) {
		    d_bitbuf_rset (buf, 0, vmax - vmin);
		}
		else {
		    d_bitbuf_set (buf, last_num - vmin);
		}
	    }

	    if (state == PS_END_OF_EXPR) {
		state = PS_NONE;
		break;
	    }	
	    state = (last_num == ~0) ? PS_WAIT_END : PS_NONE;
	}

    }

parse_end:
    *szstr = ptr;
    return state;
}

/*
 * [private] Parse scheduler entry from string representation
 *  - szentry: entry string representation
 *  - entry: result entry
 */
LOCAL bool      ICACHE_FLASH_ATTR
parse_tsentry (const char *szentry, tsentry_t * entry)
{
    os_memset(entry, 0, sizeof(tsentry_t));

    const char     *ptr = szentry;
    d_skip_space (ptr);

    if (*ptr == '@') {
        entry->flag_boot = true;
        ptr++;
        d_skip_space (ptr);
    }

    parse_state_t res = parse_tsmask (entry->minpart, 0, SCHEDULE_MINUTE_PARTS-1, &ptr) ||
                        parse_tsmask (entry->minute, 0, MIN_PER_HOUR-1, &ptr) ||
                        parse_tsmask (entry->hour, 0, HOUR_PER_DAY-1, &ptr) ||
                        parse_tsmask (entry->dom, 1, DAY_PER_MONTH, &ptr) ||
                        parse_tsmask (entry->dow, 0, DAY_PER_WEEK-1, &ptr);
    if (res != PS_NONE) {
        d_log_eprintf (SCHED_SERVICE_NAME, "tsentry invalid state:%u, pos:%u", res, ptr - szentry);
        return false;
    }

    d_skip_space (ptr);

    if (*ptr) {
        d_log_eprintf (SCHED_SERVICE_NAME, "tsentry unexpected token pos:%u", ptr - szentry);
        return false;
    }

    return true;
}

LOCAL void      ICACHE_FLASH_ATTR
entry_set_next_time (sched_entry_t * entry)
{
    os_time_t       curr_ctime = lt_ctime ();
    os_time_t       posix_time = lt_time (&curr_ctime);
    ltm_t           _tm;
    lt_localtime(posix_time, &_tm, false);

    //d_log_dbprintf("-- 0 ", &entry->ts, sizeof(tsentry_t), "dump");

    uint8           minpart = _tm.tm_sec/SCHEDULE_MINUTE_PART_SECS + 1;
    while (!d_bitbuf_get (entry->ts.minpart, minpart % SCHEDULE_MINUTE_PARTS))
        minpart++;
    lt_add_secs(&_tm, minpart*SCHEDULE_MINUTE_PART_SECS - _tm.tm_sec);

    uint8           tm_min = _tm.tm_min;
    while (!d_bitbuf_get (entry->ts.minute, tm_min % MIN_PER_HOUR))
        tm_min++;
    if (tm_min > _tm.tm_min)
        lt_add_mins(&_tm, tm_min - _tm.tm_min);

    uint8           tm_hour = _tm.tm_hour;
    while (!d_bitbuf_get (entry->ts.hour, tm_hour % HOUR_PER_DAY))
        tm_hour++;
    if (tm_hour > _tm.tm_hour)
        lt_add_hours(&_tm, tm_hour - _tm.tm_hour);

    uint8 tm_wday = _tm.tm_wday;
    while (!d_bitbuf_get (entry->ts.dow, tm_wday % DAY_PER_WEEK))
        tm_wday++;

    uint8 tm_mday = _tm.tm_mday;
    while (!d_bitbuf_get (entry->ts.dom, (tm_mday - 1) % 31))
        tm_mday++;

    if ((tm_mday > _tm.tm_mday) && (_tm.tm_wday > tm_wday)) {
        lt_add_days(&_tm, MIN(tm_wday - _tm.tm_wday, tm_mday - _tm.tm_mday));
    } else if (_tm.tm_wday > tm_wday)
        lt_add_days(&_tm, tm_wday - _tm.tm_wday);
    else if (tm_mday > _tm.tm_mday)
        lt_add_days(&_tm, tm_mday - _tm.tm_mday);

    entry->next_ctime = curr_ctime + lt_mktime (&_tm, false) - posix_time;
}


LOCAL sched_errcode_t ICACHE_FLASH_ATTR
entry_run (sched_entry_t * entry) 
{
    entry->state = SCHED_ENTRY_STATE_RUNNING;

    entry->last_ctime = lt_ctime ();
    entry->run_count++;

    sched_errcode_t res = SCHED_ERR_SUCCESS;
    size_t          varlen = 0;
    char           *vardata = NULL;
    dtlv_davp_t     davp;
    dtlv_ctx_t      vd_ctx;

    dtlv_ctx_init_decode (&vd_ctx, entry->vardata, entry->varlen);

    if ( dtlv_avp_decode (&vd_ctx, &davp) != DTLV_ERR_SUCCESS) {
        res = SCHED_INTERNAL_ERROR;
        goto run_fail;
    }
    if (davp.havpd.nscode.comp.code == SCHED_AVP_STMT_ARGUMENTS) {
        vardata = davp.avp->data;
        varlen = d_avp_data_length(davp.havpd.length);
    }

    sh_hndlr_t  hstmt;
    sh_errcode_t rres = stmt_get2 ( &entry->stmt_name, &hstmt);

    switch (rres) {
    case SH_ERR_SUCCESS:
        break;
    case SH_STMT_NOT_EXISTS:
        d_log_eprintf (SCHED_SERVICE_NAME, sz_sched_error[SCHED_STMT_NOTEXISTS], entry->entry_name, entry->stmt_name);
        res = SCHED_STMT_NOTEXISTS;
        goto run_fail;
    default:
        d_log_eprintf (SCHED_SERVICE_NAME, sz_sched_error[SCHED_STMT_ERROR], entry->entry_name, entry->stmt_name, rres);
        res = SCHED_STMT_ERROR;
        goto run_fail;
    }

    entry->state = SCHED_ENTRY_STATE_NONE;
    return res;

run_fail:
    entry->state = SCHED_ENTRY_STATE_FAILED;
    entry->fail_count++;
    return res;
}


LOCAL void      next_timer_timeout (void *args);

LOCAL void      ICACHE_FLASH_ATTR
next_timer_set (sched_entry_t *entry)
{
#ifdef ARCH_XTENSA
    os_timer_disarm (&sdata->next_timer);
    if (! entry) {
        sdata->next_ctime = SCHED_NEXT_CTIME_NONE;
        return;
    }

    sdata->next_ctime = entry->next_ctime;

    os_time_t offset = lt_ctime ();
    if ((entry->next_ctime != SCHED_NEXT_CTIME_NONE) && (entry->next_ctime < offset + SCHED_MAX_TIMEOUT_SEC))
    {
        os_timer_setfn (&sdata->next_timer, next_timer_timeout, entry);
    
        offset = (entry->next_ctime > offset) ? (entry->next_ctime - offset) * MSEC_PER_SEC : 100;
    
        os_timer_arm (&sdata->next_timer, offset, false);
    } 
    else {
        os_timer_setfn (&sdata->next_timer, next_timer_timeout, NULL);
        os_timer_arm (&sdata->next_timer, SCHED_MAX_TIMEOUT_SEC*MSEC_PER_SEC, false);
    }
#endif
}

typedef struct sched_setnext_ctx_s {
    bool             frenew;
    os_time_t        curr_ctime;
    sched_entry_t   *entry;
} sched_setnext_ctx_t;

LOCAL imdb_errcode_t ICACHE_FLASH_ATTR
sched_forall_next_time (void *ptr, void *data)
{
    sched_entry_t *entry = d_pointer_as (sched_entry_t, ptr);
    sched_setnext_ctx_t *ctx = d_pointer_as (sched_setnext_ctx_t, data);

    if (ctx->frenew || (entry->next_ctime == SCHED_NEXT_CTIME_NONE) || 
       ((ctx->curr_ctime > entry->next_ctime) && (entry->next_ctime <= entry->last_ctime)) )
        entry_set_next_time(entry);

    if (! ctx->entry || (ctx->entry->next_ctime > entry->next_ctime))
        ctx->entry = entry;

    return IMDB_ERR_SUCCESS;
}

LOCAL sched_errcode_t ICACHE_FLASH_ATTR
sched_setall_next_time (bool frenew) 
{
    sched_setnext_ctx_t ctx;
    ctx.frenew = frenew;
    ctx.entry = NULL;
    ctx.curr_ctime = lt_ctime ();

    d_sched_check_imdb_error (imdb_class_forall (sdata->hentry, (void *) &ctx, sched_forall_next_time));

    next_timer_set(ctx.entry);

    return SCHED_ERR_SUCCESS;
}

LOCAL void      ICACHE_FLASH_ATTR
next_timer_timeout (void *args)
{
    sdata->next_ctime = SCHED_NEXT_CTIME_NONE;

    if (args) {
        sched_entry_t *entry = d_pointer_as (sched_entry_t, args);
        entry->next_ctime = SCHED_NEXT_CTIME_NONE;
        entry_run (entry);
    }

    sched_setall_next_time (false);
}

typedef struct sched_find_ctx_s {
    const char       *entry_name;
    sched_entry_t    *entry;
} sched_find_ctx_t;

/*
 * [private] imdb forall callback
 */
LOCAL imdb_errcode_t ICACHE_FLASH_ATTR
sched_forall_find (void *ptr, void *data)
{
    sched_entry_t *entry = d_pointer_as (sched_entry_t, ptr);
    sched_find_ctx_t *find_ctx = d_pointer_as (sched_find_ctx_t, data);
    if (os_strncmp (entry->entry_name, find_ctx->entry_name, sizeof (entry_name_t)) == 0) {
	find_ctx->entry = entry;
	return IMDB_CURSOR_BREAK;
    }
    return IMDB_ERR_SUCCESS;
}


/*
 * [public] find task by name
 * - entry_name: entry name (safe to use char* and entry_name_t*)
 * - return: the pointer on function entry
 */
sched_errcode_t ICACHE_FLASH_ATTR
sched_entry_get (const char * entry_name, const sched_entry_t ** entry)
{
    sched_find_ctx_t find_ctx;
    os_memset (&find_ctx, 0, sizeof (sched_find_ctx_t));
    find_ctx.entry_name = entry_name;

    d_sched_check_imdb_error (imdb_class_forall (sdata->hentry, (void *) &find_ctx, sched_forall_find));

    *entry = find_ctx.entry;
    return (*entry) ? SCHED_ERR_SUCCESS : SCHED_ENTRY_NOTEXISTS;
}


sched_errcode_t ICACHE_FLASH_ATTR
sched_entry_add (const entry_name_t * entry_name, const char * sztsentry, const sh_stmt_name_t * stmt_name, const char * vardata, size_t varlen)
{
    d_check_init();

    sched_entry_t    *entry;
    if (sched_entry_get ( (const char *)entry_name, (const sched_entry_t **) &entry) == SCHED_ERR_SUCCESS) {
	d_log_wprintf (SCHED_SERVICE_NAME, sz_sched_error[SCHED_ENTRY_EXISTS], entry_name);
	return SCHED_ENTRY_EXISTS;
    }

    tsentry_t       ts_entry;
    if (!parse_tsentry (sztsentry, &ts_entry)) {
	return SCHED_PARSE_ERROR;
    }

    size_t          length = sizeof (sched_entry_t); 

    // add extra length for Schedule-String AVP
    size_t          vd_len = d_avp_full_length (d_align (os_strlen(sztsentry) + 1)); 
    // add vardata length
    if (vardata && varlen)
        vd_len += d_avp_full_length (d_align (varlen));

    length += vd_len;

    d_sched_check_imdb_error (imdb_clsobj_insert (sdata->hentry, (void **) &entry, length));
    os_memset (entry, 0, length);

    os_memcpy (&entry->ts, &ts_entry, sizeof (tsentry_t));
    os_memcpy (entry->stmt_name, stmt_name, os_strnlen ((char *)stmt_name, sizeof (sh_stmt_name_t)) );
    os_memcpy (entry->entry_name, entry_name, os_strnlen ((char *)entry_name, sizeof (entry_name_t)) );

    dtlv_ctx_t vd_ctx;
    d_sched_check_dtlv_error (dtlv_ctx_init_encode (&vd_ctx, entry->vardata, vd_len) || // MUST be the first AVP
                              dtlv_avp_encode_char (&vd_ctx, SCHED_AVP_SCHEDULE_STRING, sztsentry));

    if (vardata && varlen) {
	dtlv_avp_t     *gavp;
        d_sched_check_dtlv_error( dtlv_avp_encode (&vd_ctx, 0, SCHED_AVP_STMT_ARGUMENTS, DTLV_TYPE_OBJECT, varlen, false, &gavp));
        os_memcpy (gavp->data, vardata, varlen);
    }
    entry->varlen = vd_ctx.datalen;

    entry_set_next_time(entry);

    ltm_t           _tm;
    lt_localtime (lt_time (&entry->next_ctime), &_tm, false);
    d_log_iprintf (SCHED_SERVICE_NAME, "add \"%s\", next " TMSTR_TZ, entry_name, TM2STR_TZ (&_tm));

    if (sdata->next_ctime > entry->next_ctime)
        next_timer_set(entry);

    return SCHED_ERR_SUCCESS;
}

sched_errcode_t ICACHE_FLASH_ATTR
sched_entry_remove (const entry_name_t * entry_name)
{
    d_check_init();

    sched_entry_t *entry;
    sched_errcode_t   res = sched_entry_get ( (const char *)entry_name, (const sched_entry_t **) &entry);
    if (res == SCHED_ENTRY_NOTEXISTS) {
	d_log_wprintf (SCHED_SERVICE_NAME, sz_sched_error[res], entry_name);
	return res;
    } else if (res != SCHED_ERR_SUCCESS) {
        return res;
    }

    d_log_iprintf (SCHED_SERVICE_NAME, "remove \"%s\"", entry_name);
    d_sched_check_imdb_error (imdb_clsobj_delete (sdata->hentry, entry)
	);

    sched_setall_next_time (false);

    return SCHED_ERR_SUCCESS;
}

sched_errcode_t ICACHE_FLASH_ATTR
sched_entry_run (const entry_name_t * entry_name)
{
    d_check_init();

    sched_entry_t    *entry;
    sched_errcode_t   res = sched_entry_get ( (const char *)entry_name, (const sched_entry_t **) &entry);
    if (res == SCHED_ENTRY_NOTEXISTS) {
	d_log_wprintf (SCHED_SERVICE_NAME, sz_sched_error[res], entry_name);
	return res;
    } else if (res != SCHED_ERR_SUCCESS) {
        return res;
    }

    return entry_run (entry);
}

LOCAL svcs_errcode_t ICACHE_FLASH_ATTR
sched_on_msg_info (dtlv_ctx_t * msg_out)
{
    dtlv_avp_t     *gavp;

    d_svcs_check_imdb_error ( ((sdata->next_ctime != SCHED_NEXT_CTIME_NONE) ? dtlv_avp_encode_uint32 (msg_out, SCHED_AVP_NEXT_RUN_TIME, lt_time (&sdata->next_ctime)) : 0)
                              || dtlv_avp_encode_list (msg_out, 0, SCHED_AVP_ENTRY, DTLV_TYPE_OBJECT, &gavp));


    imdb_hndlr_t    hcur;

    d_svcs_check_imdb_error (imdb_class_query (sdata->hentry, PATH_NONE, &hcur));

    sched_entry_t  *entries[10];
    uint16          rowcount;
    d_svcs_check_imdb_error (imdb_class_fetch (hcur, 10, &rowcount, (void **)entries));

    bool            fcont = true;
    while (rowcount && fcont) {
	int             i;
	for (i = 0; i < rowcount; i++) {

	    dtlv_avp_t     *gavp_in;
	    d_svcs_check_imdb_error (dtlv_avp_encode_grouping (msg_out, 0, SCHED_AVP_ENTRY, &gavp_in)
				     || dtlv_avp_encode_nchar (msg_out, SCHED_AVP_ENTRY_NAME, sizeof (entry_name_t), entries[i]->entry_name)
				     || dtlv_avp_encode_nchar (msg_out, SCHED_AVP_STMT_NAME, sizeof (sh_stmt_name_t), entries[i]->stmt_name)
				     || dtlv_raw_encode (msg_out, entries[i]->vardata, entries[i]->varlen)
				     || dtlv_avp_encode_uint8 (msg_out, SCHED_AVP_ENTRY_STATE, entries[i]->state)
				     || ((entries[i]->last_ctime) ? dtlv_avp_encode_uint32 (msg_out, SCHED_AVP_LAST_RUN_TIME, lt_time (&entries[i]->last_ctime)) : 0)
				     || ((entries[i]->next_ctime != SCHED_NEXT_CTIME_NONE) ? dtlv_avp_encode_uint32 (msg_out, SCHED_AVP_NEXT_RUN_TIME, lt_time (&entries[i]->next_ctime)) : 0)
				     || dtlv_avp_encode_uint16 (msg_out, SCHED_AVP_RUN_COUNT, entries[i]->run_count)
				     || dtlv_avp_encode_uint16 (msg_out, SCHED_AVP_FAIL_COUNT, entries[i]->fail_count)
				     || dtlv_avp_encode_group_done (msg_out, gavp_in));
	}

        d_svcs_check_imdb_error (imdb_class_fetch (hcur, 10, &rowcount, (void **)entries));
    }

    d_svcs_check_imdb_error (dtlv_avp_encode_group_done (msg_out, gavp));

    return SVCS_ERR_SUCCESS;
}

svcs_errcode_t  ICACHE_FLASH_ATTR
sched_on_message (service_ident_t orig_id,
		   service_msgtype_t msgtype, void *ctxdata, dtlv_ctx_t * msg_in, dtlv_ctx_t * msg_out)
{
    svcs_errcode_t  res = SVCS_ERR_SUCCESS;
    switch (msgtype) {
    case SVCS_MSGTYPE_INFO:
	res = sched_on_msg_info (msg_out);
	break;
    case SVCS_MSGTYPE_ADJTIME:
        sched_setall_next_time(true);
        break;
    case SCHED_MSGTYPE_ENTRY_ADD:
        if (!msg_in)
	    return SVCS_INVALID_MESSAGE;
        {
	    dtlv_davp_t     davp;
	    const entry_name_t *entry_name = NULL;
	    const char     *sztsentry = NULL;
            const sh_stmt_name_t *stmt_name = NULL;
            const char     *vardata;
            size_t          varlen = 0;

	    while (dtlv_avp_decode (msg_in, &davp) == DTLV_ERR_SUCCESS) {
		if (!dtlv_check_namespace (&davp, SCHED_SERVICE_ID))
		    break;
		switch (davp.havpd.nscode.comp.code) {
		case SCHED_AVP_ENTRY_NAME:
		    entry_name = d_pointer_as(const entry_name_t, davp.avp->data);
		    break;
		case SCHED_AVP_STMT_NAME:
		    stmt_name = d_pointer_as(const sh_stmt_name_t, davp.avp->data);
		    break;
		case SCHED_AVP_SCHEDULE_STRING:
		    sztsentry = davp.avp->data;
		    break;
		case SCHED_AVP_STMT_ARGUMENTS:
                    varlen = d_avp_data_length(davp.havpd.length);
		    vardata = davp.avp->data;
		    break;
		}
	    }
	    if (!entry_name || !stmt_name || !sztsentry)
		return SVCS_INVALID_MESSAGE;

	    sched_errcode_t sres = sched_entry_add (entry_name, sztsentry, stmt_name, vardata, varlen);
	    if (sres != SCHED_ERR_SUCCESS) {
                d_svcs_check_svcs_error (encode_service_result_ext (msg_out, sres));
	    }
	}
        break;
    case SCHED_MSGTYPE_ENTRY_REMOVE:
    case SCHED_MSGTYPE_ENTRY_RUN:
        if (!msg_in)
	    return SVCS_INVALID_MESSAGE;
        {
	    dtlv_davp_t     davp;
	    const entry_name_t *entry_name = NULL;
	    while (dtlv_avp_decode (msg_in, &davp) == DTLV_ERR_SUCCESS) {
		if (!dtlv_check_namespace (&davp, SCHED_SERVICE_ID))
		    break;
		switch (davp.havpd.nscode.comp.code) {
		case SCHED_AVP_ENTRY_NAME:
		    entry_name = d_pointer_as (const entry_name_t, davp.avp->data);
		    break;
		}
	    }
	    if (!entry_name)
		return SVCS_INVALID_MESSAGE;

	    sched_errcode_t sres = SCHED_ERR_SUCCESS;
	    if (msgtype == SCHED_MSGTYPE_ENTRY_REMOVE)
		sres = sched_entry_remove (entry_name);
	    else	
		sres = sched_entry_run (entry_name);

	    if (sres != SCHED_ERR_SUCCESS) {
                d_svcs_check_svcs_error (encode_service_result_ext (msg_out, sres));
	    }
	}
        break;
    default:
	res = SVCS_MSGTYPE_INVALID;
    }

    return res;
}

svcs_errcode_t  ICACHE_FLASH_ATTR
sched_on_cfgupd (dtlv_ctx_t * conf)
{
    return SVCS_ERR_SUCCESS;
}


svcs_errcode_t  ICACHE_FLASH_ATTR
sched_service_install (void)
{
    svcs_service_def_t sdef;
    os_memset (&sdef, 0, sizeof (svcs_service_def_t));
    sdef.enabled = true;
    sdef.on_cfgupd = sched_on_cfgupd;
    sdef.on_message = sched_on_message;
    sdef.on_start = sched_on_start;
    sdef.on_stop = sched_on_stop;
    return svcctl_service_install (SCHED_SERVICE_ID, SCHED_SERVICE_NAME, &sdef);
}

svcs_errcode_t  ICACHE_FLASH_ATTR
sched_service_uninstall (void)
{
    return svcctl_service_uninstall (SCHED_SERVICE_NAME);
}

svcs_errcode_t  ICACHE_FLASH_ATTR
sched_on_start (const svcs_resource_t * svcres, dtlv_ctx_t * conf)
{
    if (sdata)
	return SVCS_SERVICE_ERROR;

    sched_data_t    *tmp_sdata;
    d_svcs_check_imdb_error (imdb_clsobj_insert (svcres->hdata, (void **) &tmp_sdata, sizeof (sched_data_t))
	);
    os_memset (tmp_sdata, 0, sizeof (sched_data_t));

    tmp_sdata->svcres = svcres;
    imdb_class_def_t cdef =
	{ SCHED_IMDB_CLS_ENTRY, false, true, false, 0, SCHED_ENTRY_STORAGE_PAGES, SCHED_ENTRY_STORAGE_PAGE_BLOCKS, sizeof (sched_entry_t) };
    d_svcs_check_imdb_error (imdb_class_create (svcres->hmdb, &cdef, &(tmp_sdata->hentry))
	);

    sdata = tmp_sdata;

    sdata->next_ctime = SCHED_NEXT_CTIME_NONE;
#ifdef ARCH_XTENSA
    os_timer_disarm (&sdata->next_timer);
    os_timer_setfn (&sdata->next_timer, next_timer_timeout, NULL);
#endif
    return SVCS_ERR_SUCCESS;
}

svcs_errcode_t  ICACHE_FLASH_ATTR
sched_on_stop (void)
{
    if (!sdata)
	return SVCS_NOT_RUN;

    sched_data_t     *tmp_sdata = sdata;
    sdata = NULL;
    d_svcs_check_imdb_error (imdb_class_destroy (tmp_sdata->hentry)
	);

    d_svcs_check_imdb_error (imdb_clsobj_delete (tmp_sdata->svcres->hdata, tmp_sdata)
	);

    return SVCS_ERR_SUCCESS;
}
