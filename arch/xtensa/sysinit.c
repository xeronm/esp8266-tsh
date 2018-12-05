/* 
 * ESP8266 Platform-Specific initialization
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
 * Foobar is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Foobar.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#ifdef ENABLE_AT
#include "at_custom.h"
#else
#include "uart.h"
#endif
#include "sysinit.h"
#include "flashmap.h"
#include "core/logging.h"
#include "core/system.h"

#define FIO_USER_DATA_ADDR_NONE		((uint32) ~0)

void            ICACHE_FLASH_ATTR
__os_conn_remote_port (ip_conn_t * pconn, ip_port_t * port)
{
    switch (pconn->type) {
    case ESPCONN_TCP:{
	    *port = (ip_port_t) (pconn->proto.tcp->remote_port);
	    break;
	}
    case ESPCONN_UDP:{
	    *port = (ip_port_t) (pconn->proto.udp->remote_port);
	    break;
	}
    default:
	*port = 0;
    }
}

void            ICACHE_FLASH_ATTR
__os_conn_remote_addr (ip_conn_t * pconn, ipv4_addr_t * ipaddr)
{
    switch (pconn->type) {
    case ESPCONN_TCP:{
	    os_memcpy (ipaddr->bytes, pconn->proto.tcp->remote_ip, sizeof (ipaddr->bytes));
	    break;
	}
    case ESPCONN_UDP:{
	    os_memcpy (ipaddr->bytes, pconn->proto.udp->remote_ip, sizeof (ipaddr->bytes));
	    break;
	}
    default:
	ipaddr->addr = IPADDR_ANY;
    }
}

int             ICACHE_FLASH_ATTR
__port_vprintf (const char *fmt, va_list arg)
{
    char            buf[PORT_PRINTF_BUFFER_SIZE];
    int             result = ets_vsnprintf (buf, sizeof (buf), fmt, arg);
#ifdef ENABLE_AT
    at_port_print (buf);
#else
    uart0_sendStr (buf);
#endif
    return result;
}

int             ICACHE_FLASH_ATTR
__port_printf (const char *fmt, ...)
{
    va_list         al;
    va_start (al, fmt);
    int             result = __port_vprintf (fmt, al);
    va_end (al);
    return result;
}

size_t          ICACHE_FLASH_ATTR
__strnlen (const char *s, size_t maxlen)
{
    size_t          _len = 0;
    while ((*s != '\0') && (_len < maxlen)) {
	_len++;
	s++;
    }
    return _len;
}

size_t          ICACHE_FLASH_ATTR
fio_user_read(uint32 addr, uint32 *buffer, uint32 size) 
{
    flash_ota_map_t * fwmap = get_flash_ota_map ();
    if (!fwmap->user1)
        return 0;

    uint32 addr0 = d_flash_user2_data_addr(fwmap) + addr;
    if (addr0 + size > d_flash_user2_data_addr_end(fwmap))
        return 0;

    if (spi_flash_read (addr0, buffer, size))
        return 0;
    else
        return size;
}

size_t          ICACHE_FLASH_ATTR 
fio_user_write(uint32 addr, uint32 *buffer, uint32 size)
{
    flash_ota_map_t * fwmap = get_flash_ota_map ();
    if (!fwmap->user1)
        return 0;

    uint32          addr0 = d_flash_user2_data_addr(fwmap) + addr;
    uint16          sec = addr0 / SPI_FLASH_SEC_SIZE;
    uint32          addr1 = addr0 + size;

    if (addr0 + size > d_flash_user2_data_addr_end(fwmap))
        return 0;

    if (sec != ((addr1 - 1) / SPI_FLASH_SEC_SIZE)) {
        // not aligned write, more than one sector
        return 0;
    }

    if (size < SPI_FLASH_SEC_SIZE) {
        uint8           tmp_buffer[SPI_FLASH_SEC_SIZE];
        uint32          addr_s0 = sec * SPI_FLASH_SEC_SIZE;
        uint32          addr_s1 = addr_s0 + SPI_FLASH_SEC_SIZE;

        if (spi_flash_read (addr_s0, (uint32*) tmp_buffer, SPI_FLASH_SEC_SIZE) ||
            spi_flash_erase_sector (sec) ||
            (addr_s0 < addr0 ? spi_flash_write(addr_s0, (uint32*) tmp_buffer, addr0 - addr_s0) : false) ||
            spi_flash_write(addr0, buffer, size) ||
            (addr1 < addr_s1 ? spi_flash_write(addr0 + size, (uint32*) (tmp_buffer + (addr1 - addr_s0)), addr_s1 - addr1) : false)
           )
            return 0;
    }
    else {
        if (spi_flash_erase_sector (sec) || spi_flash_write(addr0, buffer, size))
            return 0;
    }

    return size;
}

size_t          ICACHE_FLASH_ATTR 
fio_user_size(void)
{
    flash_ota_map_t * fwmap = get_flash_ota_map ();
    if (!fwmap->user1)
        return 0;

    return (d_flash_user2_data_addr_end(fwmap) - d_flash_user2_data_addr(fwmap)) / 2; // 2 - for mirroring
}

size_t          ICACHE_FLASH_ATTR 
fio_user_format(uint32 size) {
    flash_ota_map_t * fwmap = get_flash_ota_map ();
    if (!fwmap->user1)
        return 0;

    uint32          addr0 = d_flash_user2_data_addr (fwmap);
    uint32 data = 0;
    if (spi_flash_erase_sector (addr0 / SPI_FLASH_SEC_SIZE) || spi_flash_write(addr0, &data, sizeof(uint32)) )
        return 0;

    return size;
}

void            ICACHE_FLASH_ATTR
user_init (void)
{    
#ifndef DISABLE_CORE
    system_init ();
#endif
}
