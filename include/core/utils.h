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

#ifndef UTILS_H_
#define UTILS_H_

#include "sysinit.h"

#ifdef ASSERT_DEBUG
#define d_assert(expr, fmt, ...) \
	if (! (expr)) { \
		d_log_cprintf("", "Assertion '%s' at %s.%u, message: " fmt, __STRING(expr), __FILE__, __LINE__, ##__VA_ARGS__); \
		os_halt(); \
	}
#else
#define d_assert(expr, fmt, ...)
#endif

// time conversions
#define USEC_PER_MSEC		1000
#define MSEC_PER_SEC		1000
#define USEC_PER_SEC		1000000UL
#define MSEC_PER_MIN		60000

#define SEC_PER_MIN		60
#define SEC_PER_HOUR		3600
#define SEC_PER_DAY		86400
#define MIN_PER_HOUR		60
#define HOUR_PER_DAY		60
#define DAY_PER_MONTH		31
#define DAY_PER_WEEK		7
#define WEEK_START_DAY		2
#define DAY_PER_4YEAR		1461

#define VER2STR(ver) \
	((ver) >> 24), ((ver) >> 16 & 0xFF), ((ver) & 0xFFFF)

#define d_pointer_diff(x, y)		( (size_t) ((char*)(x) - (char*)(y)) )
#define d_pointer_add(type, x, y)	( (type*) ((char*)(x) + (size_t)(y)) )
#define d_pointer_as(type, x)		( (type*) ((char*)(x)) )
#define d_pointer_equal(x, y)		( (void*)x == (void*)y )

// BIT buffer operations
#define	d_bitbuf_get(buf, n)		( ((buf)[(n) >> 3] >> (7-((n) & 7))) & 0b1 )
#define	d_bitbuf_set(buf, n)		( (buf)[(n) >> 3] |= (1 << (7-((n) & 7))) )
#define	d_bitbuf_clear(buf, n)		( (buf)[(n) >> 3] &= ~(1 << (7-((n) & 7))) )
#define d_bitbuf_size(size)		(((size) + 7) >> 3)
#define	d_bitbuf_rset(buf, s, e)	\
	{ \
		int i; \
		for (i=(s); i<=(e); i++) \
			d_bitbuf_set((buf), i); \
	}
#define	d_bitbuf_rclear(buf, s, e)	\
	{ \
		int i; \
		for (i=(s); i<=(e); i++) \
			d_bitbuf_clear((buf), i); \
	}

// List operations
#define	d_list_sizeof(type)			(sizeof(imdb_list_t)+sizeof(type))
#define	d_list_alloc(ptr, type)		ptr = os_malloc(d_list_sizeof(type)); os_memset(ptr, 0, d_list_sizeof(type));
#define	d_list_data(ptr, type)		((type*)&(ptr)->data)
#define	d_list_concat(ptr1, ptr2)	(ptr1)->list_next = (ptr2); (ptr2)->list_prev = (ptr1);

// 4 bytes allign
#define d_align_32(x) \
	(((x) + 0b11) & (uint32)(~0b11))

// 8 bytes allign
#define d_align_64(x) \
	(((x) + 0b111) & (uint64)(~0b111))

#define d_align d_align_32

// math operations
#define	MIN(X, Y) \
	((X) < (Y) ? (X) : (Y))

#define	MAX(X, Y) \
	((X) > (Y) ? (X) : (Y))

#define	ABS(X) \
	(uint32)((sint32)(X) < 0 ? -(sint32)(X) : (sint32)(X))

#define	ABS64(X) \
	(uint64)((sint64)(X) < 0 ? -(sint64)(X) : (sint64)(X))

#define LOG2(X, Y) \
	do { \
		static uint32 n; \
		n=X; Y=0;\
		while (n > 0) { \
			Y += 1; n = n<<1; \
		  } \
	} while(0)

//
#define d_char_is_space(ptr)	(*(ptr) == ' ' || *(ptr) == '\t')
#define d_skip_space(ptr)	while d_char_is_space(ptr) { (ptr)++; }
#define d_skip_not_space(ptr)	while ((! d_char_is_space(ptr)) && (*(ptr) != '\0')) { (ptr)++; }
#define d_char_is_digit(ptr)	((*(ptr) >= '0') && (*(ptr) <= '9'))
#define d_char_is_letter(ptr)	( ((*(ptr) >= 'a') && (*(ptr) <= 'z')) || ((*(ptr) >= 'A') && (*(ptr) <= 'Z')) )
#define d_char_is_end(ptr)	(d_char_is_space(ptr) || (*(ptr) == '\0'))
#define d_char_is_quote(ptr)	((*(ptr) == '\'') || (*(ptr) == '"'))
#define d_char_is_token1(ptr)	((*(ptr) == '_') || d_char_is_letter(ptr))
#define d_char_is_token(ptr)	(d_char_is_token1(ptr) || d_char_is_digit(ptr))
#define d_char_is_tokend(ptr)	( ((*(ptr) >= ' ') && (*(ptr) < '~')) && (! d_char_is_token (ptr)) )

// memory allocation
#define st_alloc(ptr, type) \
	{ \
	  ptr = (type *)os_malloc(sizeof(type)); \
	}

#define st_zalloc(ptr, type) \
	{ \
	  ptr = (type *)os_malloc(sizeof(type)); \
	  os_memset(ptr, 0, sizeof(type)); \
	}

#define st_free(ptr) \
	{ \
	  os_free(ptr); \
	  ptr = NULL; \
	}

// string functions
bool            parse_uint (const char **szstr, unsigned int *num);
bool            estlen_qstr (const char *szstr, size_t * len);
bool            parse_qstr (const char **szstr, char *ch);
bool            estlen_token (const char *szstr, size_t * len);
bool            parse_token (const char **szstr, char *token);

size_t          buf2hex (char *dst, const char *src, const size_t length);
size_t          hex2buf (char *dst, const size_t length, const char *src);

size_t          sprintb (char *dst, const size_t dlen, const char *src, const size_t length);
size_t          printb (const char *src, const size_t length);
bool            bufcc (char *dst, char *src, size_t maxlen);

// number functions
unsigned long   log2x (unsigned long x);

// аналогично os_sprintf но с проверкой переполнения
size_t          csnprintf (char *buf, size_t len, const char *fmt, ...);

typedef uint8   digest64_t[8];
typedef uint8   digest128_t[16];
typedef uint8   digest256_t[32];
typedef uint8   digest512_t[64];

#endif /* UTILS_H_ */
