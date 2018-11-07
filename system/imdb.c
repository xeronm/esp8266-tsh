/* 
 * ESP8266 In-Memory Database
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

#include "sysinit.h"
#include "core/utils.h"
#include "core/logging.h"
#include "system/imdb.h"
#include "crypto/crc.h"

#define IMDB_PCT_FREE_MAX		30	// 60% (4 bits, 1unit = 2%)
#define IMDB_BLOCK_SIZE_MIN		512	// 512 Bytes

#ifdef IMDB_SMALL_RAM
#define IMDB_BLOCK_SIZE_DEFAULT	1024	// 1 KBytes
#define IMDB_FIRST_PAGE_BLOCKS_MIN	1	// 1 blocks in first page
#define IMDB_CURSOR_PAGE_BLOCKS	2	//
#else
#define IMDB_BLOCK_SIZE_DEFAULT	4096	// 4 KBytes
#define IMDB_FIRST_PAGE_BLOCKS_MIN	4	// 4 blocks in first page
#define IMDB_CURSOR_PAGE_BLOCKS	4	//
#endif

#define IMDB_FIRST_PAGE_BLOCKS_DIV	2

// determines the maximum count of sequential skip of the free slot after which the slot will be removed from free list
// used only with variable storages
#define IMDB_SLOT_SKIP_COUNT_MAX	16

#define	IMDB_SERVICE_NAME		"imdb"

#define IMDB_CLS_CURSOR			"imdb$cursors"

#define d_obj2hndlr(obj)		(imdb_hndlr_t) (obj)
#define d_hndlr2obj(type, hndlr)	(type *) (hndlr)	// TODO: сделать проверку на тип

struct imdb_class_s;
struct imdb_page_s;
struct imdb_block_s;
struct imdb_block_page_s;
struct imdb_block_class_s;

typedef union class_ptr_u {
    struct imdb_block_class_s *mptr;
    size_t fptr;
} class_ptr_t;

typedef struct imdb_s {
    imdb_def_t      db_def;
    obj_size_t      obj_bsize_max;
    imdb_stat_t     stat;
    class_ptr_t     class_first;
    class_ptr_t     class_last;
    imdb_hndlr_t    hcurs;
} imdb_t;

typedef struct imdb_file_s {
    uint16          version;
    uint16          crc16;
    uint32          scn;
    block_size_t    block_size;
    class_ptr_t     class_last;
    size_t          file_size;
    size_t          file_hwm;
} imdb_file_t;

#define	d_stat_alloc(imdb, size)	{ (imdb)->stat.mem_alloc += (size); }
#define	d_stat_free(imdb, size)		{ (imdb)->stat.mem_free += (size); }
#define	d_stat_block_alloc(imdb, blks)	{ (imdb)->stat.block_alloc += (blks); }
#define	d_stat_block_init(imdb)		{ (imdb)->stat.block_init++; }
#define	d_stat_block_recycle(imdb)	{ (imdb)->stat.block_recycle++; }
#define	d_stat_page_alloc(imdb)		{ (imdb)->stat.page_alloc++; }
#define	d_stat_page_free(imdb)		{ (imdb)->stat.page_free++; }
#define	d_stat_slot_free(imdb)		{ (imdb)->stat.slot_free++; }
#define	d_stat_slot_data(imdb, slot_ss)	{ (imdb)->stat.slot_data++; (imdb)->stat.slot_skipscan += (slot_ss); }
#define	d_stat_slot_split(imdb)		{ (imdb)->stat.slot_split++; }

typedef enum PACKED imdb_data_slot_type_s {
    DATA_SLOT_TYPE_1 = 0,
    DATA_SLOT_TYPE_2 = 1,
    DATA_SLOT_TYPE_3 = 2,
    DATA_SLOT_TYPE_4 = 3
} imdb_data_slot_type_t;

typedef enum PACKED imdb_lock_s {
    DATA_LOCK_NONE = 0,
    DATA_LOCK_WRITE = 1,
    DATA_LOCK_RD_EXCLUSIVE = 2,
    DATA_LOCK_EXCLUSIVE = 3
} imdb_lock_t;

typedef struct imdb_class_s {
    imdb_class_def_t cdef;
    imdb_t         *imdb;
    struct imdb_block_class_s *class_prev;
    struct imdb_block_class_s *class_next;
    struct imdb_block_page_s *page_first;
    struct imdb_block_page_s *page_last;
    struct imdb_block_page_s *page_fl_first;
    class_pages_t   page_count;
    obj_size_t      obj_bsize_min;
    imdb_data_slot_type_t ds_type:2;
    imdb_lock_t     lock_flags:2;
    uint8           reserved:4;
} imdb_class_t;


typedef struct imdb_page_s {
    struct imdb_block_class_s *class_block;
    struct imdb_block_page_s *page_next;
    struct imdb_block_page_s *page_prev;
    struct imdb_block_page_s *page_fl_next;	// previous page with not empty free list
    class_pages_t   page_index;
    page_blocks_t   blocks;
    page_blocks_t   alloc_hwm;
    page_blocks_t   block_fl_first;	// page free list pointer to first block
} imdb_page_t;

/* 
      +-+-+-+-+-+-+-+
      |    Class    | - Set of objects with same storage parameters
      +-+-+-+-+-+-+-+
      |    Pages    | - minimum allocatable unit of memory for storage [pages_max - class_pages_t=uint32]
      +-+-+-+-+-+-+-+
      |    Blocks   | - (block) minimum unit of memory guards by TX-control and CRC [blocks_max_per_page - uint16, block_size_max - uint16]
      +-+-+-+-+-+-+-+
	  
  Fixed/Variable Object Blocks.
	- use IMDB_BLOCK_UNIT_ALIGN adressing inside block structures
	- there is no need to coalesce free area because all of objects have fixed length. so the free area list managed by LIFO

       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |            CRC_16             |   Page Block  |Lck| Tx Length |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |Flg|  Relative Free Slot Ptr   |Free Block Prev|Free Block Next|
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                             Data                              |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                              ...                              |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                         ... Tx List                           |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

Data Slot Type#1 (Fixed length, recycle, no Tx)
       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                           User Data ..                        |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

Data Slot Type#2 (Fixed length, with Tx)
       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |Lck|        Block Offset       |Flg|  Reserved  |   Tx Slot    |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                           User Data ...                       |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

Data Slot Type#3 (Variable length, recycle, no Tx)
	  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	  |Lck|       Prev. Offset        |Flg|          Length           |
	  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	  |                           User Data ..                        |
	  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	  |    Tx Slot    |    Reserved   |Flg|          Length           |
	  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

Data Slot Type#4 (Variable length, with Tx)
       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |Lck|        Block Offset       |Flg|          Length           |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                           User Data ...                       |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |    Tx Slot    |    Reserved   |Flg|          Length           |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

Free Slot (Footer used for variable length or with Tx)

       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |Lck|         Next Offset       |Flg|          Length           |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                              ...                              |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |    Tx Slot    |   Skip count  |Flg|        Slot Offset        |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

	  Slot Offset	- when data slot deleted, used to coalesce free slot area
	  Skip Count	- used for variable length for migrate to the end of the LIFO Free-List

*/

typedef enum PACKED imdb_block_type_s {
    BLOCK_TYPE_NONE = 0,
    BLOCK_TYPE_PAGE = 1,
    BLOCK_TYPE_CLASS = 2,
} imdb_block_type_t;

#define SLOT_FLAG_FREE			1U
#define SLOT_FLAG_UNFORMATTED	2U
#define SLOT_FLAG_DATA			3U

#define d_page_block_byidx(page_block, bidx, bsize)	d_pointer_add(imdb_block_t, (page_block), (bidx-1)*(bsize))

// convert block pointer to size
#define d_bptr_size(bptr) \
	((size_t)((bptr) << IMDB_BLOCK_UNIT_ALIGN))
// convert size to block pointer
#define d_size_bptr(ptr) \
	(obj_size_t)((size_t)(ptr) >> IMDB_BLOCK_UNIT_ALIGN)

// align size to IMDB_BLOCK_UNIT_ALIGN
#define d_size_bptr_align(ptr)	(obj_size_t)(((size_t)(ptr)+(size_t)((1<<IMDB_BLOCK_UNIT_ALIGN)-1)) >> IMDB_BLOCK_UNIT_ALIGN)
// align size to IMDB_BLOCK_UNIT_ALIGN
#define d_size_align(size) \
	(block_size_t)( (block_size_t)(((size)+(block_size_t)((1<<IMDB_BLOCK_UNIT_ALIGN)-1))>>IMDB_BLOCK_UNIT_ALIGN)<<IMDB_BLOCK_UNIT_ALIGN )

// return block FreeSlot
#define d_block_slot_free(block) \
	( ((block)->free_offset)? d_pointer_add(imdb_slot_free_t, (block), d_bptr_size((block)->free_offset)): NULL)

// check that class slot has footer
#define d_block_slot_has_footer(dbclass) \
	((dbclass)->ds_type >= DATA_SLOT_TYPE_3)
// return FreeSlot Footer
#define d_block_slot_free_footer(slot) \
	d_pointer_add(imdb_slot_footer_t, (slot), d_bptr_size((slot)->length) - sizeof(imdb_slot_footer_t));
// return Slot Footer
#define d_block_slot_footer(slot) \
	d_pointer_add(imdb_slot_footer_t, (slot), d_bptr_size((slot)->length) - sizeof(imdb_slot_footer_t));

// return next FreeSlot
#define d_block_next_slot_free(block, fslot) \
	( ((fslot)->next_offset)? d_pointer_add(imdb_slot_free_t, (block), d_bptr_size((fslot)->next_offset)): NULL)
