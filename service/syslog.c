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

/* 
   TODO: Escape of temporary buffer for formatting, write direct into slot instead of it
*/

#include "sysinit.h"
#include "core/logging.h"
#include "system/comavp.h"
#include "system/imdb.h"
#include "system/services.h"
#include "proto/dtlv.h"
#include "service/syslog.h"

#define SYSLOG_STORAGE_PAGES		1
#define SYSLOG_STORAGE_PAGE_BLOCKS	4
#define SYSLOG_MESSAGE_MAX_LEN		380
#define SYSLOG_IMDB_CLS_NAME		"syslog$"

typedef struct syslog_data_s {
    const svcs_resource_t * svcres;
    imdb_hndlr_t    hlogs;
    uint16          seq_no;
    char            buf[SYSLOG_MESSAGE_MAX_LEN + 1];	// +1 for null-terminated
} syslog_data_t;

LOCAL syslog_data_t *sdata = NULL;

svcs_errcode_t  ICACHE_FLASH_ATTR
syslog_on_cfgupd (dtlv_ctx_t * conf)
{
    log_severity_t  severity = SYSLOG_DEFAULT_SEVERITY;

    if (conf) {
        dtlv_seq_decode_begin (conf, SYSLOG_SERVICE_ID);
        dtlv_seq_decode_uint8 (SYSLOG_AVP_LOG_SEVERITY, (uint8 *) &severity);
        dtlv_seq_decode_end (conf);
    }

    log_severity_set (severity);

    return SVCS_ERR_SUCCESS;
}

svcs_errcode_t  ICACHE_FLASH_ATTR
syslog_on_start (const svcs_resource_t * svcres, dtlv_ctx_t * conf)
{
    if (sdata) {
	return SVCS_SERVICE_ERROR;
    }

    syslog_data_t  *tmp_sdata;
    d_svcs_check_imdb_error (imdb_clsobj_insert (svcres->hdata, (void **) &tmp_sdata, sizeof (syslog_data_t))
	);
    os_memset (tmp_sdata, 0, sizeof (syslog_data_t));

    tmp_sdata->svcres = svcres;
    imdb_class_def_t cdef =
	{ SYSLOG_IMDB_CLS_NAME, true, true, false, 0, SYSLOG_STORAGE_PAGES, SYSLOG_STORAGE_PAGE_BLOCKS,
SYSLOG_STORAGE_PAGE_BLOCKS, 0 };
    d_svcs_check_imdb_error (imdb_class_create (svcres->hmdb, &cdef, &(tmp_sdata->hlogs))
	);

    sdata = tmp_sdata;

    syslog_on_cfgupd (conf);

    return SVCS_ERR_SUCCESS;
}

svcs_errcode_t  ICACHE_FLASH_ATTR
syslog_on_stop ()
{
    if (!sdata) {
	return SVCS_NOT_RUN;
    }

    syslog_data_t  *tmp_sdata = sdata;
    sdata = NULL;
    d_svcs_check_imdb_error (imdb_class_destroy (tmp_sdata->hlogs)
	);

    d_svcs_check_imdb_error (imdb_clsobj_delete (tmp_sdata->svcres->hdata, tmp_sdata)
	);

    return SVCS_ERR_SUCCESS;
}

#define DTLV_MIN_BUFFER_FIXED_LENGTH	(6*4 + 4*4 + 20) + 8
#define LOG_FETCH_SIZE	10

