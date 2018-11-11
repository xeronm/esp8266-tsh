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

        TODO: Siblings Bucket not realized

Inline key and value Entry format:
	+-------------+-----------+-------------+
	| Next Entry  |   Value   |  Entry-Key  |
	+-------------+-----------+-------------+
*/

#include "sysinit.h"
#include "misc/idxhash.h"
#include "core/utils.h"

#define d_obj2hndlr(obj)		(ih_hndlr_t) (obj)
#define d_hndlr2obj(type, hndlr)	(type *) (hndlr)	// TODO: сделать проверку на тип

#define d_fixed_bucket_size(buckets)		(sizeof (ih_header8_t) + (buckets) * sizeof (ih_entry_ptr_t))

#define d_overflow_entry_len(hdr, entry, len) \
	(len) = sizeof (ih_entry_header_t); \
	(len) += ((hdr)->value_length > 1) ? (hdr)->value_length : \
		     (((hdr)->value_length) ? sizeof (size_t) + *d_pointer_add (size_t, (entry), (len)): 1 + os_strlen(d_pointer_add (char, (entry), (len)))); \
	(len) += ((hdr)->key_length > 1) ? (hdr)->key_length : \
		     (((hdr)->key_length) ? sizeof (size_t) + *d_pointer_add (size_t, (entry), (len)): 1 + os_strlen(d_pointer_add (char, (entry), (len)))); \
	(len) = d_align ((len));

typedef struct ih_entry_header_s {
    ih_entry_ptr_t  next_entry;
} ih_entry_header_t;

typedef struct ih_free_slot_s {
    ih_entry_ptr_t  next_slot;
    ih_entry_ptr_t  length;
} ih_free_slot_t;

#define IH_FREE_SLOT	((ih_entry_ptr_t) ~0)