// return pointer of page owns block
#define d_block_page(block, bsize) \
	( d_pointer_add(imdb_block_page_t, (block), ((block)->block_index-1)*(bsize) ))
// return block user data upper limit in block units
#define d_block_upper_data_blimit(imdb, block) \
	(d_size_bptr((imdb)->db_def.block_size) - (block)->footer_offset)
// return block user data lower limit in block units
#define d_block_lower_data_blimit(block) \
	(d_size_bptr(block_header_size[(block)->btype]))
// return block user data lower limit in bytes
#define d_block_lower_data_limit(block)	\
	(block_header_size[(block)->btype])

#define d_block_check_class(block) \
	d_assert((block)->flags & (BLOCK_FLAG_CLASS_HEADER | BLOCK_FLAG_PAGE_HEADER), "block=%p, flags=%u", (block), (block)->flags);

#define d_block_check_page(block) \
	d_assert((block)->flags & BLOCK_FLAG_PAGE_HEADER, "block=%p, flags=%u", (block), (block)->flags);

typedef struct imdb_block_s {
    uint16          crc16;
    page_blocks_t   block_index;	// index of block
    imdb_lock_t     lock_flags:2;
    uint8           footer_offset:6;	// slot offset from the ending of block
    imdb_block_type_t btype:2;
    uint16          free_offset:14;	// offset in IMDB_BLOCK_UNIT_ALIGN
    page_blocks_t   block_fl_next;	// next block inside page with not empty free list 
} imdb_block_t;

typedef struct imdb_block_page_s {
    imdb_block_t    block;
    imdb_page_t     page;
} imdb_block_page_t;

typedef struct imdb_block_class_s {
    imdb_block_t    block;
    imdb_page_t     page;
    imdb_class_t    dbclass;
} imdb_block_class_t;

//typedef       struct imdb_slot_data1_s {
//} imdb_slot_data1_t;

typedef struct imdb_slot_data2_s {
    imdb_lock_t     lock_flags:2;
    uint16          block_offset:14;
    uint8           flags:2;
    uint8           reserved1:6;
    uint8           tx_slot;
} imdb_slot_data2_t;

typedef struct imdb_slot_data4_s {
    imdb_lock_t     lock_flags:2;
    uint16          block_offset:14;
    uint8           flags:2;
    uint16          length:14;
} imdb_slot_data4_t;

typedef struct imdb_slot_footer_s {
    uint8           tx_slot;
    uint8           skip_count;
    uint8           flags:2;
    uint16          length:14;
} imdb_slot_footer_t;

typedef struct imdb_slot_free_s {
    imdb_lock_t     lock:2;
    uint16          next_offset:14;
    uint8           flags:2;
    uint16          length:14;
} imdb_slot_free_t;

typedef struct imdb_cursor_s {
    os_time_t       otime;	// open time
    imdb_class_t   *dbclass;
    imdb_rowid_t    rowid_first;
    imdb_rowid_t    rowid_last;
    imdb_block_page_t *page_last;
    uint32          fetch_recs;
    imdb_access_path_t access_path;
} imdb_cursor_t;

typedef struct imdb_free_slot_find_ctx_s {
    imdb_block_page_t *page_block;
    imdb_block_page_t *page_block_fl_prev;
    imdb_block_t   *block;
    imdb_block_t   *block_fl_prev;
    imdb_slot_free_t *slot_free;
    imdb_slot_free_t *slot_free_prev;
} imdb_free_slot_find_ctx_t;

// imdb_data_slot_type_st
LOCAL const obj_size_t data_slot_type_bsize[] = {
    0,
    d_size_bptr (sizeof (imdb_slot_data2_t)),
    d_size_bptr (sizeof (imdb_slot_data4_t) + sizeof (imdb_slot_footer_t)),
    d_size_bptr (sizeof (imdb_slot_data4_t) + sizeof (imdb_slot_footer_t)),
};

// imdb_block_type_t
LOCAL const obj_size_t block_header_size[] = {
    sizeof (imdb_block_t),
    sizeof (imdb_block_page_t),
    sizeof (imdb_block_class_t),
};

/*
[inline] Insert Page into Class LIFO Free-List
  - result: true if free-list has been created
*/
INLINED bool    ICACHE_FLASH_ATTR
imdb_fl_insert_page (imdb_class_t * dbclass, imdb_block_page_t * page_block)
{
    imdb_page_t    *page = &page_block->page;
    page->page_fl_next = dbclass->page_fl_first;
    dbclass->page_fl_first = page_block;

    return (!page->page_fl_next);
}

/*
[inline] Insert Block into Page LIFO Free-List
  - result: true if free-list has been created
*/
INLINED bool    ICACHE_FLASH_ATTR
imdb_fl_insert_block (imdb_class_t * dbclass, imdb_block_page_t * page_block, imdb_block_t * block)
{
    imdb_page_t    *page = &page_block->page;

    block->block_fl_next = page->block_fl_first;
    page->block_fl_first = block->block_index;

    return (!block->block_fl_next);
}

/*
[inline] Insert Slot into Block LIFO Free-List
  - result: true if free-list has been created
*/
INLINED bool    ICACHE_FLASH_ATTR
imdb_fl_insert_slot (imdb_block_t * block, imdb_slot_free_t * slot_free)
{
    slot_free->next_offset = block->free_offset;
    block->free_offset = d_size_bptr (d_pointer_diff (slot_free, block));

    return (!slot_free->next_offset);
}

INLINED void   *ICACHE_FLASH_ATTR
imdb_data2_slot_init (imdb_block_t * block, imdb_slot_free_t * slot_free)
{
    imdb_slot_data2_t *data2 = (imdb_slot_data2_t *) slot_free;
    os_memset (data2, 0, sizeof (imdb_slot_data2_t));
    data2->flags = SLOT_FLAG_DATA;
    data2->block_offset = d_size_bptr (d_pointer_diff (slot_free, block));

    return d_pointer_add (void, data2, sizeof (imdb_slot_data2_t));
}

/*
[inline] Initialize data slot of Type4
*/
INLINED void   *ICACHE_FLASH_ATTR
imdb_data4_slot_init (imdb_block_t * block, imdb_slot_free_t * slot_free, obj_size_t slot_bsize)
{
    imdb_slot_data4_t *data4 = (imdb_slot_data4_t *) slot_free;
    os_memset (data4, 0, sizeof (imdb_slot_data4_t));
    data4->flags = SLOT_FLAG_DATA;
    data4->block_offset = d_size_bptr (d_pointer_diff (data4, block));
    data4->length = slot_bsize;

    imdb_slot_footer_t *footer =
	d_pointer_add (imdb_slot_footer_t, slot_free, d_bptr_size (slot_bsize) - sizeof (imdb_slot_footer_t));
    footer->flags = SLOT_FLAG_DATA;
    footer->length = slot_bsize;

    return d_pointer_add (void, data4, sizeof (imdb_slot_data4_t));
}

/*
[inline] Shift FreeSlot offset to next DataSlot
  - block:
  - [in/out] slot_offset: shift from FreeSlot to next DataSlot
*/
INLINED void    ICACHE_FLASH_ATTR
imdb_block_slot_free_next (imdb_block_t * block, block_size_t * slot_offset)
{
    imdb_slot_free_t *slot_free = d_pointer_add (imdb_slot_free_t, (block), d_bptr_size (*slot_offset));
    d_assert (slot_free->flags == SLOT_FLAG_FREE, "slot=%p, flags=%u", slot_free, slot_free->flags);
    (*slot_offset) += slot_free->length;
}

/*
[private] Get offset of last free slot in block
  - dbclass: 
  - block:
  - [out] last_offset: result last FreeSlot offset
*/
INLINED void    ICACHE_FLASH_ATTR
imdb_block_slot_free_last (imdb_class_t * dbclass, imdb_block_t * block, block_size_t * last_offset)
{
    block_size_t    boffset = d_block_upper_data_blimit (dbclass->imdb, block);

    if (d_block_slot_has_footer (dbclass)) {
	imdb_slot_footer_t *slot_footer;
	slot_footer = d_pointer_add (imdb_slot_footer_t, block, d_bptr_size (boffset) - sizeof (imdb_slot_footer_t));
	d_assert (slot_footer->flags == SLOT_FLAG_FREE, "footer=%p, flags=%u", slot_footer, slot_footer->flags);
	d_assert (boffset > slot_footer->length, "offset=%u, len=%u", boffset, slot_footer->length);
	*last_offset = boffset - slot_footer->length;
    }
    else {
	d_assert (!dbclass->cdef.opt_variable, "variable size");
	*last_offset = boffset - (boffset - d_block_lower_data_blimit (block)) % dbclass->obj_bsize_min;
    }
}

