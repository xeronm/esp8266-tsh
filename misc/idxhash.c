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

Buffer format:

	+-------------+---------------+-----+-----------------+
	| Root Bucket | Overflow Area | ... | Siblings Bucket |
	+-------------+---------------+-----+-----------------+

Inline key and value Entry format:
	+-------------+-----------+-------------+
	| Next Entry  |   Value   |  Entry-Key  |
	+-------------+-----------+-------------+

*/

#include "sysinit.h"
#include "misc/idxhash.h"
#include "core/utils.h"

typedef size_t  ih_entry_ptr_t;

#define IH_HEADER8_VALUE_LNEGTH_MASK	0x3F
typedef struct ih_header8_s {
    uint8           bucket_size;	// in values
    bool            key_nullterm:1;	// is key null terminated?
    uint8           bucket_depth:1;	// Fixme: not realized
    uint8           value_length:6;	// associated fixed value length in bytes
    uint16          bucket_count;	// allocated bucket count
    size_t          overflow_hwm;
    size_t          overflow_pos;
} ih_header8_t;

#define d_obj2hndlr(obj)		(ih_hndlr_t) (obj)
#define d_hndlr2obj(type, hndlr)	(type *) (hndlr)	// TODO: сделать проверку на тип

typedef struct ih_entry_header_s {
    ih_entry_ptr_t  next_entry;
} ih_entry_header_t;

/*
[public] calculate 8bit hash
  - buf: char buffer
  - len: buffer length or 0 if null-terminated
  - init: initialize value
*/
uint8           ICACHE_FLASH_ATTR
ih_hash8 (char *buf, size_t len, uint8 init)
{
    uint8           res = 0xb1101 * init;
    bool            haslen = len;
    while ((haslen && len--) || *buf) {
	res += (uint8) * buf;
	res += (res << 6) + (res >> 2);
	res += (res << 2) + (res >> 6);
	buf++;
    }

    return res;
}

/*
[public] calculate 16bit hash
  - buf: char buffer
  - len: buffer length or 0 if null-terminated
  - init: initialize value
*/
uint16          ICACHE_FLASH_ATTR
ih_hash16 (char *buf, size_t len, uint8 init)
{
    uint16          res = 0xb110101 * init;
    bool            haslen = len;
    while ((haslen && len--) || *buf) {
	res += (uint8) * buf;
	res += (res << 12) + (res >> 4);
	res += (res << 4) + (res >> 12);
	buf++;
    }

    return res;
}


/*
* [public] Create Hash-Map Index with inline stored keys and fixed value length
* - buf: buffer for index
* - length: buffer length
* - bucket_size: hash bucket size
* - depth: bucket tree depth
* - key_nullterm: flag means that keyis null-terminated string
* - value_length: fixed value length stored in Hash-Map
*/
ih_errcode_t    ICACHE_FLASH_ATTR
ih_init8 (char *buf, size_t length, uint8 bucket_size, uint8 depth, bool key_nullterm, uint8 value_length,
	  ih_hndlr_t * hndlr)
{
    size_t          fixed_size = sizeof (ih_header8_t) + bucket_size * sizeof (ih_entry_ptr_t);
    if (length < fixed_size) {
	return IH_BUFFER_OVERFLOW;
    }

    ih_header8_t   *hdr = d_pointer_as (ih_header8_t, buf);
    os_memset (hdr, 0, fixed_size);
    hdr->bucket_size = bucket_size;
    hdr->overflow_hwm = length;
    hdr->overflow_pos = fixed_size;
    hdr->bucket_depth = depth;
    hdr->value_length = d_align (value_length) & IH_HEADER8_VALUE_LNEGTH_MASK;
    hdr->key_nullterm = key_nullterm;

    *hndlr = d_obj2hndlr (hdr);
    return IH_ERR_SUCCESS;
}

bool            ICACHE_FLASH_ATTR
ih_entry_cmp (ih_header8_t * hdr, char *entrykey, size_t len, ih_entry_header_t * entry2)
{
    char           *ptr = d_pointer_add (char, entry2, sizeof (ih_entry_header_t) + hdr->value_length);
    if (hdr->key_nullterm) {
	return ((len == 0) ? os_strcmp (entrykey, ptr) : os_strncmp (entrykey, ptr, len)) == 0;
    }
    else {
	size_t          len2;
	os_memcpy (&len2, ptr, sizeof (size_t));
	if (len == len2) {
	    return (os_memcmp (entrykey, ptr + sizeof (size_t), len) == 0);
	}
    }

    return false;
}