/*
[public] calculate 8bit hash
  - buf: char buffer
  - len: buffer length or 0 if null-terminated
  - init: initialize value
*/
uint8           ICACHE_FLASH_ATTR
ih_hash8 (const char *buf, size_t len, uint8 init)
{
    uint8           res = 0xb1101 * init;
    bool            haslen = len;
    while ((haslen) ? (len--) : *buf) {
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
ih_hash16 (const char *buf, size_t len, uint8 init)
{
    uint16          res = 0xb110101 * init;
    bool            haslen = len;
    while ((haslen) ? (len--) : *buf) {
	res += (uint8) * buf;
	res += (res << 12) + (res >> 4);
	res += (res << 4) + (res >> 12);
	buf++;
    }

    return res;
}

ih_errcode_t    ICACHE_FLASH_ATTR
ih_compact8 (ih_hndlr_t hndlr, size_t * size_free)
{
    *size_free = 0;
    ih_header8_t   *hdr = d_hndlr2obj (ih_header8_t, hndlr);
    if (!hdr->free_slot)
        return IH_ERR_SUCCESS;

    // 1st pass - reindex
    uint8 i;
    for (i = 0; i < hdr->bucket_size; i++) {
        ih_entry_ptr_t *entry_ptr =
	    d_pointer_add (ih_entry_ptr_t, hdr, sizeof (ih_header8_t) + i * sizeof (ih_entry_ptr_t));
        ih_entry_header_t *entry = NULL;
        if (*entry_ptr) {
	    entry = d_pointer_add (ih_entry_header_t, hdr, *entry_ptr);
	    while (true) {
                ih_entry_ptr_t     next_ptr = entry->next_entry;
                entry->next_entry = i;
	        if (!next_ptr)
		    break;
	        entry = d_pointer_add (ih_entry_header_t, hdr, next_ptr);
	    }
            *entry_ptr = 0;
	}
    }

    ih_entry_ptr_t offset = 0;
    // 2st pass - restore chains and compact
    ih_entry_ptr_t entry_ptr = d_fixed_bucket_size (hdr->bucket_size);
    while (entry_ptr < hdr->overflow_pos) {
        ih_entry_header_t *entry = d_pointer_add (ih_entry_header_t, hdr, entry_ptr);
	if (entry->next_entry == IH_FREE_SLOT) {
            ih_free_slot_t * free_slot = d_pointer_as (ih_free_slot_t, entry);
            offset += free_slot->length;
            entry_ptr += free_slot->length;
	}
        else {
            ih_entry_ptr_t *entry_ptr2 =
	        d_pointer_add (ih_entry_ptr_t, hdr, sizeof (ih_header8_t) + entry->next_entry * sizeof (ih_entry_ptr_t));

            size_t len;
            d_overflow_entry_len(hdr, entry, len);

            entry->next_entry = *entry_ptr2;
            *entry_ptr2 = entry_ptr - offset;
            if (offset)
                os_memcpy((char *)entry - offset, entry, len);

            entry_ptr += len;
	}
    }

    hdr->overflow_pos -= offset;
    *size_free = offset;

    return IH_ERR_SUCCESS;
}

/*
 * [public] Create Hash-Map Index with inline stored keys and fixed value length
 * - buf: buffer for index
 * - length: buffer length
 * - bucket_size: hash bucket size
 * - key_length: Key length stored in Hash-Map (0 - null term, 1 - variable, n - fixed length in bytes)
 * - value_length: Value length stored in Hash-Map (0 - null term, 1 - variable, n - fixed length in bytes)
 */
ih_errcode_t    ICACHE_FLASH_ATTR
ih_init8 (char *buf, size_t length, uint8 bucket_size, uint8 key_length, uint8 value_length,
	  ih_hndlr_t * hndlr)
{
    size_t          fixed_size = d_fixed_bucket_size (bucket_size);
    if (length < fixed_size) {
	return IH_BUFFER_OVERFLOW;
    }

    ih_header8_t   *hdr = d_pointer_as (ih_header8_t, buf);
    os_memset (hdr, 0, fixed_size);
    hdr->free_slot = 0;
    hdr->bucket_size = bucket_size;
    hdr->overflow_hwm = length;
    hdr->overflow_pos = fixed_size;
    hdr->value_length = (value_length > 1) ? d_align (value_length) : value_length;
    hdr->key_length = key_length;

    *hndlr = d_obj2hndlr (hdr);
    return IH_ERR_SUCCESS;
}

LOCAL bool      ICACHE_FLASH_ATTR
ih_entry_cmp (ih_header8_t * hdr, const char *entrykey, size_t len, ih_entry_header_t * entry)
{
    char           *ptr = d_pointer_add (char, entry, sizeof (ih_entry_header_t));

    if (!hdr->value_length) {
        ptr += os_strlen(ptr) + 1;
    } else if (hdr->value_length == 1) {
        ptr += sizeof (size_t) + *((size_t *) ptr);
    }
    else {
        ptr += hdr->value_length;
    }

    if (! hdr->key_length) {
	return ((len == 0) ? os_strcmp (entrykey, ptr) : os_strncmp (entrykey, ptr, len)) == 0;
    }
    else if (hdr->key_length == 1) {
	size_t          len2;
	os_memcpy (&len2, ptr, sizeof (size_t));
	if (len == len2) {
	    return (os_memcmp (entrykey, ptr + sizeof (size_t), len) == 0);
	}
    }
    else {
	return (os_memcmp (entrykey, ptr, MIN(len, hdr->key_length)) == 0);
    }

    return false;
}

/*
 * [public] Add Key-Value to Hash-Map
 * - hndlr: Hash-Map handler
 * - entrykey: entry key
 * - len: entry key length
 * - value: returns pointer to entry value buffer
 * - valuelen: entry value length
 */
ih_errcode_t    ICACHE_FLASH_ATTR
ih_hash8_add (ih_hndlr_t hndlr, const char *entrykey, size_t len, char **value, size_t valuelen)
{
    ih_header8_t   *hdr = d_hndlr2obj (ih_header8_t, hndlr);
    if (hdr->key_length == 0) {
        len = os_strlen (entrykey);
    } 
    else if (hdr->key_length > 1) {
        len = hdr->key_length;
    }
    if (len == 0)
        return IH_NULL_ENTRY;

    // optimistic algorythm, first check space and comapct, then check existance 
    size_t value_len = (hdr->value_length > 1) ? hdr->value_length : 
                           ((hdr->value_length) ? sizeof (size_t) : 1) + valuelen;
    size_t entry_len = sizeof (ih_entry_header_t) + value_len;
    entry_len += (hdr->key_length > 1) ? hdr->key_length : 
                       ((hdr->key_length) ? sizeof (size_t) : 1) + len;

    if (hdr->overflow_hwm - hdr->overflow_pos < entry_len) {
        size_t size_free;
        ih_compact8 (hndlr, &size_free);
        if (size_free < entry_len)
	    return IH_BUFFER_OVERFLOW;
    }

    uint8           hash0 = ih_hash8 (entrykey, len, 0) % hdr->bucket_size;
    ih_entry_ptr_t *entry_ptr =
	d_pointer_add (ih_entry_ptr_t, hdr, sizeof (ih_header8_t) + hash0 * sizeof (ih_entry_ptr_t));
    ih_entry_header_t *entry = NULL;

    if (*entry_ptr) {
	entry = d_pointer_add (ih_entry_header_t, hdr, *entry_ptr);
	while (true) {
	    if (ih_entry_cmp (hdr, entrykey, len, entry)) {
		return IH_ENTRY_EXISTS;
	    }
	    if (!entry->next_entry)
		break;
	    entry = d_pointer_add (ih_entry_header_t, hdr, entry->next_entry);
	}
	entry_ptr = &entry->next_entry;
    }

    entry = d_pointer_add (ih_entry_header_t, hdr, hdr->overflow_pos);
    entry->next_entry = 0;
    *entry_ptr = hdr->overflow_pos;

    char           *ptr = d_pointer_add (char, entry, sizeof (ih_entry_header_t));
    if (hdr->value_length == 1) {
        *((size_t *) ptr) = valuelen;
        *value = ptr + sizeof (size_t);
    }
    else
        *value = ptr;

    ptr += value_len;
    if (! hdr->key_length) {
	os_memcpy (ptr, entrykey, len);
	ptr += len;
	*(ptr) = '\0';
        ptr ++;

    }
    else if (hdr->key_length == 1) {
	os_memcpy (ptr, &len, sizeof (size_t));
	ptr += sizeof (size_t);
	os_memcpy (ptr, entrykey, len);
	ptr += len;
    }
    else {
	os_memcpy (ptr, entrykey, len);
	ptr += hdr->key_length;
    }

    hdr->overflow_pos += d_align (d_pointer_diff (ptr, entry));

    return IH_ERR_SUCCESS;
}

ih_errcode_t    ICACHE_FLASH_ATTR
ih_hash8_search (ih_hndlr_t hndlr, const char *entrykey, size_t len, char **value)
{
    ih_header8_t   *hdr = d_hndlr2obj (ih_header8_t, hndlr);

    if (hdr->key_length == 0) {
        len = os_strlen (entrykey);
    } 
    else if (hdr->key_length > 1) {
        len = hdr->key_length;
    }
    if (len == 0)
        return IH_NULL_ENTRY;

    uint8           hash0 = ih_hash8 (entrykey, len, 0) % hdr->bucket_size;
    ih_entry_ptr_t *entry_ptr =
	d_pointer_add (ih_entry_ptr_t, hdr, sizeof (ih_header8_t) + hash0 * sizeof (ih_entry_ptr_t));
    ih_entry_header_t *entry = NULL;

    if (*entry_ptr) {
	entry = d_pointer_add (ih_entry_header_t, hdr, *entry_ptr);
	while (true) {
	    if (ih_entry_cmp (hdr, entrykey, len, entry)) {
		//os_memcpy(value, d_pointer_add(char, entry, sizeof(ih_entry_header_t)), hdr->value_length);
		*value = d_pointer_add (char, entry, sizeof (ih_entry_header_t));
		return IH_ERR_SUCCESS;
	    }
	    if (!entry->next_entry)
		break;
	    entry = d_pointer_add (ih_entry_header_t, hdr, entry->next_entry);
	}
    }

    return IH_ENTRY_NOTFOUND;
}

ih_errcode_t    ICACHE_FLASH_ATTR
ih_hash8_remove (ih_hndlr_t hndlr, const char *entrykey, size_t len) 
{
    ih_header8_t   *hdr = d_hndlr2obj (ih_header8_t, hndlr);

    if (hdr->key_length == 0) {
        len = os_strlen (entrykey);
    } 
    else if (hdr->key_length > 1) {
        len = hdr->key_length;
    }
    if (len == 0)
        return IH_NULL_ENTRY;

    uint8           hash0 = ih_hash8 (entrykey, len, 0) % hdr->bucket_size;
    ih_entry_ptr_t *entry_ptr =
	d_pointer_add (ih_entry_ptr_t, hdr, sizeof (ih_header8_t) + hash0 * sizeof (ih_entry_ptr_t));
    ih_entry_header_t *entry = NULL;

    if (*entry_ptr) {
        ih_entry_header_t *entry_prev = NULL;
	entry = d_pointer_add (ih_entry_header_t, hdr, *entry_ptr);
	while (true) {
	    if (ih_entry_cmp (hdr, entrykey, len, entry)) {
	        if (entry_prev)
                    entry_prev->next_entry = entry->next_entry;
                else
                    *entry_ptr = entry->next_entry;

                size_t len;
                d_overflow_entry_len(hdr, entry, len);

		ih_free_slot_t * free_slot = d_pointer_as (ih_free_slot_t, entry);
		free_slot->length = len;
                free_slot->next_slot = IH_FREE_SLOT;
                hdr->free_slot = 1;

		return IH_ERR_SUCCESS;
	    }
	    if (!entry->next_entry)
		break;
            entry_prev = entry;
	    entry = d_pointer_add (ih_entry_header_t, hdr, entry->next_entry);
	}
    }

    return IH_ENTRY_NOTFOUND;
}

/*
 * [public] Get pointer to inline stored key (not aligned)
 * - hndlr: handler to Hash-Map
 * - value: pointer to inlined stored value
 * - key: results pointer to inline stored key
 * - return: length of inline stored key
 */
const char *    ICACHE_FLASH_ATTR
ih_hash8_v2key (ih_hndlr_t hndlr, const char *value)
{
    ih_header8_t   *hdr = d_hndlr2obj (ih_header8_t, hndlr);

    if (!hdr->value_length) {
        value += os_strlen(value) + 1;
    } 
    else if (hdr->value_length == 1) {
        value += d_ih_get_varlength(value);
    }
    else
        value += hdr->value_length;

    return value;
}
