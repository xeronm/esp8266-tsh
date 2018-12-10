/* 
 * ESP8266 Light-weight Time-conversion utility
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

#ifndef LTIME_H_
#define LTIME_H_

#include "sysinit.h"
#include "utils.h"

typedef os_time_t lt_time_t;

#define TZ_PER_HOUR		4
#define TZ_2_SEC_FACTOR		900
#define TZ_MIN			-48
#define TZ_MAX			48
#define TZ_ERR			0x7F

#define TMUPSTR 	"%ddays, %dh %dm %ds"
#define TMUPSTR_SHORT 	"%dh %dm %ds"
#define TMSTR_TZ	"%04d.%02d.%02d %02d:%02d:%02d%+02d:%02d"
#define TMSTR 		"%04d.%02d.%02d %02d:%02d:%02d"
#define TZSTR		"%+02d:%02d"

#define TM2UPSTR(tm) 	((tm)->tm_yday-1), (tm)->tm_hour, (tm)->tm_min, (tm)->tm_sec
#define TM2UPSTR_SHORT(tm) 	(tm)->tm_hour, (tm)->tm_min, (tm)->tm_sec
#define TM2STR(tm) 	(tm)->tm_year, (tm)->tm_mon, (tm)->tm_mday, (tm)->tm_hour, (tm)->tm_min, (tm)->tm_sec
#define TM2STR_TZ(tm) 	(tm)->tm_year, (tm)->tm_mon, (tm)->tm_mday, (tm)->tm_hour, (tm)->tm_min, (tm)->tm_sec, ((tm)->tm_tz/4), (((tm)->tm_tz%4)*15)
#define TZ2STR(tz)	((tz)/4), (((tz)%4)*15)

struct ltm;

typedef struct ltm {
    uint8           tm_sec;
    uint8           tm_min;
    uint8           tm_hour;
    uint8           tm_mday;
    uint8           tm_mon;
    uint16          tm_year;
    uint8           tm_wday;
    uint16          tm_yday;
    uint8           tm_isdst;
    uint8           tm_tz;
} ltm_t;

typedef struct lt_timestamp_s {
    uint32          sec;
    uint32          usec;
} lt_timestamp_t;


/* [public]: fill tm-structure from POSIX Time
  - _time: POSIX time (seconds)
  - _tm: return time stucture
  - utc: use UTC time
*/
void            lt_localtime (const lt_time_t _time, struct ltm *_tm, bool utc);

void            lt_add_secs (struct ltm *_tm, uint32 secs);
void            lt_add_mins (struct ltm *_tm, uint32 mins);
void            lt_add_hours (struct ltm *_tm, uint32 hours);
void            lt_add_days (struct ltm *_tm, uint32 days);

/* [public]: make POSIX Time from tm-structure
  - _tm: time stucture
  - utc: use UTC time
  - result: POSIX time (seconds)
*/
lt_time_t       lt_mktime (struct ltm *_tm, bool utc);

// time zone
bool            lt_set_timezone (sint8 time_zone);
sint8           lt_get_timezone (void);

/* [public]: return time from system start
  - ts: return timstamp structure (sec,usec) */
void            lt_get_ctime (lt_timestamp_t * ts);

/* [public]: return POSIX time
  - ts: return timstamp structure (sec,usec) */
void            lt_get_time (lt_timestamp_t * ts);

/* [public]: set POSIX time
  - ts: return timstamp structure (sec,usec) */
void            lt_set_time (lt_timestamp_t * ts);

/* [public]: return time from system start
  - return: return seconds */
lt_time_t       lt_ctime (void);

/* [public]: return POSIX time
  - return: return seconds */
lt_time_t       lt_time (lt_time_t * ct);

void            lt_time_add (lt_timestamp_t * dst, const lt_timestamp_t * src);
void            lt_time_sub (lt_timestamp_t * dst, const lt_timestamp_t * src);
sint8           lt_time_cmp (lt_timestamp_t * t1, const lt_timestamp_t * t2);


#endif /* LTIME_H_ */
