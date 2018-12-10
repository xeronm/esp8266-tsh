/* 
 * ESP8266 Things Shell Logging
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
 * ESP8266 Things Shell is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ESP8266 Things Shell.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#ifndef _LOGGING_H_
#define _LOGGING_H_ 1

#include "sysinit.h"

#define	MAIN_SERVICE_NAME	"main"

typedef enum log_severity_s {
    LOG_NONE = 0,
    LOG_CRITICAL = 1,
    LOG_ERROR = 2,
    LOG_WARNING = 3,
    LOG_INFO = 4,
    LOG_DEBUG = 5,
} log_severity_t;

// used for string messages logging, writes timestamps and service_name
void            log_printf (const log_severity_t severity, const char *svc, const char *fmt, ...);

// used for buffer logging, writes buffer as hex dump, writes timestamps and service_name
void            log_bprintf (const log_severity_t severity, const char *svc, const char *buf, size_t len,
                             const char *fmt, ...);

void            log_severity_set (log_severity_t severity);
log_severity_t  log_severity_get (void);

char           *get_last_error (void);
void            reset_last_error (void);

#ifdef LOGGING_DEBUG
#define d_log_dprintf(svc, fmt, ...)			log_printf(LOG_DEBUG, svc, fmt, ##__VA_ARGS__)
#define d_log_dbprintf(svc, buf, len, fmt, ...)	log_bprintf(LOG_DEBUG, svc, buf, len, fmt, ##__VA_ARGS__)
#else
#define d_log_dprintf(svc, fmt, ...)
#define d_log_dbprintf(svc, buf, len, fmt, ...)
#endif

#define d_log_iprintf(svc, fmt, ...)			log_printf(LOG_INFO, svc, fmt, ##__VA_ARGS__)
#define d_log_ibprintf(svc, buf, len, fmt, ...)		log_bprintf(LOG_INFO, svc, buf, len, fmt, ##__VA_ARGS__)
#define d_log_wprintf(svc, fmt, ...)			log_printf(LOG_WARNING, svc, fmt, ##__VA_ARGS__)
#define d_log_wbprintf(svc, buf, len, fmt, ...)		log_bprintf(LOG_WARNING, svc, buf, len, fmt, ##__VA_ARGS__)
#define d_log_eprintf(svc, fmt, ...)			log_printf(LOG_ERROR, svc, fmt, ##__VA_ARGS__)
#define d_log_ebprintf(svc, buf, len, fmt, ...)		log_bprintf(LOG_ERROR, svc, buf, len, fmt, ##__VA_ARGS__)
#define d_log_cprintf(svc, fmt, ...)			log_printf(LOG_CRITICAL, svc, fmt, ##__VA_ARGS__)
#define d_log_cbprintf(svc, buf, len, fmt, ...)		log_bprintf(LOG_CRITICAL, svc, buf, len, fmt, ##__VA_ARGS__)

#endif /* _SYSLOG_H_ */