/*
[private] Shift offset to previous DataSlot and return data pointer (only for recycled storage). There are no bounds checks.
  - dbclass: 
  - block:
  - [in/out] slot_offset: shift from DataSlot to previous DataSlot
  - [out] ptr: previous DataSlot data pointer
*/
LOCAL void      ICACHE_FLASH_ATTR
imdb_block_slot_prev (imdb_class_t * dbclass, imdb_block_t * block, block_size_t * slot_offset, void **ptr)
{
    imdb_slot_footer_t *slot_footer;
    //imdb_slot_data2_t* slot_data2;
    imdb_slot_data4_t *slot_data4;

#ifdef ASSERT_DEBUG
    block_size_t    offset_limit;
#endif
    block_size_t    offset = *slot_offset;

    switch (dbclass->ds_type) {
    case DATA_SLOT_TYPE_1:
#ifdef ASSERT_DEBUG
	offset_limit = d_block_lower_data_blimit (block);
#endif
	d_assert (offset - dbclass->obj_bsize_min >= offset_limit, "offset=%u, objlen=%u, limit=%u", offset,
		  dbclass->obj_bsize_min, offset_limit);
	offset -= dbclass->obj_bsize_min;
	*ptr = d_pointer_add (void, block, d_bptr_size (offset));
	break;
    case DATA_SLOT_TYPE_3:
#ifdef ASSERT_DEBUG
	offset_limit = d_block_lower_data_blimit (block);
#endif
	d_assert (offset - data_slot_type_bsize[DATA_SLOT_TYPE_3] >= offset_limit, "offset=%u, shlen=%u, limit=%u",
		  offset, data_slot_type_bsize[DATA_SLOT_TYPE_3], offset_limit);
	slot_footer = d_pointer_add (imdb_slot_footer_t, block, d_bptr_size (offset) - sizeof (imdb_slot_footer_t));

	d_assert (slot_footer->flags == SLOT_FLAG_DATA, "flags=%u", slot_footer->flags);
	d_assert (offset - slot_footer->length >= offset_limit, "offset=%u, len=%u, limit=%u", offset,
		  slot_footer->length, offset_limit);

	d_log_dprintf (IMDB_SERVICE_NAME, "slot_prev: footer=%p, flags=%u, len=%u", slot_footer, slot_footer->flags,
		       slot_footer->length);
	offset -= slot_footer->length;

	slot_data4 = d_pointer_add (imdb_slot_data4_t, block, d_bptr_size (offset));
	d_assert (slot_data4->flags == SLOT_FLAG_DATA, "flags=%u", slot_footer->flags);
	d_log_dprintf (IMDB_SERVICE_NAME, "slot_prev3: slot=%p, flags=%u", slot_data4, slot_data4->flags);

	*ptr = d_pointer_add (void, slot_data4, sizeof (imdb_slot_data4_t));
	break;
    case DATA_SLOT_TYPE_2:
    case DATA_SLOT_TYPE_4:
    default:
	d_assert (false, "ds_type=%u", dbclass->ds_type);
    }

    *slot_offset = offset;
}

/*
[private] Shift offset to next data/free slot and return next data pointer (NULL for free slot). There are no bounds checks.
  - dbclass: 
  - block:
  - [in/out] slot_offset: shift from current DataSlot/FreeSlot to next DataSlot/FreeSlot
  - [out] ptr: current DataSlot data pointer
*/
LOCAL void      ICACHE_FLASH_ATTR
imdb_block_slot_next (imdb_class_t * dbclass, imdb_block_t * block, block_size_t * slot_offset, void **ptr)
{
    imdb_slot_free_t *slot_free;

#ifdef ASSERT_DEBUG
    block_size_t    offset_limit;
#endif
    block_size_t    offset = *slot_offset;
    block_size_t    offset_add = 0;
    switch (dbclass->ds_type) {
    case DATA_SLOT_TYPE_1:
#ifdef ASSERT_DEBUG
	offset_limit = d_block_upper_data_blimit (dbclass->imdb, block);
#endif
	d_assert (offset + dbclass->obj_bsize_min <= offset_limit, "offset=%u, objlen=%u, limit=%u", offset,
		  dbclass->obj_bsize_min, offset_limit);
	*ptr = d_pointer_add (void, block, d_bptr_size (offset));
	offset += dbclass->obj_bsize_min;
	break;
    case DATA_SLOT_TYPE_2:
    case DATA_SLOT_TYPE_3:
    case DATA_SLOT_TYPE_4:
#ifdef ASSERT_DEBUG
	offset_limit = d_block_upper_data_blimit (dbclass->imdb, block);
#endif
	d_assert (offset + data_slot_type_bsize[dbclass->ds_type] <= offset_limit, "offset=%u, shlen=%u, limit=%u",
		  offset, data_slot_type_bsize[dbclass->ds_type], offset_limit);
	slot_free = d_pointer_add (imdb_slot_free_t, block, d_bptr_size (offset));
	d_assert (slot_free->flags == SLOT_FLAG_DATA
		  || slot_free->flags == SLOT_FLAG_FREE, "flags=%u", slot_free->flags);

	if (slot_free->flags == SLOT_FLAG_DATA) {
	    *ptr = d_pointer_add (void, slot_free, sizeof (imdb_slot_free_t));
	    offset_add =
		(dbclass->ds_type ==
		 DATA_SLOT_TYPE_2) ? (dbclass->obj_bsize_min +
				      data_slot_type_bsize[dbclass->ds_type]) : slot_free->length;
	}
	else {
	    *ptr = NULL;
	    offset_add = slot_free->length;
	}
	d_assert (offset + offset_add <= offset_limit, "offset=%u, len=%u, limit=%u", offset, offset_add, offset_limit);
	offset += offset_add;
	d_log_dprintf (IMDB_SERVICE_NAME, "slot_next: slot=%p, flags=%u, len=%u, next_offset=%u", slot_free,
		       slot_free->flags, offset_add, offset);
	break;
    default:
	d_assert (false, "ds_type=%u", dbclass->ds_type);
    }

    *slot_offset = offset;
}

/*
[private] Initialize Freeslot for new block
  - dbclass: 
  - page_block:
  - block:
*/
LOCAL void      ICACHE_FLASH_ATTR
imdb_block_slot_init (imdb_class_t * dbclass, imdb_block_page_t * page_block, imdb_block_t * block)
{
    d_stat_slot_free (dbclass->imdb);

    obj_size_t      slen = d_block_lower_data_limit (block);
    imdb_slot_free_t *slot_free = d_pointer_add (imdb_slot_free_t, (block), slen);
    slot_free->flags = SLOT_FLAG_FREE;
    block->footer_offset = 0;
    slot_free->length = d_block_upper_data_blimit (dbclass->imdb, block) - d_size_bptr (slen);

    if (d_block_slot_has_footer (dbclass)) {
	imdb_slot_footer_t *slot_footer = d_block_slot_free_footer (slot_free);
	os_memset (slot_footer, 0, sizeof (imdb_slot_footer_t));
	slot_footer->flags = SLOT_FLAG_FREE;
	slot_footer->length = slot_free->length;
    }

    imdb_fl_insert_slot (block, slot_free);
}

LOCAL imdb_block_t *ICACHE_FLASH_ATTR imdb_page_block_alloc (imdb_class_t * dbclass, imdb_block_page_t * page_block);

/*
[private] Recycle next block for target block.
  - dbclass: 
  - page_block:
  - block: target block
*/
LOCAL imdb_block_t *ICACHE_FLASH_ATTR
imdb_page_block_recycle (imdb_class_t * dbclass, imdb_block_page_t * page_block, imdb_block_t * block)
{
    d_assert (dbclass->page_fl_first == NULL, "page_fl_first=%p", dbclass->page_fl_first);

    block_size_t    bsize = dbclass->imdb->db_def.block_size;
    imdb_block_t   *block_targ = NULL;
    imdb_block_page_t *page_targ = page_block;

    page_blocks_t   bidx;
    // try to recycle block in this page
    if (block->block_index < page_block->page.blocks) {
	bidx = block->block_index + 1;
	block_targ = d_page_block_byidx (page_targ, bidx, bsize);
    }
    else {
	// try to recycle next page
	page_targ = page_block->page.page_next ? page_block->page.page_next : dbclass->page_first;
	bidx = 1;
	block_targ = d_page_block_byidx (page_targ, bidx, bsize);
    }

#ifdef IMDB_BLOCK_CRC
    uint16          crc16 = crc8 (block_targ, bsize);
    d_assert (crc16 == block_targ->crc16, "crc=%u,%u", crc16, block_targ->crc16);
#else
    d_assert (block_targ->crc16 == 0xFFFF, "crc=%u", block_targ->crc16);
#endif

    d_log_dprintf (IMDB_SERVICE_NAME, "recycle: class page=%p block#%d=%p, size=%u)", page_targ, bidx, block_targ,
		   bsize);
    d_stat_block_recycle (dbclass->imdb);
    block->block_fl_next = 0;
    block->free_offset = 0;
    block->lock_flags = 0;

    imdb_block_slot_init (dbclass, page_targ, block_targ);
    imdb_fl_insert_block (dbclass, page_targ, block_targ);
    imdb_fl_insert_page (dbclass, page_targ);

#ifdef IMDB_BLOCK_CRC
    block_targ->crc16 = crc8 (block_targ, bsize);
#else
    block_targ->crc16 = 0xFFFF;
#endif

    return block_targ;
}

