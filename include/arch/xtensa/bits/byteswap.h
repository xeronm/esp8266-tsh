/* Copyright (c) 2018 by Denis Muratov <xeronm@gmail.com>. All rights reserved

   FileName: byteswap.h
   Source: https://dtec.pro/gitbucket/git/esp8266/esp8266_lsh.git

   Description: Stubs for GNU C Headers.
   Macros to swap the order of bytes in integer values.

*/

#if !defined _BYTESWAP_H && !defined _ENDIAN_H
#error "Never use <bits/byteswap.h> directly; include <byteswap.h> instead."
#endif

#ifndef _BITS_BYTESWAP_H
#define _BITS_BYTESWAP_H 1

/* Swap bytes in 16 bit value.  */
#define __bswap_constant_16(x) \
	((unsigned short int)((((x) >> 8) & 0xffu) | (((x) & 0xffu) << 8)))

#define __bswap_16(x) \
    (__extension__							      \
     ({ unsigned short int __bsx = (unsigned short int) (x);		      \
       __bswap_constant_16 (__bsx); }))

/* Swap bytes in 32 bit value.  */
#define __bswap_constant_32(x) \
     ((((x) & 0xff000000u) >> 24) | (((x) & 0x00ff0000u) >>  8) |	      \
      (((x) & 0x0000ff00u) <<  8) | (((x) & 0x000000ffu) << 24))

#define __bswap_32(x) \
  (__extension__							      \
   ({ unsigned int __bsx = (x); __bswap_constant_32 (__bsx); }))


/* Swap bytes in 64 bit value.  */
#define __bswap_constant_64(x) \
     (__extension__ ((((x) & 0xff00000000000000ull) >> 56)		      \
		     | (((x) & 0x00ff000000000000ull) >> 40)		      \
		     | (((x) & 0x0000ff0000000000ull) >> 24)		      \
		     | (((x) & 0x000000ff00000000ull) >> 8)		      \
		     | (((x) & 0x00000000ff000000ull) << 8)		      \
		     | (((x) & 0x0000000000ff0000ull) << 24)		      \
		     | (((x) & 0x000000000000ff00ull) << 40)		      \
		     | (((x) & 0x00000000000000ffull) << 56)))

#define __bswap_64(x) \
  (__extension__							      \
   ({ unsigned long long __bsx = (x); __bswap_constant_64 (__bsx); }))

#endif /* _BITS_BYTESWAP_H */
