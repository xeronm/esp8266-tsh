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

#include "sysinit.h"
#include "core/ltime.h"
#include "core/utils.h"
#include "core/logging.h"

LOCAL const uint16   _year4[5] = { 0, 365, 730, 1096, 1461 };
LOCAL const uint16   _mon12[13] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 };

LOCAL sint8    s_time_zone = 0;	// current TimeZone
LOCAL uint32   s_time_last = 0;	// last RTC time for overflow check
LOCAL uint32   s_time_overflow_sec = 0;	// overflow number of seconds 
LOCAL lt_timestamp_t s_start_time = { 0, 0 };	// system start time from 01.01.1900 (POSIX)

LOCAL void 
lt_daystotm (uint32 days, struct ltm *_tm)
{
    _tm->tm_wday = days % DAY_PER_WEEK + 4;

    uint32          _y4 = days / DAY_PER_4YEAR;
    uint32          _d = (days + 1) % DAY_PER_4YEAR;	// days left in 4-year period
    _tm->tm_year = 1970 + _y4 * 4;

    _y4 = (_d - 1) / 365;	// full years in 4-year period
    if (_d <= _year4[_y4]) {
	_y4 -= 1;
    }
    _tm->tm_year += _y4;
    _d -= _year4[_y4];
    _tm->tm_yday = _d;
    uint8           _yleap = (_y4 == 2) ? 1 : 0;

    _y4 = (_d - 1) / 29;	// full months in year
    if (_d <= _mon12[_y4] + ((_y4 > 1) ? _yleap : 0)) {
	_y4 -= 1;
    }
    _tm->tm_mon = _y4 + 1;
    _d -= _mon12[_y4] + ((_y4 > 1) ? _yleap : 0);
    _tm->tm_mday = _d;
}

LOCAL uint32 
lt_tmtodays (const struct ltm *_tm) {
    uint32          _d = ((_tm->tm_year - 1970) / 4) * DAY_PER_4YEAR;
    uint32          _y4 = (_tm->tm_year - 1970) % 4;
    uint8           _yleap = (_y4 == 2) ? 1 : 0;
    _d += _year4[_y4];
    if (_tm->tm_yday != 0) {
	_d += _tm->tm_yday - 1;
    }
    else {
	_d += _mon12[_tm->tm_mon - 1] + ((_tm->tm_mon > 0) ? _yleap : 0);
	_d += _tm->tm_mday - 1;
    }

    return _d;
}


void            ICACHE_FLASH_ATTR
lt_localtime (const lt_time_t _time, struct ltm *_tm, bool utc)
{

    _tm->tm_tz = utc ? 0 : s_time_zone;
    if (_tm->tm_tz == TZ_ERR) {
	_tm->tm_tz = 0;
    }
    _tm->tm_isdst = 0;

    uint32          _t = _time + TZ_2_SEC_FACTOR * _tm->tm_tz;
    //sys_printf("_0: t:%d, spd:%d", _t, SEC_PER_DAY);

    uint32          _d = _t / SEC_PER_DAY;
    uint32          _s = _t % SEC_PER_DAY;

    _tm->tm_hour = _s / SEC_PER_HOUR;
    _s = _s % SEC_PER_HOUR;
    _tm->tm_min = _s / SEC_PER_MIN;
    _tm->tm_sec = _s % SEC_PER_MIN;

    lt_daystotm(_d, _tm);
}

lt_time_t       ICACHE_FLASH_ATTR
lt_mktime (struct ltm *_tm, bool utc)
{
    lt_time_t       _time = _tm->tm_sec;
    _time += _tm->tm_min * SEC_PER_MIN;
    _time += _tm->tm_hour * SEC_PER_HOUR;

    _time += lt_tmtodays(_tm) * SEC_PER_DAY;

    if (utc == true) {
	_time -= TZ_2_SEC_FACTOR * _tm->tm_tz;
    }

    return _time;
}

void            ICACHE_FLASH_ATTR
lt_add_secs (struct ltm *_tm, uint32 secs) {
    uint32 res = _tm->tm_sec + secs;
    _tm->tm_sec = res % SEC_PER_MIN;
    if (res >= SEC_PER_MIN)
        lt_add_mins (_tm, res/SEC_PER_MIN);
}

