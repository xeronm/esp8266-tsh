/* Copyright (c) 2018 by Denis Muratov <xeronm@gmail.com>. All rights reserved

   FileName: sysinit.h
   Source: https://dtec.pro/gitbucket/git/esp8266/esp8266_lsh.git

   Description: Platform-Specific initialization for x86 Linux

*/

#ifndef _SYSINIT_H_
#define _SYSINIT_H_ 1

#ifndef _BSD_SOURCE
#  define _BSD_SOURCE
#endif

#include <endian.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>


typedef unsigned char	sint8_t;
typedef unsigned short	sint16_t;
typedef int 		sint32_t;
typedef long long int	sint64_t;
typedef sint8_t 	sint8;
typedef sint16_t 	sint16;
typedef sint32_t 	sint32;
typedef sint64_t 	sint64;
typedef uint8_t 	uint8;
typedef uint16_t 	uint16;
typedef uint32_t 	uint32;
typedef float 		real32;

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

struct ip_addr {
    uint32 addr;
};

typedef struct ip_addr ip_addr_t;

typedef uint16  ip_port_t;
typedef void   *ip_conn_t;

#define IPADDR_NONE 	0xFFFFFFFF

#define LINE_END		"\n"
#define LINE_END_STRLEN	1

#define os_free		free
#define os_malloc	malloc
#define os_realloc	realloc

void           *os_zalloc (size_t size);
size_t          system_get_free_heap_size ();
os_time_t       system_get_time (void);

uint32          system_rtc_clock_cali_proc(void);

size_t          fio_user_read(uint32 addr, uint32 *buffer, uint32 size);
size_t          fio_user_write(uint32 addr, uint32 *buffer, uint32 size);
size_t          fio_user_size(void);

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

#define ALIGN_DATA	__attribute__ ((aligned (4)))
#define PACKED		//__packed
#define RODATA		// ICACHE_RODATA_ATTR
#define LOCAL       	static
#define INLINED		//inline
#define ICACHE_FLASH_ATTR
#define ICACHE_RODATA_ATTR
#define SPI_FLASH_SEC_SIZE	4096

#define GPIO_PIN_COUNT	0

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