/*
[private] Extract DataSlot from target FreeSlot. Remove FreeSlot from FreeList when no enought space for splitting.
  - dbclass: 
  - find_ctx: FreeSlot search context
  - slot_bsize: search user data size in block units
  - extra_bsize: extra (header) size in block units need for split
*/
LOCAL void      ICACHE_FLASH_ATTR
imdb_slot_free_extract (imdb_class_t * dbclass,
			imdb_free_slot_find_ctx_t * find_ctx, obj_size_t slot_bsize, obj_size_t extra_bsize)
{
    imdb_slot_free_t *slot_free = find_ctx->slot_free;
    d_assert (slot_free->flags == SLOT_FLAG_FREE, "flags=%u", slot_free->flags);

    imdb_slot_free_t *slot_free_next = NULL;

    if (slot_free->length >= slot_bsize + extra_bsize) {
	d_stat_slot_split (dbclass->imdb);
	slot_free_next = d_pointer_add (imdb_slot_free_t, slot_free, d_bptr_size (slot_bsize));
	slot_free_next->flags = SLOT_FLAG_FREE;
	slot_free_next->length = slot_free->length - slot_bsize;

	d_log_dprintf (IMDB_SERVICE_NAME, "slot_extract: split free slot=%p, len=%u", slot_free_next,
		       slot_free_next->length);

	if (d_block_slot_has_footer (dbclass)) {
	    imdb_slot_footer_t *slot_footer = d_block_slot_free_footer (slot_free);
	    d_assert (slot_free->flags == SLOT_FLAG_FREE, "flags=%u", slot_free->flags);
	    slot_footer->length = slot_free_next->length;
	    slot_footer->skip_count = 0;	// reset skip count
	}

	// free slot have enougth space after emit data_slot
	if (slot_free_next->length >= dbclass->obj_bsize_min + extra_bsize) {
	    slot_free_next->next_offset = slot_free->next_offset;
	}
	else {
	    slot_free_next = d_block_next_slot_free (find_ctx->block, slot_free);
	    d_log_dprintf (IMDB_SERVICE_NAME, "slot_extract: next free slot=%p, len=%u", slot_free_next,
			   (slot_free_next) ? slot_free_next->length : 0);
	}
    }
    else {
	slot_free_next = d_block_next_slot_free (find_ctx->block, slot_free);
	d_log_dprintf (IMDB_SERVICE_NAME, "slot_extract: next free slot=%p, len=%u", slot_free_next,
		       (slot_free_next) ? slot_free_next->length : 0);
    }

    if (slot_free_next) {
	if (find_ctx->slot_free_prev)
	    find_ctx->slot_free_prev->next_offset = d_size_bptr (d_pointer_diff (slot_free_next, find_ctx->block));
	else
	    find_ctx->block->free_offset = d_size_bptr (d_pointer_diff (slot_free_next, find_ctx->block));
    }
    else {
	// delete block from Block Free List, try to allocate next block
	imdb_block_page_t *page_block = find_ctx->page_block;
	find_ctx->block->free_offset = 0;
	if (!find_ctx->block_fl_prev) {
	    if (!find_ctx->block->block_fl_next) {
		page_block->page.block_fl_first = 0;
		if (page_block->page.alloc_hwm < page_block->page.blocks) {
		    // allocate next block in this page
		    imdb_page_block_alloc (dbclass, page_block);
		}
	    }
	    else {
		page_block->page.block_fl_first = find_ctx->block->block_fl_next;
	    }
	}
	else {
	    find_ctx->block_fl_prev->block_fl_next = find_ctx->block->block_fl_next;
	}

	if (!page_block->page.block_fl_first) {
	    // delete page from Page Free List
	    if (find_ctx->page_block_fl_prev) {
		find_ctx->page_block_fl_prev->page.page_fl_next = page_block->page.page_fl_next;
	    }
	    else {
		dbclass->page_fl_first = page_block->page.page_fl_next;
		if (!dbclass->page_fl_first && dbclass->cdef.opt_recycle) {
		    // recycle next block
		    imdb_page_block_recycle (dbclass, find_ctx->page_block, find_ctx->block);
		}
	    }
	}
    }

    slot_free->flags = SLOT_FLAG_DATA;
}

LOCAL imdb_block_page_t *imdb_page_alloc (imdb_class_t * dbclass);

/*
[private]: Find FreeSlot in Class FreeList
  - dbclass: 
  - slot_bsize: search user data size in block units
  - find_ctx: FreeSlot search context
*/
LOCAL void      ICACHE_FLASH_ATTR
imdb_slot_free_find (imdb_class_t * dbclass, obj_size_t slot_bsize, imdb_free_slot_find_ctx_t * find_ctx)
{
    // find suitable free slot
    uint32          skipscan = 0;
    if (dbclass->page_fl_first) {
	block_size_t    bsize = dbclass->imdb->db_def.block_size;

	find_ctx->page_block = dbclass->page_fl_first;
	// find suitable page
	while (find_ctx->page_block) {
	    page_blocks_t   bidx = find_ctx->page_block->page.block_fl_first;
	    find_ctx->block_fl_prev = NULL;
	    // find suitable block
	    while (bidx) {
		find_ctx->block = d_page_block_byidx (find_ctx->page_block, bidx, bsize);
		find_ctx->slot_free = d_block_slot_free (find_ctx->block);
		find_ctx->slot_free_prev = NULL;
		// find suitable slot
		while (find_ctx->slot_free) {
		    d_assert (find_ctx->slot_free->flags == SLOT_FLAG_FREE, "flags=%u", find_ctx->slot_free->flags);
		    if (slot_bsize <= find_ctx->slot_free->length) {
			goto slot_found;
		    }
		    if (dbclass->ds_type == DATA_SLOT_TYPE_4) {
			imdb_slot_footer_t *slot_footer = d_block_slot_free_footer (find_ctx->slot_free);
			slot_footer->skip_count++;
		    }
		    skipscan++;

		    find_ctx->slot_free_prev = find_ctx->slot_free;
		    find_ctx->slot_free = d_block_next_slot_free (find_ctx->block, find_ctx->slot_free);
		}
		find_ctx->block_fl_prev = find_ctx->block;
		bidx = find_ctx->block->block_fl_next;
	    }
	    find_ctx->slot_free_prev = NULL;

	    // allocate new block in page
	    if (find_ctx->page_block->page.alloc_hwm < find_ctx->page_block->page.blocks) {
		find_ctx->block = imdb_page_block_alloc (dbclass, find_ctx->page_block);
		find_ctx->slot_free = d_block_slot_free (find_ctx->block);
		goto slot_found;
	    }

	    find_ctx->page_block_fl_prev = find_ctx->page_block;
	    find_ctx->page_block = find_ctx->page_block->page.page_fl_next;
	}
	find_ctx->page_block_fl_prev = NULL;
    }

    if (!find_ctx->page_block) {
	if (dbclass->page_count < dbclass->cdef.pages_max) {
	    // allocate new page
	    find_ctx->page_block = imdb_page_alloc (dbclass);
	    find_ctx->block = &(find_ctx->page_block->block);
	    find_ctx->slot_free = d_block_slot_free (find_ctx->block);
	}
	else {
	    return;
	}
    }

  slot_found:
    d_assert (slot_bsize <= find_ctx->slot_free->length, "size=%u, len=%u", slot_bsize, find_ctx->slot_free->length);
    d_stat_slot_data (dbclass->imdb, skipscan);
    return;
}

/*
[private]: Get FreeSlot or recycle block
  - dbclass: 
  - slot_bsize: search user data size in block units
  - find_ctx: FreeSlot search context
*/
LOCAL void      ICACHE_FLASH_ATTR
imdb_slot_free_get_or_recycle (imdb_class_t * dbclass, obj_size_t slot_bsize, imdb_free_slot_find_ctx_t * find_ctx)
{
    d_assert (dbclass->page_fl_first, "page_fl_first=%p", dbclass->page_fl_first);
    find_ctx->page_block = dbclass->page_fl_first;

    page_blocks_t   bidx = find_ctx->page_block->page.block_fl_first;
    d_assert (bidx > 0, "bidx=%u", bidx);
    block_size_t    bsize = dbclass->imdb->db_def.block_size;
    find_ctx->block = d_page_block_byidx (find_ctx->page_block, bidx, bsize);

    find_ctx->slot_free = d_block_slot_free (find_ctx->block);
    d_assert (find_ctx->slot_free, "slot=%p", find_ctx->slot_free);
    d_assert (find_ctx->slot_free->flags == SLOT_FLAG_FREE, "flags=%u", find_ctx->slot_free->flags);
    if (slot_bsize <= find_ctx->slot_free->length) {
	return;
    }

    find_ctx->block->free_offset = 0;
    find_ctx->page_block->page.block_fl_first = 0;

    if (dbclass->page_count < dbclass->cdef.pages_max) {
	// allocate new page
	dbclass->page_fl_first = imdb_page_alloc (dbclass);
	find_ctx->page_block = dbclass->page_fl_first;
	find_ctx->block = d_page_block_byidx (find_ctx->page_block, find_ctx->page_block->page.block_fl_first, bsize);
    }
    else {
	if (find_ctx->page_block->page.alloc_hwm < find_ctx->page_block->page.blocks) {
	    // allocate next block in this page
	    d_assert (find_ctx->page_block->page.alloc_hwm == find_ctx->block->block_index, "hwm=%u, bidx=%u",
		      find_ctx->page_block->page.alloc_hwm, find_ctx->block->block_index);
	    find_ctx->block = imdb_page_block_alloc (dbclass, find_ctx->page_block);
	}
	else {
	    // recycle next block;
	    dbclass->page_fl_first = NULL;
	    find_ctx->block = imdb_page_block_recycle (dbclass, find_ctx->page_block, find_ctx->block);
	}
	find_ctx->page_block = dbclass->page_fl_first;
    }
    find_ctx->slot_free = d_block_slot_free (find_ctx->block);
}

/*
[inline] Initialize new Page
  - dbclass:
  - page_block:
  - btype: block type
*/
INLINED void    ICACHE_FLASH_ATTR
imdb_page_init (imdb_class_t * dbclass, imdb_block_page_t * page_block, imdb_block_type_t btype)
{
    imdb_block_t   *block = &(page_block->block);
    imdb_page_t    *page = &page_block->page;

    block->btype = btype;
    page->page_index = dbclass->page_count;
    page->class_block = (imdb_block_class_t *) (dbclass->page_first);
    page->alloc_hwm = 1;
    page->blocks = (btype == BLOCK_TYPE_PAGE) ? dbclass->cdef.page_blocks : dbclass->cdef.init_blocks;

    block->block_index = 1;

    imdb_block_slot_init (dbclass, page_block, block);
    imdb_fl_insert_block (dbclass, page_block, block);

#ifdef IMDB_BLOCK_CRC
    block->crc16 = crc8 (block, bsize);
#else
    block->crc16 = 0xFFFF;
#endif
}

