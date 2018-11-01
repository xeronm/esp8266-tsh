/* 
 * Simple Hash Index
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

/*
 * TODO: Add remove support
 */

#ifndef __IDXHASH_H__
#define __IDXHASH_H__	1

#include "sysinit.h"

typedef void   *ih_hndlr_t;

uint8           ih_hash8 (char *buf, size_t len, uint8 init);
uint16          ih_hash16 (char *buf, size_t len, uint8 init);

typedef enum ih_errcode_e {
    IH_ERR_SUCCESS = 0,
    IH_BUFFER_OVERFLOW = 1,
    IH_ENTRY_EXISTS = 2,
    IH_ENTRY_NOTFOUND = 3,
    IH_NULL_ENTRY = 4,
} ih_errcode_t;

typedef char   *(*ih_get_entry_func) (char *value);	// not used

ih_errcode_t    ih_init8 (char *buf, size_t length, uint8 bucket_size, uint8 depth, bool key_nullterm,
			  uint8 value_length, ih_hndlr_t * hndlr);
ih_errcode_t    ih_hash8_add (ih_hndlr_t hndlr, char *entrykey, size_t len, char **value);
ih_errcode_t    ih_hash8_search (ih_hndlr_t hndlr, char *entrykey, size_t len, char **value);
char           *ih_hash8_v2key (ih_hndlr_t hndlr, char *value);


#endif
