/* Copyright (c) 2018 by Denis Muratov <xeronm@gmail.com>. All rights reserved

   FileName: uintn-identity.h
   Source: https://dtec.pro/gitbucket/git/esp8266/esp8266_lsh.git

   Description: Stubs for GNU C Headers. 
   Inline functions to return unsigned integer values unchanged.   

*/

#if !defined _BITS_UINTN_IDENTITY_H && !defined _ENDIAN_H
#error "Never use <bits/uintn-identity.h> directly; include <endian.h> instead."
#endif

#ifndef _BITS_UINTN_IDENTITY_H
#define _BITS_UINTN_IDENTITY_H 1

#define __uint16_identity(x) \
	((unsigned short int) (x))

#define __uint32_identity(x) \
	((unsigned int) (x))

#define __uint64_identity(x) \
	((unsigned long long) (x))

#endif /* _BITS_UINTN_IDENTITY_H */
