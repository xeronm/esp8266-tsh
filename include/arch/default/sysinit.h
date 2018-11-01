/* Copyright (c) 2018 by Denis Muratov <xeronm@gmail.com>. All rights reserved

   FileName: sysinit.h
   Source: https://dtec.pro/gitbucket/git/esp8266/esp8266_lsh.git

   Description: Platform-Specific initialization for x86 Linux

*/

#ifndef _SYSINIT_H_
#define _SYSINIT_H_ 1

#define _BSD_SOURCE
#include <endian.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>


typedef unsigned short sint8;
typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;

/* 
    Additional Types
*/
typedef uint32  os_time_t;

typedef struct ip_addr_s {
    union {
	uint32          addr;
	uint8           bytes[4];
    };
} ipv4_addr_t;

typedef uint16  ip_port_t;
typedef void   *ip_conn_t;

#define LINE_END		"\n"
#define LINE_END_STRLEN	1

#define os_free		free
#define os_malloc	malloc
#define os_realloc	realloc

void           *os_zalloc (size_t size);
size_t          system_get_free_heap_size ();
os_time_t       system_get_time (void);

#define os_printf	printf
#define os_sprintf	sprintf
#define os_snprintf	snprintf

#define os_vprintf	vprintf
#define os_vsprintf	vsprintf
#define os_vsnprintf	vsnprintf

#define os_strlen	strlen
#define os_strnlen	strnlen
#define os_strncmp	strncmp
#define os_strcmp	strcmp
#define os_memcpy	memcpy
#define os_memset	memset
#define os_memcmp	memcmp

#define os_halt()	exit(0)

#define PACKED		//__packed
#define RODATA		// ICACHE_RODATA_ATTR
#define LOCAL       	static
#define INLINED		//inline

#undef IMDB_SMALL_RAM

#define IP4_ADDR(ipaddr, a,b,c,d) \
        (ipaddr)->addr = ((uint32)((d) & 0xff) << 24) | \
                         ((uint32)((c) & 0xff) << 16) | \
                         ((uint32)((b) & 0xff) << 8)  | \
                          (uint32)((a) & 0xff)

#define os_conn_remote_port(conn, port)		*(port) = 0;
#define os_conn_remote_addr(conn, ipaddr)	IP4_ADDR(ipaddr, 192, 168, 1, 1);
#define os_conn_sent(conn, data, length)	;	// espconn_sent((conn), (char *)(data), (length));
#define os_conn_create(conn)			;
#define os_conn_free(conn)			;
#define os_conn_set_recvcb(conn, recv_cb)	;

#define os_random()				(0)

#define ip4_addr1(ipaddr) (((uint8*)(ipaddr))[0])
#define ip4_addr2(ipaddr) (((uint8*)(ipaddr))[1])
#define ip4_addr3(ipaddr) (((uint8*)(ipaddr))[2])
#define ip4_addr4(ipaddr) (((uint8*)(ipaddr))[3])

#define ip4_addr1_16(ipaddr) ((uint16)ip4_addr1(ipaddr))
#define ip4_addr2_16(ipaddr) ((uint16)ip4_addr2(ipaddr))
#define ip4_addr3_16(ipaddr) ((uint16)ip4_addr3(ipaddr))
#define ip4_addr4_16(ipaddr) ((uint16)ip4_addr4(ipaddr))

#define IPSTR "%d.%d.%d.%d"
#define IP2STR(ipaddr) ip4_addr1_16(ipaddr), \
    ip4_addr2_16(ipaddr), \
    ip4_addr3_16(ipaddr), \
    ip4_addr4_16(ipaddr)

#endif /* _SYSINIT_H_ */
