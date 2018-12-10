/* 
 * ESP8266 Auxiliary Function and Defines
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
#include "core/utils.h"
#include "core/logging.h"

static char     sz_hex_map[16] = "0123456789abcdef";


bool            ICACHE_FLASH_ATTR
parse_hex (const char ch, char *num)
{
    if ((ch >= '0') && (ch <= 'F')) {
        *num += (ch - '0');
    }
    else if ((ch >= 'a') && (ch <= 'f')) {
        *num += (ch - 'a' + 10);
    }
    else
        return false;
    return true;
}

bool            ICACHE_FLASH_ATTR
parse_uint (const char **szstr, unsigned int *num)
{
    const char     *ptr = *szstr;
    unsigned int    _num = 0;
    unsigned short  _digit = 0;
    while d_char_is_digit
        (ptr) {
        _num = _num * 10 + (*ptr - '0');
        _digit++;
        ptr++;
        }

    *szstr = ptr;
    *num = _num;
    return (_digit > 0) ? true : false;
}

// estimate quoted string length, for accurate memory allocation
bool            ICACHE_FLASH_ATTR
estlen_qstr (const char *szstr, size_t * len)
{
    const char     *ptr = szstr;
    if (!d_char_is_quote (ptr))
        return false;
    char            qterm = *ptr;
    ptr++;                      // skip opening quote
    bool            fescape = false;
    size_t          _len = 0;
    while ((fescape || *ptr != qterm) && (*ptr != '\0')) {
        if (*ptr == '\\') {
            fescape = true;
        }
        else {
            _len++;
            fescape = false;
        }
        ptr++;
    }

    if (*ptr == '\0')
        return false;

    *len = _len;
    return true;
}

/*
[public] Parse quoted string with escape characters
*/
bool            ICACHE_FLASH_ATTR
parse_qstr (const char **szstr, char *ch)
{
    const char     *ptr = *szstr;
    char           *chptr = ch;
    if (!d_char_is_quote (ptr)) {
        *chptr = '\0';
        return false;
    }

    char            qterm = *ptr;
    ptr++;                      // skip opening quote
    bool            fescape = false;
    while ((fescape || *ptr != qterm) && (*ptr != '\0')) {
        if (*ptr == '\\') {
            fescape = true;
        }
        else {
            *chptr = *ptr;
            if (fescape) {
                switch (*chptr) {
                case 'r':
                    *chptr = '\r';
                    break;
                case 't':
                    *chptr = '\t';
                    break;
                case 'n':
                    *chptr = '\n';
                    break;
                default:
                    break;
                }
            }
            chptr++;
            fescape = false;
        }
        ptr++;
    }
    *chptr = '\0';
    *szstr = ptr;

    if (*ptr == '\0') {
        return false;
    }
    else {
        *szstr += 1;
    }

    return true;
}

/*
[public] Estimate token length, for accurate memory allocation
*/
bool            ICACHE_FLASH_ATTR
estlen_token (const char *szstr, size_t * len)
{
    const char     *ptr = szstr;
    // first character can't be a digit
    if (!d_char_is_token1 (ptr))
        return false;

    while d_char_is_token
        (ptr) ptr++;

    if (!d_char_is_tokend (ptr))
        return false;
    *len = (ptr - szstr);
    return true;
}

/*
* [public] Pasrse token string
*/
bool            ICACHE_FLASH_ATTR
parse_token (const char **szstr, char *token)
{
    const char     *ptr = *szstr;
    char           *tptr = token;
    // first character can't be a digit
    if (!d_char_is_token1 (ptr)) {
        *tptr = '\0';
        return false;
    }

    while (d_char_is_token (ptr)) {
        *tptr = *ptr;
        ptr++;
        tptr++;
    }

    *tptr = '\0';
    *szstr = ptr;

    if (!d_char_is_tokend (ptr))
        return false;

    return true;
}

