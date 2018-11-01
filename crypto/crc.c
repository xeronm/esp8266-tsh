/* 
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


#include "sysinit.h"
#include "crypto/crc.h"

/*
[public] CRC-8 ETSI
	Polynom			: 0xD5	x^8 + x^7 + x^6 + x^4 + x^2 + 1
	Effective Length: 15 bytes (127 bits)
*/
unsigned char   ICACHE_FLASH_ATTR
crc8 (unsigned char *buf, size_t len)
{
    unsigned char   crc = 0x00;
    unsigned int    i;

    while (len--) {
	crc ^= *buf++;

	for (i = 0; i < 8; i++)
	    crc = crc & 0x80 ? (crc << 1) ^ 0xD5 : crc << 1;
    }

    return crc;
}


/*
[public] CRC-16 CCIT
	Polynom			: 0x1021	x^16 + x^12 + x^5 + 1
	Effective Length: 4095 bytes (32767 bits)
*/
unsigned short  ICACHE_FLASH_ATTR
crc16 (unsigned char *buf, size_t len)
{
    unsigned short  crc = 0xFFFF;
    unsigned char   i;

    while (len--) {
	crc ^= *buf++ << 8;

	for (i = 0; i < 8; i++)
	    crc = crc & 0x8000 ? (crc << 1) ^ 0x1021 : crc << 1;
    }
    return crc;
}