/*
[private]: Allocate new Block in Page
  - dbclass: 
  - page_block: target page block
  - result: allocated block
*/
LOCAL imdb_block_t *ICACHE_FLASH_ATTR
imdb_page_block_alloc (imdb_class_t * dbclass, imdb_block_page_t * page_block)
{
    d_assert (page_block->block.btype == BLOCK_TYPE_PAGE
	      || page_block->block.btype == BLOCK_TYPE_CLASS, "btype=%u", page_block->block.btype);

    imdb_page_t    *page = &page_block->page;
    d_assert (dbclass == &(page->class_block->dbclass), "block=%p", page->class_block);
    d_assert (page->alloc_hwm < page->blocks, "hwm=%u, blocks=%u", page->alloc_hwm, page->blocks);

#ifdef IMDB_BLOCK_CRC
    uint16          crc16 = crc8 (page_block, bsize);
    d_assert (crc16 == page_block->block.crc16, "crc=%u,%u", crc16, page_block->block.crc16);
#else
    d_assert (page_block->block.crc16 == 0xFFFF, "crc=%u", page_block->block.crc16);
#endif

    block_size_t    bsize = dbclass->imdb->db_def.block_size;
    page_blocks_t   bidx = ++page->alloc_hwm;
    d_stat_block_init (dbclass->imdb);
    imdb_block_t   *block = d_page_block_byidx (page_block, bidx, bsize);
    d_log_dprintf (IMDB_SERVICE_NAME, "block_alloc: format page=%p block#%d=%p, size=%u", page_block, bidx, block,
		   bsize);

#ifdef IMDB_ZERO_MEM
    os_memset (block, 0, bsize);
#else
    os_memset (block, 0, sizeof (imdb_block_t));
#endif
    block->block_index = bidx;

    imdb_block_slot_init (dbclass, page_block, block);
    imdb_fl_insert_block (dbclass, page_block, block);

#ifdef IMDB_BLOCK_CRC
    block->crc16 = crc8 (block, bsize);
#else
    block->crc16 = 0xFFFF;
#endif

    return block;
}

/*
[private] Allocate initial Page for Class storage.
  - imdb: imdb pointer
  - cdef: Class definition
  - result: Page block pointer
*/
LOCAL imdb_block_class_t *ICACHE_FLASH_ATTR
imdb_class_page_alloc (imdb_t * imdb, imdb_class_def_t * cdef)
{
    size_t          psize = cdef->init_blocks * imdb->db_def.block_size;

    imdb_block_class_t *class_block = (imdb_block_class_t *) os_malloc (psize);
    imdb_class_t   *dbclass = &class_block->dbclass;
    d_log_dprintf (IMDB_SERVICE_NAME, "page_alloc: class_page=%p, size=%u", class_block, psize);
    d_stat_alloc (imdb, psize);
    d_stat_block_alloc (imdb, cdef->init_blocks);
    d_stat_page_alloc (imdb);
    d_stat_block_init (imdb);

#ifdef IMDB_ZERO_MEM
    os_memset (class_block, 0, imdb->db_def.block_size);
#else
    os_memset (class_block, 0, sizeof (imdb_block_class_t));
#endif
    dbclass->lock_flags = DATA_LOCK_EXCLUSIVE;
    imdb_block_page_t *page_block = d_pointer_as (imdb_block_page_t, class_block);

    dbclass->page_count = 1;
    dbclass->page_first = page_block;
    dbclass->page_last = page_block;
    dbclass->imdb = imdb;
    if (!cdef->opt_variable) {
	dbclass->ds_type = (cdef->opt_tx_control || !cdef->opt_recycle) ? DATA_SLOT_TYPE_2 : DATA_SLOT_TYPE_1;
    }
    else {
	dbclass->ds_type = (cdef->opt_tx_control || !cdef->opt_recycle) ? DATA_SLOT_TYPE_4 : DATA_SLOT_TYPE_3;
    }
    os_memcpy (&dbclass->cdef, cdef, sizeof (imdb_class_def_t));

    imdb_page_init (dbclass, page_block, BLOCK_TYPE_CLASS);
    imdb_fl_insert_page (dbclass, page_block);

    dbclass->lock_flags = DATA_LOCK_NONE;
    return class_block;
}

/*
[private] Allocate next (not initial) page for Class storage.
  - dbclass: Class pointer
  - result: Page block pointer
*/
LOCAL imdb_block_page_t *ICACHE_FLASH_ATTR
imdb_page_alloc (imdb_class_t * dbclass)
{
    size_t          psize = dbclass->cdef.page_blocks * dbclass->imdb->db_def.block_size;
    imdb_block_page_t *page_block = (imdb_block_page_t *) os_malloc (psize);
    d_log_dprintf (IMDB_SERVICE_NAME, "page_alloc: class=%p alloc page=%p, size=%u", dbclass->page_first, page_block,
		   psize);
    d_stat_alloc (dbclass->imdb, psize);
    d_stat_block_alloc (dbclass->imdb, dbclass->cdef.page_blocks);
    d_stat_page_alloc (dbclass->imdb);

#ifdef IMDB_ZERO_MEM
    os_memset (page_block, 0, dbclass->imdb->db_def.block_size);
#else
    os_memset (page_block, 0, sizeof (imdb_block_page_t));
#endif

    imdb_page_init (dbclass, page_block, BLOCK_TYPE_PAGE);
    imdb_fl_insert_page (dbclass, page_block);

    imdb_page_t    *page = &page_block->page;
    dbclass->page_count++;
    dbclass->page_last->page.page_next = page_block;
    page->page_prev = dbclass->page_last;
    dbclass->page_last = page_block;

    return page_block;
}

/*
[private] insert object into storage.
  - class_block: Class block pointer
  - ptr: result pointer to object
  - length: object size (0 for fixed objects)
  - result: imdb error code
*/
LOCAL imdb_errcode_t ICACHE_FLASH_ATTR
imdb_class_instance_alloc (imdb_block_class_t * class_block, void **ptr, size_t length)
{
    imdb_class_t   *dbclass = &class_block->dbclass;
    imdb_class_def_t *cdef = &(dbclass->cdef);

    obj_size_t      extra_bsize = data_slot_type_bsize[dbclass->ds_type];
    obj_size_t      slot_bsize;
    if (cdef->opt_variable) {
	slot_bsize = d_size_bptr_align (length);
	if (slot_bsize > dbclass->imdb->obj_bsize_max) {
	    return IMDB_INVALID_OBJSIZE;
	}
    }
    else {
	slot_bsize = dbclass->obj_bsize_min;
    }
    slot_bsize += extra_bsize;

    d_log_dprintf (IMDB_SERVICE_NAME, "alloc: class=%p searching free slot f_len=%u, e_len=%u", class_block, slot_bsize,
		   extra_bsize);

    imdb_free_slot_find_ctx_t find_ctx;
    os_memset (&find_ctx, 0, sizeof (imdb_free_slot_find_ctx_t));

    if (dbclass->cdef.opt_recycle) {
	imdb_slot_free_get_or_recycle (dbclass, slot_bsize, &find_ctx);
    }
    else {
	imdb_slot_free_find (dbclass, slot_bsize, &find_ctx);
	if (!find_ctx.slot_free) {
	    return IMDB_ALLOC_PAGES_MAX;
	}
    }
    d_log_dprintf (IMDB_SERVICE_NAME, "alloc: free slot found - page=%p, block=%p, slot=%p, len=%u",
		   find_ctx.page_block, find_ctx.block, find_ctx.slot_free, find_ctx.slot_free->length);

    imdb_slot_free_extract (dbclass, &find_ctx, slot_bsize, extra_bsize);
    d_log_dprintf (IMDB_SERVICE_NAME, "alloc: data slot=%p, rid=%u:%u:%u", find_ctx.slot_free, 0,
		   find_ctx.block->block_index, d_pointer_diff (find_ctx.slot_free, find_ctx.block));

    switch (dbclass->ds_type) {
    case DATA_SLOT_TYPE_1:
	*ptr = find_ctx.slot_free;
	break;
    case DATA_SLOT_TYPE_2:
	*ptr = imdb_data2_slot_init (find_ctx.block, find_ctx.slot_free);
	break;
    case DATA_SLOT_TYPE_3:
    case DATA_SLOT_TYPE_4:
	*ptr = imdb_data4_slot_init (find_ctx.block, find_ctx.slot_free, slot_bsize);
	break;
    default:
	d_assert (false, "ds_type=%u", dbclass->ds_type);
    }

    return IMDB_ERR_SUCCESS;
}