/*
* [public] Convert ASCII buffer to Hex string
*/
size_t          ICACHE_FLASH_ATTR
buf2hex (char *dst, const char *src, const size_t length)
{
    size_t          i;
    for (i = 0; i < length; i += 1) {
        uint8           ch = (uint8) * src;
        *dst = sz_hex_map[(ch >> 4) & 0xF];
        dst += 1;
        *dst = sz_hex_map[ch & 0xF];
        dst += 1;
        src += 1;
    }
    *dst = 0x0;

    return i * 2;
}

/*
* [public] Convert Hex buffer to ASCII string
*/
size_t          ICACHE_FLASH_ATTR
hex2buf (char *dst, const size_t length, const char *src)
{
    size_t          slen = os_strlen (src);
    if (slen % 2 != 0)
        return 0;

    size_t          i;
    char            num;
    for (i = 0; i < slen / 2; i += 1) {
        num = 0;
        // first digit
        if (!parse_hex (*src, &num))
            return 0;
        src += 1;
        num *= 16;
        // second digit
        if (!parse_hex (*src, &num))
            return 0;
        src += 1;

        *dst = num;
        dst++;
    }
    *dst = 0x0;

    return i;
}

size_t          ICACHE_FLASH_ATTR
printb (const char *src, const size_t length)
{
    size_t          i;
    size_t          _len;

    _len = os_printf ("\t<ptr:%p, len:%u>" LINE_END, src, length);
    for (i = 0; i < length; i += 1) {
        uint8           ch = (uint8) * src;
        if (i % 16 == 0) {
            if (i == 0) {
                _len += os_printf ("\t%04x: ", i);
            }
            else {
                _len += os_printf (LINE_END "\t%04x: ", i);
            }
        }
        else if (i % 4 == 0) {
            _len += os_printf (" ");
        }

        _len += os_printf ("%02x", ch);
        src++;
    }

    return _len;
}

size_t          ICACHE_FLASH_ATTR
sprintb (char *dst, const size_t dlen, const char *src, const size_t length)
{
    size_t          i;
    char           *_dst = dst;
    char           *_dst_max = dst + dlen - 16; // 16 for <cutted>

    _dst += os_sprintf (_dst, "\t<ptr:%p, len:%u>" LINE_END, src, length);
    for (i = 0; i < length; i += 1) {
        uint8           ch = (uint8) * src;
        if (_dst >= _dst_max) {
            _dst += os_sprintf (_dst, " <cut>");
            break;
        }
        if (i % 16 == 0) {
            if (i == 0) {
                _dst += os_sprintf (_dst, "\t%04x: ", i);
            }
            else {
                _dst += os_sprintf (_dst, LINE_END "\t%04x: ", i);
            }
        }
        else if (i % 4 == 0) {
            *_dst = 32;
            _dst++;
        }

        *_dst = sz_hex_map[(ch >> 4) & 0xF];
        _dst++;
        *_dst = sz_hex_map[ch & 0xF];
        _dst++;
        src++;
    }
    *_dst = 0x0;

    return (_dst - dst);
}

unsigned long   ICACHE_FLASH_ATTR
log2x (unsigned long x)
{
    unsigned long   n = x;
    unsigned long   y = 0;
    while (n > 0) {
        n = n << 1;
        y += 1;
    }
    return y;
}

size_t          ICACHE_FLASH_ATTR
csnprintf (char *buf, size_t len, const char *fmt, ...)
{
    va_list         al;
    va_start (al, fmt);
    size_t          _len = os_vsnprintf (buf, len - 1, fmt, al);
    if (_len >= len) {
        d_log_wprintf ("", "csnprintf buffer overflow: %lu", _len);
    }
    va_end (al);

    return _len;
}


bool            ICACHE_FLASH_ATTR
bufcc (char *dst, char *src, size_t maxlen)
{
    size_t          _slen = MIN (maxlen, os_strlen (src));
    if (os_strncmp (dst, src, _slen) != 0) {
        os_memcpy (dst, src, _slen);
        if (maxlen > _slen) {
            os_memset (dst + _slen, 0, maxlen - _slen);
        }
        return true;
    }

    return false;
}