LOCAL svcs_errcode_t ICACHE_FLASH_ATTR
syslog_on_msg_query (dtlv_ctx_t * msg_in, dtlv_ctx_t * msg_out)
{
    uint16          rec_no = 0xFFFF;
    if (msg_in) {
	dtlv_davp_t     davp;
	while (dtlv_avp_decode (msg_in, &davp) == DTLV_ERR_SUCCESS) {
	    if (!dtlv_check_namespace (&davp, SYSLOG_SERVICE_ID))
		break;

	    switch (davp.havpd.nscode.comp.code) {
	    case SYSLOG_AVP_LOG_RECNO:
		dtlv_avp_get_uint16 (&davp, &rec_no);
		break;
	    default:
		continue;
	    }
	}
    }


    dtlv_avp_t     *gavp;
    d_svcs_check_imdb_error (dtlv_avp_encode_list (msg_out, 0, SYSLOG_AVP_LOG_ENTRY, DTLV_TYPE_OBJECT, &gavp));

    imdb_hndlr_t    hcur;

    d_svcs_check_imdb_error (imdb_class_query (sdata->hlogs, PATH_RECYCLE_SCAN_REW, &hcur));

    void           *recs[LOG_FETCH_SIZE];
    uint16          rowcount;
    d_svcs_check_imdb_error (imdb_class_fetch (hcur, LOG_FETCH_SIZE, &rowcount, recs));

    bool            fcont = true;
    while (rowcount && fcont) {
	int             i;
	for (i = 0; i < rowcount; i++) {
	    syslog_logrec_t *rec = d_pointer_as (syslog_logrec_t, recs[i]);
	    //os_printf(" -- %u:%u %u - %u\n", i, rowcount, rec->rec_no, os_strlen(rec->vardata));

	    if (rec->rec_no < rec_no) {
		if (d_ctx_left_size (msg_out) < DTLV_MIN_BUFFER_FIXED_LENGTH + os_strlen (rec->vardata)) {
		    goto end_of_data;
		}

		dtlv_avp_t     *gavp_in;
		d_svcs_check_imdb_error (dtlv_avp_encode_grouping (msg_out, 0, SYSLOG_AVP_LOG_ENTRY, &gavp_in) ||
					 dtlv_avp_encode_uint16 (msg_out, SYSLOG_AVP_LOG_RECNO, rec->rec_no) ||
					 dtlv_avp_encode_uint8 (msg_out, SYSLOG_AVP_LOG_SEVERITY, rec->severity) ||
					 dtlv_avp_encode_uint32 (msg_out, SYSLOG_AVP_LOG_TIMESTAMP,
								 lt_time (&rec->rec_ctime))
					 || dtlv_avp_encode_nchar (msg_out, COMMON_AVP_SERVICE_NAME,
								   sizeof (service_name_t), rec->service)
					 || dtlv_avp_encode_char (msg_out, SYSLOG_AVP_LOG_MESSAGE, rec->vardata)
					 || dtlv_avp_encode_group_done (msg_out, gavp_in));

	    }
	}

	d_svcs_check_imdb_error (imdb_class_fetch (hcur, LOG_FETCH_SIZE, &rowcount, recs));
    }

  end_of_data:
    imdb_class_close (hcur);

    d_svcs_check_imdb_error (dtlv_avp_encode_group_done (msg_out, gavp));

    return SVCS_ERR_SUCCESS;
}

svcs_errcode_t  ICACHE_FLASH_ATTR
syslog_on_message (service_ident_t orig_id, service_msgtype_t msgtype, void *ctxdata, dtlv_ctx_t * msg_in,
		   dtlv_ctx_t * msg_out)
{
    svcs_errcode_t  res = SVCS_ERR_SUCCESS;
    switch (msgtype) {
    case SYSLOG_MSGTYPE_QUERY:
	res = syslog_on_msg_query (msg_in, msg_out);
	break;
    default:
	res = SVCS_MSGTYPE_INVALID;
    }

    return res;
}

svcs_errcode_t ICACHE_FLASH_ATTR
syslog_query(imdb_hndlr_t* hcur) {
    if (!sdata) {
	d_log_dprintf (SYSLOG_SERVICE_NAME, "not available");
	return SVCS_NOT_RUN;
    }
 
    imdb_errcode_t ret = imdb_class_query(sdata->hlogs, true, hcur);
    d_svcs_check_imdb_error(ret);

    return SVCS_ERR_SUCCESS;
}