/*
[public] Initialize imdb instance
  - hndlr: result handler to imdb instance
  - hcurmdb: handler
  - result: imdb error code
*/
imdb_errcode_t  ICACHE_FLASH_ATTR
imdb_init (imdb_def_t * imdb_def, imdb_hndlr_t hcurmdb, imdb_hndlr_t * himdb)
{
    imdb_t         *imdb;
    st_zalloc (imdb, imdb_t);
    d_stat_alloc (imdb, sizeof (imdb_t));

    if (!imdb_def->block_size) {
	imdb_def->block_size = IMDB_BLOCK_SIZE_DEFAULT;
    }
    else {
	imdb_def->block_size = MAX (IMDB_BLOCK_SIZE_MIN, d_size_align (imdb_def->block_size));
    }
    os_memcpy (&imdb->db_def, imdb_def, sizeof (imdb_def_t));
    imdb->obj_bsize_max = d_size_bptr (imdb->db_def.block_size - block_header_size[BLOCK_TYPE_CLASS]);

    d_log_dprintf (IMDB_SERVICE_NAME, "init: instance init %p (bsz=%u)", imdb, imdb->db_def.block_size);
    d_log_dprintf (IMDB_SERVICE_NAME,
		   "init: structures size:\n\timdb\t: %lu,\n\tclass\t: %lu,\n\tpage\t: %lu,\n\tblock\t: %lu,\n\tslot\t: %lu:%lu",
		   sizeof (imdb_t), sizeof (imdb_class_t), sizeof (imdb_page_t), sizeof (imdb_block_t),
		   sizeof (imdb_slot_free_t), sizeof (imdb_slot_footer_t));

    *himdb = d_obj2hndlr (imdb);

    if (hcurmdb) {
        imdb_t         *curimdb = d_hndlr2obj (imdb_t, hcurmdb);
        imdb->hcurs = curimdb->hcurs;
    }
    else {
        imdb_class_def_t cdef =
	    { IMDB_CLS_CURSOR, false, false, false, 0, 0, 1, IMDB_CURSOR_PAGE_BLOCKS, sizeof (imdb_cursor_t) };
        imdb_class_create (*himdb, &cdef, &imdb->hcurs);
    }

    if (imdb_def->opt_media) {
        // read header
        imdb_file_t hdr_file;
        fio_user_read(0, (uint32 *) &hdr_file, sizeof(imdb_file_t));
        uint16 crc = hdr_file.crc16;
        hdr_file.crc16 = 0;
        if (crc16(&hdr_file, sizeof(imdb_file_t)) != crc) {
            hdr_file.version = IMDB_FILE_HEADER_VERSION;
            hdr_file.block_size = imdb_def->block_size;
            hdr_file.class_last.fptr = 0;
            hdr_file.file_size = MIN(imdb_def->file_size, fio_user_size ()/imdb_def->block_size);
            hdr_file.file_hwm = 0;
            hdr_file.scn = 1;
            hdr_file.crc16 = crc16(&hdr_file, sizeof(imdb_file_t));
            d_log_wprintf (IMDB_SERVICE_NAME, "file header crc error, new: %ublk", hdr_file.file_size);
            fio_user_write(0, (uint32 *) &hdr_file, sizeof(imdb_file_t));
        }
        else {
            d_log_wprintf (IMDB_SERVICE_NAME, "read data file [SCN:%u,size:%ublk]", hdr_file.scn, hdr_file.file_size);
            imdb->class_first.fptr = (hdr_file.file_hwm) ? 1 : 0;
            imdb->class_last.fptr = hdr_file.class_last.fptr;
        }
    }

    return IMDB_ERR_SUCCESS;
}

/*
[public] Destroy imdb instance and all storages
  - hndlr: handler to imdb instance
  - result: imdb error code
  */
imdb_errcode_t  ICACHE_FLASH_ATTR
imdb_done (imdb_hndlr_t hmdb)
{
    d_imdb_check_hndlr (hmdb);

    imdb_t         *imdb = d_hndlr2obj (imdb_t, hmdb);

    imdb_block_class_t *class_block = imdb->class_first.mptr;
    while (class_block) {
	imdb_hndlr_t    hclass = d_obj2hndlr (class_block);
	class_block = class_block->dbclass.class_next;
	imdb_class_destroy (hclass);
    }

    d_stat_free (imdb, sizeof (imdb_t));

    d_assert (imdb->stat.mem_alloc == imdb->stat.mem_free, "alloc=%u, free=%u", imdb->stat.mem_alloc,
	      imdb->stat.mem_free);
    d_log_dprintf (IMDB_SERVICE_NAME, "done: instance done %p (mem=%lu)", imdb,
		   imdb->stat.mem_alloc - imdb->stat.mem_free);

    os_free (imdb);
    return IMDB_ERR_SUCCESS;
}

/*
[public] Query imdb instance information (statistics, etc)
  - hndlr: handler to imdb instance
  - imdb_info: result pointer to class_info structure
  - result: imdb error code
*/
imdb_errcode_t  ICACHE_FLASH_ATTR
imdb_info (imdb_hndlr_t hmdb, imdb_info_t * imdb_info, imdb_class_info_t info_array[], uint8 array_len)
{
    d_imdb_check_hndlr (hmdb);

    imdb_t         *imdb = d_hndlr2obj (imdb_t, hmdb);
    os_memcpy (&imdb_info->stat, &imdb->stat, sizeof (imdb_stat_t));
    os_memcpy (&imdb_info->db_def, &imdb->db_def, sizeof (imdb_def_t));

    imdb_info->size_class = sizeof (imdb_block_class_t);
    imdb_info->size_page = sizeof (imdb_block_page_t);
    imdb_info->size_block = sizeof (imdb_block_t);
    imdb_info->size_rowid = sizeof (imdb_rowid_t);
    imdb_info->size_cursor = sizeof (imdb_cursor_t);

    imdb_info->class_count = 0;
    imdb_block_class_t *class_block = imdb->class_first.mptr;
    while (class_block) {
	imdb_info->class_count++;
	if (array_len >= imdb_info->class_count) {
	    imdb_class_info (d_obj2hndlr (class_block), &info_array[imdb_info->class_count - 1]);
	}
	class_block = class_block->dbclass.class_next;
    }

    return IMDB_ERR_SUCCESS;
}


/*
[public] Create class storage for object.
  - hmdb: Handler to imdb instance
  - cdef: Class definition
  - hclass: Result Class instance handler
  - result: imdb error code
*/
imdb_errcode_t  ICACHE_FLASH_ATTR
imdb_class_create (imdb_hndlr_t hmdb, imdb_class_def_t * cdef, imdb_hndlr_t * hclass)
{
    d_imdb_check_hndlr (hmdb);
    imdb_t         *imdb = d_hndlr2obj (imdb_t, hmdb);

    if (imdb->db_def.opt_media)
        return IMDB_INTERNAL_ERROR;

    cdef->page_blocks = MAX (IMDB_FIRST_PAGE_BLOCKS_MIN, cdef->page_blocks);
    cdef->obj_size = d_size_align (cdef->obj_size);
    obj_size_t      obj_bsize_min = d_size_bptr (cdef->obj_size);
    if ((obj_bsize_min > imdb->obj_bsize_max) || (obj_bsize_min == 0 && !cdef->opt_variable)) {
	return IMDB_INVALID_OBJSIZE;
    }

    if (!cdef->init_blocks) {
	cdef->init_blocks = cdef->page_blocks >> IMDB_FIRST_PAGE_BLOCKS_DIV;
    }
    cdef->init_blocks = MAX (IMDB_FIRST_PAGE_BLOCKS_MIN, cdef->init_blocks);
    if (cdef->opt_recycle && cdef->init_blocks <= 2) {
	return IMDB_INVALID_RECYCLE_STORAGE;
    }


    if (cdef->pct_free > IMDB_PCT_FREE_MAX) {
	cdef->pct_free = IMDB_PCT_FREE_MAX;
    }

    imdb_block_class_t *class_block = imdb_class_page_alloc (imdb, cdef);
    imdb_class_t   *dbclass = &class_block->dbclass;
    dbclass->obj_bsize_min = obj_bsize_min;
    if (imdb->class_last.mptr) {
	dbclass->class_prev = imdb->class_last.mptr;
	imdb->class_last.mptr->dbclass.class_next = class_block;
	imdb->class_last.mptr = class_block;
    }
    else {
	imdb->class_first.mptr = class_block;
	imdb->class_last.mptr = class_block;
    }

    *hclass = d_obj2hndlr (class_block);
    d_log_iprintf (IMDB_SERVICE_NAME, "created \"%s\" %08x (type=%u,page_blks=%u,obj_sz=%u)", cdef->name, *hclass,
		   dbclass->ds_type, cdef->page_blocks, cdef->obj_size);

    return IMDB_ERR_SUCCESS;
}


/*
[public] Destroy class storage for object and deallocate memory.
  - hcls: handler to class instance
  - result: imdb error code
*/
imdb_errcode_t  ICACHE_FLASH_ATTR
imdb_class_destroy (imdb_hndlr_t hclass)
{
    d_imdb_check_hndlr (hclass);

    imdb_block_class_t *class_block = d_hndlr2obj (imdb_block_class_t, hclass);
    imdb_class_t   *dbclass = &class_block->dbclass;
    imdb_t         *imdb = dbclass->imdb;

    if (dbclass->lock_flags != DATA_LOCK_NONE) {
	return IMDB_INVALID_OPERATION;
    }

    dbclass->lock_flags = DATA_LOCK_EXCLUSIVE;

    if (dbclass->class_prev) {
	dbclass->class_prev->dbclass.class_next = dbclass->class_next;
    }
    else {
	dbclass->imdb->class_first.mptr = class_block;
    }
    if (dbclass->class_next) {
	dbclass->class_next->dbclass.class_prev = dbclass->class_prev;
    }

    class_name_t    cname;
    os_memcpy (cname, dbclass->cdef.name, sizeof (class_name_t));
    // iterate page
    imdb_block_page_t *page_targ = dbclass->page_first;
    class_pages_t   pcnt = 0;
    uint32          bcnt = 0;
    while (page_targ) {
	void           *ptr = page_targ;
	d_stat_page_free (imdb);
	d_stat_free (imdb, page_targ->page.blocks * imdb->db_def.block_size);
	pcnt++;
	bcnt += page_targ->page.blocks;
	page_targ = page_targ->page.page_next;
	os_free (ptr);
    }
    d_log_iprintf (IMDB_SERVICE_NAME, "destroyed \"%s\" %08x (page_cnt=%u,blk_cnt=%u)", cname, hclass, pcnt, bcnt);

    return IMDB_ERR_SUCCESS;
}

