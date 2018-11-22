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

/*
Buffer format:

	+-------------+---------------+-----+-----------------+
	| Root Bucket | Overflow Area | ... | Siblings Bucket |
	+-------------+---------------+-----+-----------------+

        TODO: Siblings Bucket not realized

Inline key and value Entry format:
	+-------------+-----------+-------------+
	| Next Entry  |   Value   |  Entry-Key  |
	+-------------+-----------+-------------+
*/

#ifndef __IDXHASH_H__
#define __IDXHASH_H__	1

#include "sysinit.h"

typedef void   *ih_hndlr_t;
typedef uint8	ih_size_t;

#define IH_SIZE_MASK	0xFF

typedef         void (*ih_forall_func) (const char *key, ih_size_t keylen, const char *value, ih_size_t valuelen, void * data);

uint8           ih_hash8 (const char *buf, size_t len, uint8 init);
uint16          ih_hash16 (const char *buf, size_t len, uint8 init);

typedef size_t  ih_entry_ptr_t;

typedef struct ih_header8_s {
    uint8           bucket_size;	// in values
    ih_size_t       key_length;	        // Key length stored in Hash-Map (0 - null term, 1 - variable, n - fixed length in bytes)
    ih_size_t       value_length;	// Value length stored in Hash-Map (0 - null term, 1 - variable, n - fixed length in bytes)
    ih_entry_ptr_t  overflow_hwm;
    ih_entry_ptr_t  overflow_pos;
    ih_entry_ptr_t  free_slot;		// pointer to free list
} ih_header8_t;

#define d_hash8_fixedmap_size(keylen, vallen, bcount, icount) \
	(sizeof (ih_header8_t) + \
	 (bcount) * sizeof (ih_entry_ptr_t) + \
	 (d_align (sizeof (ih_entry_ptr_t) + (keylen) + (vallen))) * (icount) ) * 4 / 3

#define d_ih_get_varlength(vptr)	(*((size_t *) ((char *)(vptr) - sizeof (size_t)) ))

typedef enum ih_errcode_e {
    IH_ERR_SUCCESS = 0,
    IH_BUFFER_OVERFLOW = 1,
    IH_ENTRY_EXISTS = 2,
    IH_ENTRY_NOTFOUND = 3,
    IH_NULL_ENTRY = 4,
} ih_errcode_t;

/*
 * [public] Create Hash-Map Index with inline stored keys and fixed value length
 * - buf: buffer for index
 * - length: buffer length
 * - bucket_size: hash bucket size
 * - key_length: Key length stored in Hash-Map (0 - null term, 1 - variable, n - fixed length in bytes)
 * - value_length: Value length stored in Hash-Map (0 - null term, 1 - variable, n - fixed length in bytes)
 */
ih_errcode_t    ih_init8 (char *buf, size_t length, uint8 bucket_size, ih_size_t key_length, ih_size_t value_length,
	  ih_hndlr_t * hndlr);

/*
 * [public] Add Key-Value to Hash-Map
 * - hndlr: handler to Hash-Map
 * - entrykey: entry key
 * - len: entry key length (0 - when null-terminated string or fixed-length)
 * - value: results pointer to entry value buffer
 * - valuelen: entry value length (0 - when null-terminated string or fixed-length)
 */
ih_errcode_t    ih_hash8_add (ih_hndlr_t hndlr, const char *entrykey, ih_size_t keylen, char **value, ih_size_t valuelen);
ih_errcode_t    ih_hash8_search (ih_hndlr_t hndlr, const char *entrykey, ih_size_t keylen, char **value);
ih_errcode_t    ih_hash8_remove (ih_hndlr_t hndlr, const char *entrykey, ih_size_t keylen);

ih_errcode_t    ih_hash8_forall (const ih_hndlr_t hndlr, const ih_forall_func cb_func, void * data);

/*
 * [public] Get pointer to inline stored key (not aligned)
 * - hndlr: handler to Hash-Map
 * - value: pointer to inlined stored value
 * - return: results pointer to inline stored key
 */
const char *    ih_hash8_v2key (ih_hndlr_t hndlr, const char *value);


#endif
