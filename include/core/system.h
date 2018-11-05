/* 
 * ESP8266 System entry points
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

#ifndef _SYSTEM_H_
#define _SYSTEM_H_ 1

#include "system/imdb.h"

#define SYSTEM_IMDB_BLOCK_SIZE	2048
#define SYSTEM_FDB_BLOCK_SIZE	SPI_FLASH_SEC_SIZE

void            system_init (void);
void            system_shutdown (void);

imdb_hndlr_t    get_hmdb (void);
imdb_hndlr_t    get_fdb (void);

uint8           system_get_default_secret (unsigned char *buf, uint8 len);

#endif /* _SYSTEM_H */