ih_errcode_t    ICACHE_FLASH_ATTR
ih_hash8_add (ih_hndlr_t hndlr, char *entrykey, size_t len, char **value)
{
    ih_header8_t   *hdr = d_hndlr2obj (ih_header8_t, hndlr);
    if (len == 0) {
	if (hdr->key_nullterm) {
	    len = os_strlen (entrykey);
	    if (len == 0)
		return IH_NULL_ENTRY;
	}
	else
	    return IH_NULL_ENTRY;
    }

    uint8           hash0 = ih_hash8 (entrykey, len, 0) % hdr->bucket_size;
    ih_entry_ptr_t *entry_ptr =
	d_pointer_add (ih_entry_ptr_t, hdr, sizeof (ih_header8_t) + hash0 * sizeof (ih_entry_ptr_t));
    ih_entry_header_t *entry2 = NULL;

    if (*entry_ptr) {
	entry2 = d_pointer_add (ih_entry_header_t, hdr, *entry_ptr);
	while (true) {
	    if (ih_entry_cmp (hdr, entrykey, len, entry2)) {
		return IH_ENTRY_EXISTS;
	    }
	    if (!entry2->next_entry)
		break;
	    entry2 = d_pointer_add (ih_entry_header_t, hdr, entry2->next_entry);
	}
	entry_ptr = &entry2->next_entry;
    }
    else {
	*entry_ptr = hdr->overflow_pos;
    }

    if (hdr->overflow_hwm - hdr->overflow_pos <
	sizeof (ih_entry_header_t) + len + (hdr->key_nullterm ? 1 : sizeof (size_t)) + hdr->value_length) {
	return IH_BUFFER_OVERFLOW;
    }
    entry2 = d_pointer_add (ih_entry_header_t, hdr, hdr->overflow_pos);
    entry2->next_entry = 0;
    *entry_ptr = hdr->overflow_pos;

    char           *ptr = d_pointer_add (char, entry2, sizeof (ih_entry_header_t));
    //os_memcpy(ptr, value, hdr->value_length);
    *value = ptr;
    ptr += hdr->value_length;
    if (hdr->key_nullterm) {
	os_memcpy (ptr, entrykey, len);
	ptr += len;
	*(ptr) = '\0';
	ptr++;
    }
    else {
	os_memcpy (ptr, &len, sizeof (size_t));
	ptr += sizeof (size_t);
	os_memcpy (ptr, entrykey, len);
	ptr += len;
    }

    hdr->overflow_pos += d_align (d_pointer_diff (ptr, entry2));

    return IH_ERR_SUCCESS;
}

ih_errcode_t    ICACHE_FLASH_ATTR
ih_hash8_search (ih_hndlr_t hndlr, char *entrykey, size_t len, char **value)
{
    ih_header8_t   *hdr = d_hndlr2obj (ih_header8_t, hndlr);
    uint8           hash0 = ih_hash8 (entrykey, len, 0) % hdr->bucket_size;
    ih_entry_ptr_t *entry_ptr =
	d_pointer_add (ih_entry_ptr_t, hdr, sizeof (ih_header8_t) + hash0 * sizeof (ih_entry_ptr_t));
    ih_entry_header_t *entry2 = NULL;

    if (*entry_ptr) {
	entry2 = d_pointer_add (ih_entry_header_t, hdr, *entry_ptr);
	while (true) {
	    if (ih_entry_cmp (hdr, entrykey, len, entry2)) {
		//os_memcpy(value, d_pointer_add(char, entry2, sizeof(ih_entry_header_t)), hdr->value_length);
		*value = d_pointer_add (char, entry2, sizeof (ih_entry_header_t));
		return IH_ERR_SUCCESS;
	    }
	    if (!entry2->next_entry)
		break;
	    entry2 = d_pointer_add (ih_entry_header_t, hdr, entry2->next_entry);
	}
    }

    return IH_ENTRY_NOTFOUND;
}

/*
* [public] Get pointer to inline stored key
* - hndlr: handler to Hash-Map
* - value: pointer to inlined stored value
* - return: pointer to inline stored key
*/
char           *ICACHE_FLASH_ATTR
ih_hash8_v2key (ih_hndlr_t hndlr, char *value)
{
    ih_header8_t   *hdr = d_hndlr2obj (ih_header8_t, hndlr);
    return d_pointer_add (char, value, hdr->value_length);
}