/*
[public] insert object into storage.
  - hclass: handler to class instance
  - ptr: result pointer to object
  - length: object size (0 for fixed objects)
  - result: imdb error code
 */
imdb_errcode_t  ICACHE_FLASH_ATTR
imdb_clsobj_insert (imdb_hndlr_t hclass, void **ptr, size_t length)
{
    imdb_block_class_t *dbclass = d_hndlr2obj (imdb_block_class_t, hclass);
    return imdb_class_instance_alloc (dbclass, ptr, length);
}

/*
[public] delete object from storage.
  - hclass: handler to class instance
  - ptr: pointer to deleting object
  - result: imdb error code
 */
imdb_errcode_t  ICACHE_FLASH_ATTR
imdb_clsobj_delete (imdb_hndlr_t hclass, void *ptr)
{
    d_imdb_check_hndlr (hclass);
    imdb_block_class_t *class_block = d_hndlr2obj (imdb_block_class_t, hclass);
    imdb_class_t   *dbclass = &class_block->dbclass;

    imdb_slot_free_t *slot_free = NULL;
    imdb_block_t   *block = NULL;

    imdb_slot_data2_t *slot_data2;
    imdb_slot_data4_t *slot_data4;
    imdb_slot_footer_t *slot_footer = NULL;

    obj_size_t      slen = 0;
    switch (dbclass->ds_type) {
    case DATA_SLOT_TYPE_1:
    case DATA_SLOT_TYPE_3:
	return IMDB_INVALID_OPERATION;
	break;
    case DATA_SLOT_TYPE_2:
	slot_data2 = d_pointer_add (imdb_slot_data2_t, ptr, -sizeof (imdb_slot_data2_t));
	d_assert (slot_data2->flags == SLOT_FLAG_DATA, "flags=%u", slot_data2->flags);
	block = d_pointer_add (imdb_block_t, slot_data2, -d_bptr_size (slot_data2->block_offset));
	slen = dbclass->cdef.obj_size + sizeof (imdb_slot_data2_t);
	slot_free = (imdb_slot_free_t *) slot_data2;
	break;
    case DATA_SLOT_TYPE_4:
	slot_data4 = d_pointer_add (imdb_slot_data4_t, ptr, -sizeof (imdb_slot_data4_t));
	d_assert (slot_data4->flags == SLOT_FLAG_DATA, "flags=%u", slot_data4->flags);
	slen = d_bptr_size (slot_data4->length);
	slot_footer = d_pointer_add (imdb_slot_footer_t, slot_data4, slen - sizeof (imdb_slot_footer_t));
	d_assert (slot_footer->flags == SLOT_FLAG_DATA, "flags=%u", slot_footer->flags);

	block = d_pointer_add (imdb_block_t, slot_data4, -d_bptr_size (slot_data4->block_offset));
	slot_free = (imdb_slot_free_t *) slot_data4;
	break;
    default:
	d_assert (false, "ds_type=%u", dbclass->ds_type);
    }

    d_stat_slot_free (dbclass->imdb);
    slot_free->flags = SLOT_FLAG_FREE;
    slot_free->length = d_size_bptr (slen);

    d_log_dprintf (IMDB_SERVICE_NAME, "delete: class=%p add free slot=%p, len=%u", class_block, slot_free,
		   slot_free->length);

    if (imdb_fl_insert_slot (block, slot_free)) {
	imdb_block_page_t *page_block = d_block_page (block, dbclass->imdb->db_def.block_size);
	imdb_fl_insert_block (dbclass, page_block, block) && imdb_fl_insert_page (dbclass, page_block);
    }

    if (slot_footer) {
	os_memset (slot_footer, 0, sizeof (imdb_slot_footer_t));
	slot_footer->flags = SLOT_FLAG_FREE;
	slot_footer->length = slot_free->length;
    }

    // !!! TODO: Coalesce !!!

    return IMDB_ERR_SUCCESS;
}

/**
[public] change size of existing variable length object into storage.
  - hclass: handler to class instance
  - ptr_old: pointer to existing object
  - ptr: result pointer to new object
  - length: object size
  - result: imdb error code
*/
imdb_errcode_t  ICACHE_FLASH_ATTR
imdb_clsobj_resize (imdb_hndlr_t hclass, void *ptr_old, void **ptr, size_t length)
{
    d_imdb_check_hndlr (hclass);
    imdb_block_class_t *class_block = d_hndlr2obj (imdb_block_class_t, hclass);
    imdb_class_t   *dbclass = &class_block->dbclass;

    if (dbclass->ds_type != DATA_SLOT_TYPE_4) {
	return IMDB_INVALID_OPERATION;
    }

    // TODO: Not finished
    return IMDB_ERR_SUCCESS;
}

/*
[public] Return object length.
  - hclass: handler to class instance
  - ptr: pointer to deleting object
  - result: imdb error code
*/
imdb_errcode_t  ICACHE_FLASH_ATTR
imdb_clsobj_length (imdb_hndlr_t hclass, void *ptr, size_t * length)
{
    d_imdb_check_hndlr (hclass);
    imdb_block_class_t *class_block = d_hndlr2obj (imdb_block_class_t, hclass);
    if (class_block->dbclass.cdef.opt_variable) {
	imdb_slot_data4_t *data_slot4 = d_pointer_add (imdb_slot_data4_t, ptr, -sizeof (imdb_slot_data4_t));
	d_assert (data_slot4->flags == SLOT_FLAG_DATA, "flags=%u", data_slot4->flags);
	*length = d_bptr_size (data_slot4->length);
    }
    else {
	*length = class_block->dbclass.cdef.obj_size;
    }
    return IMDB_ERR_SUCCESS;
}

/**
[public] Return Information about class storage.
  - hclass: handler to class instance
  - class_info: result pointer to class_info structure
  - result: imdb error code
*/
imdb_errcode_t  ICACHE_FLASH_ATTR
imdb_class_info (imdb_hndlr_t hclass, imdb_class_info_t * class_info)
{
    d_imdb_check_hndlr (hclass);
    imdb_block_class_t *class_block = d_hndlr2obj (imdb_block_class_t, hclass);
    imdb_class_t   *dbclass = &class_block->dbclass;
    imdb_t         *imdb = dbclass->imdb;

    os_memset (class_info, 0, sizeof (imdb_class_info_t));
    class_info->hclass = hclass;
    os_memcpy (&class_info->cdef, &(dbclass->cdef), sizeof (imdb_class_def_t));

    imdb_block_page_t *page_targ = NULL;
    imdb_block_t   *block_targ = NULL;
    imdb_slot_free_t *slot_free = NULL;
    block_size_t    bsize = imdb->db_def.block_size;
    if (dbclass->page_fl_first) {
	page_targ = dbclass->page_fl_first;
	// iterate page
	while (page_targ) {
	    page_blocks_t   bidx = page_targ->page.block_fl_first;

	    class_info->blocks_free +=
		(d_pointer_equal (page_targ, class_block) ? dbclass->cdef.init_blocks : dbclass->cdef.page_blocks)
		- page_targ->page.alloc_hwm;
	    // iterate block
	    while (bidx) {
		block_targ = d_page_block_byidx (page_targ, bidx, bsize);
		slot_free = d_block_slot_free (block_targ);

		// iterate slot
		while (slot_free) {
		    class_info->slots_free++;
		    class_info->slots_free_size += d_bptr_size (slot_free->length);
		    if (dbclass->ds_type >= DATA_SLOT_TYPE_3) {
			imdb_slot_footer_t *slot_footer = d_block_slot_free_footer (slot_free);
			class_info->fl_skip_count += slot_footer->skip_count;
		    }
		    slot_free = d_block_next_slot_free (block_targ, slot_free);
		}
		bidx = block_targ->block_fl_next;
	    }
	    page_targ = page_targ->page.page_fl_next;
	}
    }


    // iterate page
    page_targ = dbclass->page_first;
    while (page_targ) {
	class_info->pages++;
	class_info->blocks += (class_info->pages == 1 ? dbclass->cdef.init_blocks : dbclass->cdef.page_blocks);
	page_targ = page_targ->page.page_next;
    }
    d_assert (dbclass->page_count == class_info->pages, "pages=%u,%u", dbclass->page_count, class_info->pages);

    return IMDB_ERR_SUCCESS;
}

/*
[private] Prepare cursor for specified access-path.
  - dbclass: pointer to class instance
  - cur: pointer to cursor
  - access_path: access path (FULL_SCAN, RECYCLE_SCAN, RECYCLE_SCAN_REW, etc.)
  - result: imdb error code
*/
imdb_errcode_t  ICACHE_FLASH_ATTR
imdb_class_cur_open (imdb_class_t * dbclass, imdb_cursor_t * cur, imdb_access_path_t access_path)
{
    os_memset (cur, 0, sizeof (imdb_cursor_t));
    cur->dbclass = dbclass;
    cur->otime = system_get_time ();
    cur->access_path = access_path;

    imdb_block_t   *block = NULL;
    switch (access_path) {
    case PATH_FULL_SCAN:
	cur->page_last = cur->dbclass->page_first;
	cur->rowid_last.page_id = cur->page_last->page.page_index;
	cur->rowid_last.block_id = 1;
	break;
    case PATH_RECYCLE_SCAN:
	// TODO: Not Finished
	return IMDB_CURSOR_INVALID_PATH;
    case PATH_RECYCLE_SCAN_REW:
	cur->page_last = cur->dbclass->page_fl_first;
	d_assert (cur->page_last, "page_last=%p", cur->page_last);
	cur->rowid_last.page_id = cur->page_last->page.page_index;
	cur->rowid_last.block_id = cur->page_last->page.block_fl_first;
	block = d_page_block_byidx (cur->page_last, cur->rowid_last.block_id, dbclass->imdb->db_def.block_size);
	cur->rowid_last.slot_offset = block->free_offset;
	break;
    default:
	return IMDB_CURSOR_INVALID_PATH;
    }

    return IMDB_ERR_SUCCESS;
}

