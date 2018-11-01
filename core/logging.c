/* 
 * ESP8266 Logging
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

#include "sysinit.h"
#include "core/utils.h"
#include "core/ltime.h"
#include "core/logging.h"
#include "service/syslog.h"

#ifdef LOGGIGN_DEBUG_MODE
#define LOGGIGN_SEVERITY	LOG_DEBUG
#else
#ifndef LOGGIGN_SEVERITY
#define LOGGIGN_SEVERITY	LOG_INFO
#endif
#endif

#define LOGGIGN_LAST_ERROR_BUFFER_SIZE	84

LOCAL log_severity_t __log_severity = LOGGIGN_SEVERITY;
LOCAL char           __last_error[LOGGIGN_LAST_ERROR_BUFFER_SIZE] = "";
LOCAL char           __last_error_tmp[LOGGIGN_LAST_ERROR_BUFFER_SIZE] = "";

LOCAL const char *sz_severity_message[] = {
    "none ",
    "crit ",
    "error",
    "warn ",
    "info ",
    "debug",
};

LOCAL void      ICACHE_FLASH_ATTR
log_print_prefix (log_severity_t severity, const char *svc)
{
    lt_timestamp_t  ts2;
    lt_get_ctime (&ts2);

    os_printf ("[%u.%03d] [%s]", ts2.sec, ts2.usec / USEC_PER_MSEC, sz_severity_message[severity]);
    if (!d_char_is_end (svc)) {
	os_printf ("[%s] ", svc);
    }
}

LOCAL void      ICACHE_FLASH_ATTR
log_vprintf (const log_severity_t severity, const char *svc, const char *fmt, va_list al)
{
    if (severity <= LOG_WARNING)
        os_vsnprintf(__last_error, LOGGIGN_LAST_ERROR_BUFFER_SIZE-1, fmt, al);

    if (severity > __log_severity)
	return;

    log_print_prefix (severity, svc);
    os_vprintf (fmt, al);
    os_printf (LINE_END);

#ifndef DISABLE_SERVICE_SYSLOG
    if ((severity < LOG_DEBUG) && (syslog_available ())) {
	syslog_vprintf (severity, svc, fmt, al);
	//return;
    }
#endif
}

LOCAL void      ICACHE_FLASH_ATTR
log_vbprintf (const log_severity_t severity, const char *svc, const char *buf, size_t len, const char *fmt, va_list al)
{
    if (severity <= LOG_WARNING)
        os_vsnprintf(__last_error, LOGGIGN_LAST_ERROR_BUFFER_SIZE-1, fmt, al);

    if (severity > __log_severity)
	return;

    log_print_prefix (severity, svc);
    os_vprintf (fmt, al);
    os_printf (LINE_END);
    printb (buf, len);
    os_printf (LINE_END);

#ifndef DISABLE_SERVICE_SYSLOG
    if ((severity < LOG_DEBUG) && (syslog_available ())) {
	syslog_vbprintf (severity, svc, buf, len, fmt, al);
	//return;
    }
#endif
}

void            ICACHE_FLASH_ATTR
log_severity_set (log_severity_t severity)
{
    __log_severity = severity;
}

log_severity_t  ICACHE_FLASH_ATTR
log_severity_get (void)
{
    return __log_severity;
}

char *           ICACHE_FLASH_ATTR
get_last_error (void) {
    os_memcpy(__last_error_tmp, __last_error, LOGGIGN_LAST_ERROR_BUFFER_SIZE);
    return (char *) &__last_error_tmp;
}

void             ICACHE_FLASH_ATTR
reset_last_error (void) {
    os_memset(__last_error, 0, LOGGIGN_LAST_ERROR_BUFFER_SIZE);
}


/*
[public] write formatted string.
  - svc: service name
  - fmt: string format
  - ...: argumets for formated string
*/
void            ICACHE_FLASH_ATTR
log_printf (const log_severity_t severity, const char *svc, const char *fmt, ...)
{
    va_list         al;
    va_start (al, fmt);
    log_vprintf (severity, svc, fmt, al);
    va_end (al);
}


/*
[public] write formatted string with buffer dump.
  - svc: service name
  - buf: binary buffer
  - fmt: string format
  - ...: argumets for formated string
*/
void            ICACHE_FLASH_ATTR
log_bprintf (const log_severity_t severity, const char *svc, const char *buf, size_t len, const char *fmt, ...)
{
    va_list         al;
    va_start (al, fmt);
    log_vbprintf (severity, svc, buf, len, fmt, al);
    va_end (al);
}