void            ICACHE_FLASH_ATTR
lt_add_mins (struct ltm *_tm, uint32 mins) {
    uint32 res = _tm->tm_min + mins;
    _tm->tm_min = res % MIN_PER_HOUR;
    if (res >= MIN_PER_HOUR)
        lt_add_hours (_tm, res/MIN_PER_HOUR);
}

void            ICACHE_FLASH_ATTR
lt_add_hours (struct ltm *_tm, uint32 hours) {
    uint32 res = _tm->tm_hour + hours;
    _tm->tm_hour = res % HOUR_PER_DAY;
    if (res >= HOUR_PER_DAY)
        lt_add_days (_tm, res/HOUR_PER_DAY);
}

void            ICACHE_FLASH_ATTR
lt_add_days (struct ltm *_tm, uint32 days) {
    lt_daystotm (lt_tmtodays (_tm) + days, _tm);
}

bool            ICACHE_FLASH_ATTR
lt_set_timezone (sint8 time_zone)
{
    if ((time_zone < TZ_MIN) || (time_zone > TZ_MAX)) {
	return false;
    }

    s_time_zone = time_zone;
    return true;
}

sint8           ICACHE_FLASH_ATTR
lt_get_timezone (void)
{
    return s_time_zone;
}

void            ICACHE_FLASH_ATTR
lt_get_ctime (lt_timestamp_t * ts)
{
    uint32          time_curr = system_get_time ();

    if (time_curr < s_time_last) {
	s_time_overflow_sec += 0xFFFFFFFF / USEC_PER_SEC;
    }
    s_time_last = time_curr;
    ts->sec = s_time_overflow_sec + s_time_last / USEC_PER_SEC;
    ts->usec = s_time_last % USEC_PER_SEC;
}

void            ICACHE_FLASH_ATTR
lt_get_time (lt_timestamp_t * ts)
{
    lt_get_ctime (ts);
    lt_time_add (ts, &s_start_time);
}

lt_time_t       ICACHE_FLASH_ATTR
lt_ctime (void)
{
    lt_timestamp_t  ts2;
    lt_get_ctime (&ts2);
    return ts2.sec;
}

lt_time_t       ICACHE_FLASH_ATTR
lt_time (lt_time_t * ct)
{
    if (ct) {
	return s_start_time.sec + *ct;
    }
    else {
	lt_timestamp_t  ts2;
	lt_get_ctime (&ts2);
	return s_start_time.sec + ts2.sec + (ts2.usec + s_start_time.usec) / USEC_PER_SEC;
    }
}

void            ICACHE_FLASH_ATTR
lt_set_time (lt_timestamp_t * ts)
{
    lt_timestamp_t  ts2;
    lt_get_ctime (&ts2);
    ts->sec -= ts2.sec;
    if (ts->usec < ts2.usec) {
	ts->sec--;
	ts->usec += USEC_PER_SEC;
    }
    ts->usec -= ts2.usec;

    s_start_time.sec = ts->sec + ts->usec / USEC_PER_SEC;
    s_start_time.usec = ts->usec % USEC_PER_SEC;
}


void            ICACHE_FLASH_ATTR
lt_time_add (lt_timestamp_t * dst, const lt_timestamp_t * src)
{
    dst->sec += src->sec;
    if (~dst->usec < src->usec) {
	dst->sec += 1;
    }
    dst->usec += src->usec;
}

void            ICACHE_FLASH_ATTR
lt_time_sub (lt_timestamp_t * dst, const lt_timestamp_t * src)
{
    dst->sec -= src->sec;
    if (dst->usec < src->usec) {
	dst->sec -= 1;
    }
    dst->usec -= src->usec;
}

sint8           ICACHE_FLASH_ATTR
lt_time_cmp (lt_timestamp_t * t1, const lt_timestamp_t * t2)
{
    if (t1->sec < t2->sec) {
	return -1;
    }
    else if (t1->sec > t2->sec) {
	return 1;
    }
    else {
	if (t1->usec < t2->usec) {
	    return -1;
	}
	else if (t1->usec > t2->usec) {
	    return 1;
	}
    }

    return 0;
}