/*
[private] Fetch next record from cursor.
  - dbclass: pointer to class instance
  - cur: pointer to cursor
  - ptr: result pointer to record
  - result: imdb error code
*/
imdb_errcode_t  ICACHE_FLASH_ATTR
imdb_class_cur_fetch (imdb_class_t * dbclass, imdb_cursor_t * cur, uint16 count, uint16 * rowcount, void *ptr[])
{
    if (!cur->page_last) {
	return IMDB_CURSOR_NO_DATA_FOUND;
    }

    imdb_block_t   *block = NULL;
    block_size_t    offset_limit;

    block_size_t    offset = cur->rowid_last.slot_offset;
    //bool last_block = false;
    switch (cur->access_path) {
    case PATH_FULL_SCAN:
	while (true) {
	    while (cur->rowid_last.block_id <= cur->page_last->page.alloc_hwm) {
		block = d_page_block_byidx (cur->page_last, cur->rowid_last.block_id, dbclass->imdb->db_def.block_size);
		if (offset == 0) {
		    offset = d_block_lower_data_blimit (block);
		}
		offset_limit = d_block_upper_data_blimit (dbclass->imdb, block);
		while (offset < offset_limit) {
		    *ptr = NULL;
		    while (!(*ptr) && offset < offset_limit) {
			imdb_block_slot_next (dbclass, block, &offset, ptr);
		    }
		    if (!(*ptr)) {
			break;
		    }

		    cur->rowid_last.slot_offset = offset;
		    if (!cur->rowid_first.page_id) {
			os_memcpy (&cur->rowid_first, &cur->rowid_last, sizeof (imdb_rowid_t));
		    }
		    ptr++;
		    (*rowcount)++;

		    if (*rowcount == count) {
			return IMDB_ERR_SUCCESS;
		    }
		}
		cur->rowid_last.block_id++;
		cur->rowid_last.slot_offset = offset = 0;
	    }
	    cur->page_last = cur->page_last->page.page_next;
	    if (!cur->page_last) {
		return IMDB_CURSOR_NO_DATA_FOUND;
		//cur->page_last = dblass->page_last; 
	    }
	    cur->rowid_last.page_id = cur->page_last->page.page_index;
	    cur->rowid_last.block_id = 1;
	    cur->rowid_last.slot_offset = offset = 0;
	}
	break;
    case PATH_RECYCLE_SCAN:
	return IMDB_CURSOR_INVALID_PATH;
	break;
    case PATH_RECYCLE_SCAN_REW:
	while (true) {
	    while (cur->rowid_last.block_id > 0) {
		block = d_page_block_byidx (cur->page_last, cur->rowid_last.block_id, dbclass->imdb->db_def.block_size);
		if (offset == 0) {
		    if (block->free_offset) {
			// because recycle whole block
			return IMDB_CURSOR_NO_DATA_FOUND;
		    }
		    imdb_block_slot_free_last (dbclass, block, &offset);
		}
		offset_limit = d_block_lower_data_blimit (block);
		while (offset > offset_limit) {
		    imdb_block_slot_prev (dbclass, block, &offset, ptr);
		    cur->rowid_last.slot_offset = offset;
		    if (!cur->rowid_first.page_id) {
			os_memcpy (&cur->rowid_first, &cur->rowid_last, sizeof (imdb_rowid_t));
		    }
		    ptr++;
		    (*rowcount)++;

		    if (*rowcount == count) {
			return IMDB_ERR_SUCCESS;
		    }
		}
		cur->rowid_last.block_id--;
		cur->rowid_last.slot_offset = offset = 0;
		//if (last_block) { return IMDB_CURSOR_NO_DATA_FOUND; }
	    }
	    cur->page_last = cur->page_last->page.page_prev;
	    if (!cur->page_last) {
		cur->page_last = dbclass->page_last;
	    }
	    cur->rowid_last.page_id = cur->page_last->page.page_index;
	    cur->rowid_last.block_id = cur->page_last->page.alloc_hwm;
	    cur->rowid_last.slot_offset = offset = 0;
	}
	break;
    default:
	return IMDB_CURSOR_INVALID_PATH;
    }

    return IMDB_ERR_SUCCESS;
}

/*
[public] Open cursor for fetch recors from class storage.
  - hclass: class instance handler
  - hcur: pointer to cursor handler
  - result: imdb error code
*/
imdb_errcode_t  ICACHE_FLASH_ATTR
imdb_class_query (imdb_hndlr_t hclass, imdb_access_path_t access_path, imdb_hndlr_t * hcur)
{
    d_imdb_check_hndlr (hclass);
    imdb_block_class_t *class_block = d_hndlr2obj (imdb_block_class_t, hclass);
    imdb_cursor_t  *cur = NULL;
    imdb_errcode_t  ret = imdb_clsobj_insert (class_block->dbclass.imdb->hcurs, (void **) &cur, 0);
    d_assert (ret == IMDB_ERR_SUCCESS, "res=%u", ret);

    imdb_access_path_t access_path2 = access_path;
    if (access_path2 == PATH_NONE) {
	if (class_block->dbclass.cdef.opt_recycle) {
	    access_path2 = PATH_RECYCLE_SCAN_REW;
	}
	else {
	    access_path2 = PATH_FULL_SCAN;
	}
    }

    ret = imdb_class_cur_open (&class_block->dbclass, cur, access_path2);
    if (ret != IMDB_ERR_SUCCESS) {
	imdb_clsobj_delete (class_block->dbclass.imdb->hcurs, cur);
	cur = NULL;
    }
    d_log_dprintf (IMDB_SERVICE_NAME, "query: \"%u\" open cursor %p, path=%u, res=%u", cur, hclass, access_path, ret);

    *hcur = d_obj2hndlr (cur);
    return ret;
}

/*
[public] Fetch records from opened cursor.
  - hcur: cursor handler
  - count: fetch records limit
  - rowcount: return fetched records count
  - ptr[]: array of pointer to fetched records
  - result: imdb error code
*/
imdb_errcode_t  ICACHE_FLASH_ATTR
imdb_class_fetch (imdb_hndlr_t hcur, uint16 count, uint16 * rowcount, void *ptr[])
{
    *rowcount = 0;
    d_imdb_check_hndlr (hcur);
    if (count == 0) {
	return IMDB_ERR_SUCCESS;
    }

    imdb_cursor_t  *cur = d_hndlr2obj (imdb_cursor_t, hcur);
    imdb_errcode_t  ret = imdb_class_cur_fetch (cur->dbclass, cur, count, rowcount, ptr);
    return ret;
}


/*
[public] Close cursor.
  - hcur: cursor handler
  - result: imdb error code
*/
imdb_errcode_t  ICACHE_FLASH_ATTR
imdb_class_close (imdb_hndlr_t hcur)
{
    d_imdb_check_hndlr (hcur);
    imdb_cursor_t  *cur = d_hndlr2obj (imdb_cursor_t, hcur);
    imdb_errcode_t  ret = imdb_clsobj_delete (cur->dbclass->imdb->hcurs, cur);

    return ret;
}

imdb_errcode_t  ICACHE_FLASH_ATTR
imdb_cursor_forall (imdb_hndlr_t hcur, void *data, imdb_forall_func forall_func)
{
    d_imdb_check_hndlr (hcur);
    void           *ptrs[10];
    imdb_errcode_t  ret = IMDB_ERR_SUCCESS;
    imdb_errcode_t  ret2 = IMDB_ERR_SUCCESS;
    uint16          rcnt;
    uint16          i;

    while (ret == IMDB_ERR_SUCCESS) {
	ret = imdb_class_fetch (hcur, 10, &rcnt, ptrs);
	if (ret != IMDB_ERR_SUCCESS && ret != IMDB_CURSOR_NO_DATA_FOUND) {
	    return ret;
	}
	for (i = 0; i < rcnt; i++) {
	    ret2 = forall_func (ptrs[i], data);
	    switch (ret2) {
	    case IMDB_ERR_SUCCESS:
		break;
	    case IMDB_CURSOR_BREAK:
		return IMDB_ERR_SUCCESS;
	    default:
		return IMDB_CURSOR_FORALL_FUNC;
	    }
	}
    }
    return IMDB_ERR_SUCCESS;
}

imdb_errcode_t  ICACHE_FLASH_ATTR
imdb_class_forall (imdb_hndlr_t hclass, void *data, imdb_forall_func forall_func)
{
    imdb_hndlr_t    hcur;
    imdb_errcode_t  ret = imdb_class_query (hclass, PATH_NONE, &hcur);
    if (ret != IMDB_ERR_SUCCESS) {
	return ret;
    }
    ret = imdb_cursor_forall (hcur, data, forall_func);
    imdb_errcode_t  ret2 = imdb_class_close (hcur);
    if (ret != IMDB_ERR_SUCCESS) {
	return ret;
    }
    else {
	return ret2;
    }
}

imdb_errcode_t  ICACHE_FLASH_ATTR
forall_count (void *ptr, void *data)
{
    uint32         *objcount = (uint32 *) data;
    (*objcount)++;
    return IMDB_ERR_SUCCESS;
}