svcs_errcode_t  ICACHE_FLASH_ATTR
syslog_write (const log_severity_t severity, const char *svc, size_t * length, char **buf)
{
    *buf = NULL;
    if (!sdata) {
	d_log_dprintf (SYSLOG_SERVICE_NAME, "not available");
	return SVCS_NOT_RUN;
    }

    *length = MIN (*length, SYSLOG_MESSAGE_MAX_LEN);
    if (*length == 0) {
	return SVCS_ERR_SUCCESS;
    }

    sdata->seq_no++;
    syslog_logrec_t *rec;
    d_svcs_check_imdb_error (imdb_clsobj_insert (sdata->hlogs, (void **) &rec, *length + sizeof (syslog_logrec_t))
	);

    rec->rec_ctime = lt_ctime ();
    rec->rec_no = sdata->seq_no;
    rec->severity = severity;

    size_t          len = os_strnlen (svc, sizeof (service_name_t));
    os_memcpy (rec->service, svc, len);
    if (len < sizeof (service_name_t))
	rec->service[len] = '\0';

    *buf = (char *) rec->vardata;

    return SVCS_ERR_SUCCESS;
}

svcs_errcode_t  ICACHE_FLASH_ATTR
syslog_write_msg (const log_severity_t severity, const char *svc, char *msg)
{
    size_t          length = os_strlen (msg);
    char           *buf = NULL;
    svcs_errcode_t  ret = syslog_write (severity, svc, &length, &buf);
    if (ret) {
	return ret;
    }

    os_memcpy (buf, msg, length + 1);	// +1 for null-terminate
    buf[length] = '\0';		// null-terminate

    return SVCS_ERR_SUCCESS;
}

svcs_errcode_t  ICACHE_FLASH_ATTR
syslog_vprintf (const log_severity_t severity, const char *svc, const char *fmt, va_list al)
{
    size_t          length = os_vsnprintf (sdata->buf, SYSLOG_MESSAGE_MAX_LEN, fmt, al);
    sdata->buf[length] = '\0';	// make null-terminated
    return syslog_write_msg (severity, svc, sdata->buf);
}

svcs_errcode_t  ICACHE_FLASH_ATTR
syslog_vbprintf (const log_severity_t severity, const char *svc, const char *buf, size_t len, const char *fmt,
		 va_list al)
{
    size_t          length = os_vsnprintf (sdata->buf, SYSLOG_MESSAGE_MAX_LEN, fmt, al);
    size_t          lenadd = LINE_END_STRLEN;
    os_memcpy ((char *) &sdata->buf[length], LINE_END, lenadd);
    length += lenadd;

    lenadd = sprintb ((char *) &sdata->buf[length], SYSLOG_MESSAGE_MAX_LEN - length, buf, len);
    length += lenadd;
    sdata->buf[length] = '\0';	// make null-terminated

    return syslog_write_msg (severity, svc, sdata->buf);
}


svcs_errcode_t  ICACHE_FLASH_ATTR
syslog_service_install (void)
{
    svcs_service_def_t sdef;
    os_memset (&sdef, 0, sizeof (svcs_service_def_t));
    sdef.enabled = true;
    sdef.on_start = syslog_on_start;
    sdef.on_stop = syslog_on_stop;
    sdef.on_message = syslog_on_message;
    sdef.on_cfgupd = syslog_on_cfgupd;
    return svcctl_service_install (SYSLOG_SERVICE_ID, SYSLOG_SERVICE_NAME, &sdef);
}

svcs_errcode_t  ICACHE_FLASH_ATTR
syslog_service_uninstall (void)
{
    return svcctl_service_uninstall (SYSLOG_SERVICE_NAME);
}

bool            ICACHE_FLASH_ATTR
syslog_available (void)
{
    return (sdata != NULL);
}
