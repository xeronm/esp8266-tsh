/* Copyright (c) 2018 by Denis Muratov <xeronm@gmail.com>. All rights reserved

   FileName: sysinit.h
   Source: https://dtec.pro/gitbucket/git/esp8266/esp8266_lsh.git

   Description: Platform-Specific initialization for esp8266

*/

#ifndef _SYSINIT_H_
#define _SYSINIT_H_ 1

#include "endian.h"
#include "assert.h"
#include "stdarg.h"
#include "ets_sys.h"
#include "osapi.h"
#include "mem.h"
#include "ip_addr.h"
#include "espconn.h"
#include "user_interface.h"
#include "at_custom.h"
#include "spi_flash.h"

#define ARCH_XTENSA

/* 
    Additional Types
*/
typedef uint32  os_time_t;

#define LINE_END		"\n"
#define LINE_END_STRLEN	1

#define IMDB_SMALL_RAM

#define ALIGN_DATA	_Alignas(uint32)
#define PACKED		//__packed
#define LOCAL       	static
#define INLINED       	inline
#define RODATA		// ICACHE_RODATA_ATTR

extern int      ets_vprintf (const char *format, va_list arg);
extern int      ets_vsprintf (char *s, const char *format, va_list arg);
extern int      ets_vsnprintf (char *s, size_t n, const char *format, va_list arg);
extern int      ets_sprintf (char *buf, const char *format, ...);
extern int      ets_snprintf (char *buf, unsigned int size, const char *format, ...);

int             __port_printf (const char *fmt, ...);
int             __port_vprintf (const char *fmt, va_list arg);

#ifdef ENABLE_AT
#define PORT_PRINTF_BUFFER_SIZE	256
#undef os_print
#undef os_printf
#undef os_vprintf

#define os_print	at_port_print
#define os_printf	__port_printf
#define os_vprintf	__port_vprintf
#else
#define PORT_PRINTF_BUFFER_SIZE	256
#define os_print      os_printf
#define os_vprintf	__port_vprintf
#endif

size_t          __strnlen (const char *s, size_t maxlen);
#define os_strnlen	__strnlen

#define os_snprintf	ets_snprintf
#define os_sprintf	ets_sprintf

#define os_vsprintf	ets_vsprintf
#define os_vsnprintf	ets_vsnprintf

#define os_halt		system_restart

#define os_random_buffer	os_get_random
/*
   Network 
*/
typedef struct ipv4_addr_s {
    union {
	uint32          addr;
	uint8           bytes[4];
	struct ip_addr  ip;
    };
} ipv4_addr_t;

typedef uint16  ip_port_t;
typedef struct espconn ip_conn_t;

void            __os_conn_remote_port (ip_conn_t * pconn, ip_port_t * port);
void            __os_conn_remote_addr (ip_conn_t * pconn, ipv4_addr_t * ipaddr);

#define os_conn_remote_port(conn, port)		__os_conn_remote_port((conn), (port))
#define os_conn_remote_addr(conn, ipaddr)	__os_conn_remote_addr((conn), (ipaddr))
#define os_conn_sent(conn, data, length)	espconn_sent((conn), (uint8 *)(data), (length))
#define os_conn_create(conn)			espconn_create((conn))
#define os_conn_free(conn)			espconn_delete((conn))
#define os_conn_set_recvcb(conn, recv_cb)	espconn_regist_recvcb((conn), (recv_cb))


size_t          fio_user_read(uint32 addr, uint32 *buffer, uint32 size);
size_t          fio_user_write(uint32 addr, uint32 *buffer, uint32 size);
size_t          fio_user_size(void);


/*
   User Init
*/
void            user_init (void);

#endif /* _SYSINIT_H_ */
