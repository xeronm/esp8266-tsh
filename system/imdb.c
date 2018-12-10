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
#include "misc/idxhash.h"
#include "system/imdb.h"
#include "crypto/crc.h"

#define IMDB_PCT_FREE_MAX		30      // 60% (4 bits, 1unit = 2%)
#define IMDB_BLOCK_SIZE_MIN		512     // 512 Bytes

#ifdef IMDB_SMALL_RAM
#define IMDB_BLOCK_SIZE_DEFAULT	1024    // 1 KBytes
#define IMDB_FIRST_PAGE_BLOCKS_MIN	1       // 1 blocks in first page
#define IMDB_CURSOR_PAGE_BLOCKS	2       //
#else
#define IMDB_BLOCK_SIZE_DEFAULT	4096    // 4 KBytes
#define IMDB_FIRST_PAGE_BLOCKS_MIN	4       // 4 blocks in first page
#define IMDB_CURSOR_PAGE_BLOCKS	4       //
#endif

#define IMDB_FIRST_PAGE_BLOCKS_DIV	2

// determines the maximum count of sequential skip of the free slot after which the slot will be removed from free list
// used only with variable storages
#define IMDB_SLOT_SKIP_COUNT_MAX	16

#define	IMDB_SERVICE_NAME		"imdb"

#define d_obj2hndlr(obj)		(imdb_hndlr_t) (obj)
#define d_hndlr2obj(type, hndlr)	(type *) (hndlr)        // TODO: сделать проверку на тип

LOCAL const char *sz_imdb_error[] RODATA = {
    "",
    "internal error",
    "page allocation error",
    "",
    "",
    "allocation error: pages max",
    "",
    "",
    "",
    "",
    "",
    "",
    "file %p:%u read error: %d",
    "file %p:%u write error: %d",
    "file %p:%u chksum error: %u",
    "file lock error %d:%d",
    "cache capacity error, block %p",
    "block %p access error",
};

struct imdb_class_s;
struct imdb_page_s;
struct imdb_block_s;
struct imdb_block_page_s;
struct imdb_block_class_s;
struct imdb_bc_free_block_s;

#define BLOCK_PTR_RAW_NONE	((size_t) 0)

typedef union class_ptr_u {
    struct imdb_block_class_s *mptr;
    size_t          fptr;       // relative datafile pointer
    size_t          raw;
} class_ptr_t;

typedef union page_ptr_u {
    struct imdb_block_page_s *mptr;
    size_t          fptr;       // relative datafile pointer
    size_t          raw;
} page_ptr_t;

typedef union block_ptr_u {
    struct imdb_block_s *mptr;
    size_t          fptr;       // relative datafile pointer
    size_t          raw;
} block_ptr_t;

typedef struct imdb_bc_block_s {
    struct imdb_block_s *mptr;  // pointer to memory
    uint32          wcnt;       // Fixme: No difference between read/write, used as a rcnt
    uint32          rcnt;
} imdb_bc_block_t;

typedef struct imdb_bc_free_block_s {
    struct imdb_bc_free_block_s *fl_next_block;
} imdb_bc_free_block_t;

typedef struct imdb_s {
    imdb_def_t      db_def;
    obj_size_t      obj_bsize_max;
    imdb_stat_t     stat;
    class_ptr_t     class_first;
    class_ptr_t     class_last;
} imdb_t;

typedef struct imdb_bc_s {
    imdb_t          base;
    ih_hndlr_t      hbcmap;
    uint8          *buffer_cache;
    size_t          bcmap_size;
    imdb_bc_free_block_t *bc_free_list;
    char            bcmap[];
} imdb_bc_t;

typedef enum imdb_lock_s {
    DATA_LOCK_NONE = 0,
    DATA_LOCK_READ = 1,
    DATA_LOCK_WRITE = 2,
    DATA_LOCK_EXCLUSIVE = 3
} imdb_lock_t;

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
#define	d_stat_block_read(imdb)		{ (imdb)->stat.block_read++; }
#define	d_stat_block_write(imdb)	{ (imdb)->stat.block_write++; }

typedef enum imdb_data_slot_type_s {
    DATA_SLOT_TYPE_1 = 0,
    DATA_SLOT_TYPE_2 = 1,
    DATA_SLOT_TYPE_3 = 2,
    DATA_SLOT_TYPE_4 = 3
} imdb_data_slot_type_t;

typedef struct imdb_class_s {
    imdb_class_def_t cdef;
    class_ptr_t     class_prev;
    class_ptr_t     class_next;
    page_ptr_t      page_last;
    page_ptr_t      page_fl_first;
    class_pages_t   page_count;
    obj_size_t      obj_bsize_min;
    imdb_data_slot_type_t ds_type:2;
    uint8           reserved:6;
} imdb_class_t;


typedef struct imdb_page_s {
    page_ptr_t      page_next;
    page_ptr_t      page_prev;
    page_ptr_t      page_fl_next;       // previous page with not empty free list
    page_blocks_t   alloc_hwm;
    page_blocks_t   block_fl_first;     // page free list pointer to first block
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

typedef enum imdb_block_type_s {
    BLOCK_TYPE_NONE = 0,
    BLOCK_TYPE_PAGE = 1,
    BLOCK_TYPE_CLASS = 2,
} imdb_block_type_t;

#define SLOT_FLAG_FREE		1U
#define SLOT_FLAG_UNFORMATTED	2U
#define SLOT_FLAG_DATA		3U

#define d_acquire_block(imdb, bptr, lock)	((imdb)->db_def.opt_media) ? fdb_cache_get((imdb_bc_t*)(imdb), (bptr).fptr, false, (lock)) : d_pointer_as (imdb_block_t, (bptr).mptr)
#define d_acquire_page_block(imdb, pptr, lock)	d_pointer_as (imdb_block_page_t, d_acquire_block ((imdb), (pptr), (lock)))
#define d_acquire_class_block(imdb, pptr)	d_pointer_as (imdb_block_class_t, d_acquire_block ((imdb), (pptr), DATA_LOCK_READ))

#define d_setwrite_block(imdb, block)	((imdb)->db_def.opt_media) ? fdb_cache_setlock((imdb_bc_t*)(imdb), d_pointer_as (imdb_block_t, (block)), DATA_LOCK_WRITE) : true

#define d_release_block(imdb, block)	\
	{ \
		if ( ((imdb)->db_def.opt_media) && ((block)->btype == BLOCK_TYPE_NONE) ) \
			((block)->lock_flag = DATA_LOCK_NONE); \
	}

#define d_release_page_block(imdb, page_block) \
	{ \
		if ( ((imdb)->db_def.opt_media) && ((page_block)->block.btype == BLOCK_TYPE_PAGE) ) \
			((page_block)->block.lock_flag = DATA_LOCK_NONE); \
	}

#define d_release_class_block(imdb, class_block) \
	{ \
		if ( ((imdb)->db_def.opt_media) && ((class_block)->block.btype == BLOCK_TYPE_CLASS) ) \
			((class_block)->block.lock_flag = DATA_LOCK_NONE); \
	}

#define d_page_get_blockid_byidx(page_block, bidx, bsize) \
	((page_block)->block.id.raw + (bidx-1)*(bsize))

#define d_block_get_page_blockid(block, bsize) \
	((block)->id.raw - ((block)->block_index-1)*(bsize))

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
#define d_block_slot_has_footer(dbclass) 	((dbclass)->ds_type >= DATA_SLOT_TYPE_3)
#define d_dstype_slot_has_footer(ds_type)	((ds_type) >= DATA_SLOT_TYPE_3)

// return FreeSlot Footer
#define d_block_slot_free_footer(slot) \
	d_pointer_add(imdb_slot_footer_t, (slot), d_bptr_size((slot)->length) - sizeof(imdb_slot_footer_t));
// return Slot Footer
#define d_block_slot_footer(slot) \
	d_pointer_add(imdb_slot_footer_t, (slot), d_bptr_size((slot)->length) - sizeof(imdb_slot_footer_t));

// return next FreeSlot
#define d_block_next_slot_free(block, fslot) \
	( ((fslot)->next_offset)? d_pointer_add(imdb_slot_free_t, (block), d_bptr_size((fslot)->next_offset)): NULL)

// return block user data upper limit in block units
#define d_block_upper_data_blimit(imdb, block) \
	(d_size_bptr((imdb)->db_def.block_size) - (block)->footer_offset)
// return block user data lower limit in block units
#define d_block_lower_data_blimit(block) \
	(d_size_bptr(block_header_size[(block)->btype]))
// return block user data lower limit in bytes
#define d_block_lower_data_limit(block)	\
	(block_header_size[(block)->btype])


typedef struct imdb_block_s {
    block_ptr_t     id;
    uint16          crc16;      // whole block CRC
    uint8           crchdr;     // header CRC Fixme: make header CRC write and check for detect metadata corruption
    page_blocks_t   block_index;        // index of block
    imdb_lock_t     lock_flag:2;        // Fixme: Current usage are weird
    uint8           footer_offset:6;    // slot offset from the ending of block
    imdb_block_type_t btype:2;
    uint16          free_offset:14;     // offset in IMDB_BLOCK_UNIT_ALIGN
    page_blocks_t   block_fl_next;      // next block inside page with not empty free list 
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
    imdb_lock_t     lock_flag:2;
    uint16          block_offset:14;
    uint8           flags:2;
    uint8           reserved1:6;
    uint8           tx_slot;
} imdb_slot_data2_t;

typedef struct imdb_slot_data4_s {
    imdb_lock_t     lock_flag:2;
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
    imdb_t         *imdb;
    class_ptr_t     class;
    imdb_rowid_t    rowid_last;
    uint32          fetch_recs;
    imdb_access_path_t access_path;
} imdb_cursor_t;

typedef struct imdb_free_slot_find_ctx_s {
    page_ptr_t      page_block_fl_prev;
    block_ptr_t     block_fl_prev;
    // real pointers
    imdb_block_page_t *page_block;
    imdb_block_t   *block;
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

// Insert Page into Class LIFO Free-List
#define d_imdb_class_fl_insert_page(class_block, page_block) \
	{ \
		page_block->page.page_fl_next.raw = class_block->dbclass.page_fl_first.raw; \
		class_block->dbclass.page_fl_first.raw = page_block->block.id.raw; \
	}


// Insert Block into Page LIFO Free-List
#define d_imdb_page_fl_insert_block(page_block, block) \
	{ \
		(block)->block_fl_next = (page_block)->page.block_fl_first; \
		(page_block)->page.block_fl_first = (block)->block_index; \
	}

// Insert Slot into Block LIFO Free-List
#define d_imdb_block_fl_insert_slot(block, slot_free) \
	{ \
		(slot_free)->next_offset = (block)->free_offset; \
		(block)->free_offset = d_size_bptr (d_pointer_diff ((slot_free), (block))); \
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
    slot_bsize = slot_free->length;     // use length from free_slot, may be greater than slot_bsize

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
[private] Get offset of last slot in block
  - dbclass: 
  - block:
  - [out] last_offset: result last data slot end offset
*/
INLINED void    ICACHE_FLASH_ATTR
imdb_block_slot_data_last (imdb_t * imdb, imdb_class_t * dbclass, imdb_block_t * block, block_size_t * last_offset)
{
    block_size_t    boffset = d_block_upper_data_blimit (imdb, block);

    if (d_dstype_slot_has_footer (dbclass->ds_type)) {
        imdb_slot_footer_t *slot_footer;
        slot_footer = d_pointer_add (imdb_slot_footer_t, block, d_bptr_size (boffset) - sizeof (imdb_slot_footer_t));
        if (slot_footer->flags == SLOT_FLAG_FREE) {
            d_assert (boffset > slot_footer->length, "offset=%u, len=%u", boffset, slot_footer->length);
            *last_offset = boffset - slot_footer->length;
        }
        else
            *last_offset = boffset;
    }
    else {
        d_assert (!dbclass->cdef.opt_variable, "variable size");
        *last_offset = boffset - (boffset - d_block_lower_data_blimit (block)) % dbclass->obj_bsize_min;
    }
}

LOCAL imdb_errcode_t ICACHE_FLASH_ATTR
fdb_cache_flush (imdb_bc_t * imdb_bc, imdb_bc_block_t * bc_block, size_t block_addr, bool fremove)
{
    block_size_t    block_size = imdb_bc->base.db_def.block_size;
    if (bc_block->wcnt > 0) {
        bc_block->mptr->crc16 = IMDB_BLOCK_CRC_DEFAULT;
#ifdef IMDB_BLOCK_CRC
        bc_block->mptr->crc16 = crc16 (d_pointer_as (unsigned char, bc_block->mptr), block_size);
#endif

        imdb_bc->base.stat.block_write++;
        size_t          hres = fio_user_write (block_addr, (uint32 *) bc_block->mptr, block_size);
        if (hres != block_size) {
            d_log_cprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_FILE_WRITE_ERROR], block_addr, block_size, hres);
            return IMDB_FILE_WRITE_ERROR;
        }
        d_log_dprintf (IMDB_SERVICE_NAME, "fdb flush id=%p, block=%p", block_addr, bc_block->mptr);
    }

    if (fremove) {
        imdb_bc_free_block_t *fl_block = d_pointer_as (imdb_bc_free_block_t, bc_block->mptr);
        fl_block->fl_next_block = imdb_bc->bc_free_list;
        imdb_bc->bc_free_list = fl_block;

        ih_hash8_remove (imdb_bc->hbcmap, (void *) &block_addr, 0);
    }
    else {
        bc_block->wcnt = 0;
    }

    return IMDB_ERR_SUCCESS;
}

typedef struct fdb_lru_data_s {
    size_t          rawid;
    imdb_bc_block_t *candidate;
} fdb_lru_data_t;

LOCAL void      ICACHE_FLASH_ATTR
fdb_forall_cache_lru (const char *key, ih_size_t keylen, const char *value, ih_size_t valuelen, void *data)
{
    imdb_bc_block_t *bc_block = d_pointer_as (imdb_bc_block_t, value);
    fdb_lru_data_t *lru_data = d_pointer_as (fdb_lru_data_t, data);

    //os_printf("-- cache %p:%p, r=%u,w=%u lock=%u\n", *((size_t*)key), bc_block->mptr, bc_block->rcnt, bc_block->wcnt, bc_block->mptr->lock_flag);
    if (bc_block->rcnt > 3)
        bc_block->rcnt = bc_block->rcnt >> 1;
    if (bc_block->wcnt > 3)
        bc_block->wcnt = bc_block->wcnt >> 1;
    if (((!lru_data->candidate) || (lru_data->candidate->rcnt < bc_block->rcnt)) && (!bc_block->mptr->lock_flag)) {
        lru_data->candidate = bc_block;
        lru_data->rawid = *((size_t *) key);
    }
}

typedef struct fdb_flush_data_s {
    imdb_bc_t      *imdb_bc;
} fdb_flush_data_t;

LOCAL void      ICACHE_FLASH_ATTR
fdb_forall_cache_flush (const char *key, ih_size_t keylen, const char *value, ih_size_t valuelen, void *data)
{
    imdb_bc_block_t *bc_block = d_pointer_as (imdb_bc_block_t, value);
    fdb_flush_data_t *flush_data = d_pointer_as (fdb_flush_data_t, data);

    //os_printf("-- cache %p:%p, r=%u,w=%u lock=%u\n", *((size_t*)key), bc_block->mptr, bc_block->rcnt, bc_block->wcnt, bc_block->mptr->lock_flag);
    if (bc_block->wcnt)
        fdb_cache_flush (flush_data->imdb_bc, bc_block, *((size_t *) key), false);
    bc_block->rcnt = 0;
}

LOCAL imdb_block_t *ICACHE_FLASH_ATTR
fdb_cache_get (imdb_bc_t * imdb_bc, size_t block_addr, bool alloc_new, imdb_lock_t lock)
{
    d_assert (block_addr, "block_addr=0");

    imdb_bc_block_t *bc_block;
    imdb_block_t   *mblock = NULL;
    ih_errcode_t    res = ih_hash8_search (imdb_bc->hbcmap, (const char *) &block_addr, 0, (char **) &bc_block);
    if (res == IH_ENTRY_NOTFOUND) {
        block_size_t    block_size = imdb_bc->base.db_def.block_size;
        if (!imdb_bc->bc_free_list) {
            fdb_lru_data_t  data;
            os_memset (&data, 0, sizeof (fdb_lru_data_t));

            ih_hash8_forall (imdb_bc->hbcmap, fdb_forall_cache_lru, &data);

            if (data.candidate) {
                fdb_cache_flush (imdb_bc, data.candidate, data.rawid, true);
            }
            else {
                d_log_eprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_CACHE_CAPACITY], block_addr);
                return NULL;
            }
        }

        mblock = d_pointer_as (imdb_block_t, imdb_bc->bc_free_list);
        imdb_bc->bc_free_list = imdb_bc->bc_free_list->fl_next_block;
        if (alloc_new) {
#ifdef IMDB_ZERO_MEM
            os_memset (mblock, 0, block_size);
#else
            os_memset (mblock, 0, sizeof (imdb_block_page_t));
#endif
            mblock->id.fptr = block_addr;
        }
        else {
            imdb_bc->base.stat.block_read++;
            size_t          hres = fio_user_read (block_addr, (uint32 *) mblock, block_size);
            if (hres != imdb_bc->base.db_def.block_size) {
                d_log_eprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_FILE_READ_ERROR], block_addr, block_size, hres);
                goto get_error;
            }

#ifdef IMDB_BLOCK_CRC
            uint16          crc = mblock->crc16;
            mblock->crc16 = IMDB_BLOCK_CRC_DEFAULT;
            mblock->crc16 = crc16 (d_pointer_as (unsigned char, mblock), block_size);
#else
            uint16          crc = IMDB_BLOCK_CRC_DEFAULT;
#endif
            if (mblock->crc16 != crc) {
                d_log_eprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_FILE_CRC_ERROR], block_addr, block_size,
                               mblock->crc16);
                goto get_error;
            }

            d_assert (mblock->lock_flag == DATA_LOCK_NONE, "block addr=%p, lock=%u", block_addr, mblock->lock_flag);
        }

        res = ih_hash8_add (imdb_bc->hbcmap, (const char *) &block_addr, 0, (char **) &bc_block, 0);
        if (res != IH_ERR_SUCCESS) {
            d_log_cprintf (IMDB_SERVICE_NAME, "block addr=%p, hash bcmap res=%u", block_addr, res);
            goto get_error;
        }
        bc_block->rcnt = 0;
        bc_block->wcnt = 0;
        bc_block->mptr = mblock;
        d_log_dprintf (IMDB_SERVICE_NAME, "fdb get id=%p, block=%p", block_addr, mblock);
    }
    else if (res != IH_ERR_SUCCESS) {
        d_log_cprintf (IMDB_SERVICE_NAME, "block addr=%p, hash bcmap res=%u", block_addr, res);
        return NULL;
    }

    if (bc_block->mptr->lock_flag > lock) {
        d_log_eprintf (IMDB_SERVICE_NAME, "block addr=%p, lock error %u:%u", block_addr, bc_block->mptr->lock_flag,
                       lock);
        return NULL;
    }

    if (bc_block->mptr->lock_flag == DATA_LOCK_NONE)
        bc_block->rcnt++;
    if (((lock == DATA_LOCK_WRITE) || (lock == DATA_LOCK_EXCLUSIVE)) && (bc_block->mptr->lock_flag <= DATA_LOCK_READ))
        bc_block->wcnt++;

    bc_block->mptr->lock_flag = lock;

    return bc_block->mptr;

  get_error:
    {
        imdb_bc_free_block_t *free_block = d_pointer_as (imdb_bc_free_block_t, mblock);
        free_block->fl_next_block = imdb_bc->bc_free_list;
        imdb_bc->bc_free_list = free_block;
    }
    return NULL;
}

LOCAL bool      ICACHE_FLASH_ATTR
fdb_cache_setlock (imdb_bc_t * imdb_bc, imdb_block_t * block, imdb_lock_t lock)
{
    if (block->lock_flag == lock)
        return true;
    else if (block->lock_flag > lock) {
        d_log_eprintf (IMDB_SERVICE_NAME, "block addr=%p, lock error %u:%u", block->id.raw, block->lock_flag, lock);
        return false;
    }

    if ((lock >= DATA_LOCK_WRITE) && (block->lock_flag <= DATA_LOCK_READ)) {
        imdb_bc_block_t *bc_block;
        ih_errcode_t    res = ih_hash8_search (imdb_bc->hbcmap, (const char *) &block->id.raw, 0, (char **) &bc_block);
        if (res != IH_ERR_SUCCESS)
            return false;
        bc_block->wcnt++;
    }

    return true;
}

imdb_errcode_t  ICACHE_FLASH_ATTR
fdb_header_read (imdb_file_t * hdr_file)
{
    size_t          hres = fio_user_read (0, (uint32 *) hdr_file, sizeof (imdb_file_t));
    if (hres != sizeof (imdb_file_t)) {
        d_log_eprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_FILE_READ_ERROR], 0, sizeof (imdb_file_t), hres);
        return IMDB_FILE_READ_ERROR;
    }

#ifdef IMDB_BLOCK_CRC
    uint16          crc = hdr_file->crc16;
    hdr_file->crc16 = IMDB_BLOCK_CRC_DEFAULT;
    hdr_file->crc16 = crc16 (d_pointer_as (unsigned char, hdr_file), sizeof (imdb_file_t));
#else
    uint16          crc = IMDB_BLOCK_CRC_DEFAULT;
#endif
    if (hdr_file->crc16 != crc) {
        d_log_eprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_FILE_CRC_ERROR], 0, sizeof (imdb_file_t), hdr_file->crc16);
        return IMDB_FILE_CRC_ERROR;
    }

    return IMDB_ERR_SUCCESS;
}

LOCAL imdb_errcode_t ICACHE_FLASH_ATTR
fdb_header_write (imdb_file_t * hdr_file)
{
    hdr_file->scn++;
    hdr_file->crc16 = IMDB_BLOCK_CRC_DEFAULT;
#ifdef IMDB_BLOCK_CRC
    hdr_file->crc16 = crc16 (d_pointer_as (unsigned char, hdr_file), sizeof (imdb_file_t));
#endif
    size_t          hres = fio_user_write (0, (uint32 *) hdr_file, sizeof (imdb_file_t));
    if (hres != sizeof (imdb_file_t)) {
        d_log_eprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_FILE_WRITE_ERROR], 0, sizeof (imdb_file_t), hres);
        return IMDB_FILE_WRITE_ERROR;
    }

    return IMDB_ERR_SUCCESS;
}

/*
[private] Shift offset to previous DataSlot and return data pointer (only for recycled storage). There are no bounds checks.
  - dbclass: 
  - block:
  - [in/out] slot_offset: shift from DataSlot to previous DataSlot
  - [out] ptr: previous DataSlot data pointer
*/
LOCAL void      ICACHE_FLASH_ATTR
imdb_block_slot_prev (imdb_block_class_t * class_block, imdb_block_t * block, block_size_t * slot_offset, void **ptr)
{
    imdb_slot_footer_t *slot_footer;
    //imdb_slot_data2_t* slot_data2;
    imdb_slot_data4_t *slot_data4;

#ifdef ASSERT_DEBUG
    block_size_t    offset_limit;
#endif
    block_size_t    offset = *slot_offset;
    imdb_data_slot_type_t ds_type = class_block->dbclass.ds_type;
    obj_size_t      obj_bsize_min = class_block->dbclass.obj_bsize_min;

    switch (ds_type) {
    case DATA_SLOT_TYPE_1:
#ifdef ASSERT_DEBUG
        offset_limit = d_block_lower_data_blimit (block);
#endif
        d_assert (offset - obj_bsize_min >= offset_limit, "offset=%u, objlen=%u, limit=%u", offset,
                  obj_bsize_min, offset_limit);
        offset -= obj_bsize_min;
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
        d_assert (false, "ds_type=%u", ds_type);
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
imdb_block_slot_next (imdb_t * imdb, imdb_block_class_t * class_block, imdb_block_t * block, block_size_t * slot_offset,
                      void **ptr)
{
    imdb_slot_free_t *slot_free;

#ifdef ASSERT_DEBUG
    block_size_t    offset_limit;
#endif
    block_size_t    offset = *slot_offset;
    block_size_t    offset_add = 0;
    imdb_data_slot_type_t ds_type = class_block->dbclass.ds_type;
    obj_size_t      obj_bsize_min = class_block->dbclass.obj_bsize_min;

    switch (ds_type) {
    case DATA_SLOT_TYPE_1:
#ifdef ASSERT_DEBUG
        offset_limit = d_block_upper_data_blimit (imdb, block);
#endif
        d_assert (offset + obj_bsize_min <= offset_limit, "offset=%u, objlen=%u, limit=%u", offset,
                  obj_bsize_min, offset_limit);
        *ptr = d_pointer_add (void, block, d_bptr_size (offset));
        offset += obj_bsize_min;
        break;
    case DATA_SLOT_TYPE_2:
    case DATA_SLOT_TYPE_3:
    case DATA_SLOT_TYPE_4:
#ifdef ASSERT_DEBUG
        offset_limit = d_block_upper_data_blimit (imdb, block);
#endif
        d_assert (offset + data_slot_type_bsize[ds_type] <= offset_limit, "offset=%u, shlen=%u, limit=%u",
                  offset, data_slot_type_bsize[ds_type], offset_limit);
        slot_free = d_pointer_add (imdb_slot_free_t, block, d_bptr_size (offset));
        d_assert (slot_free->flags == SLOT_FLAG_DATA
                  || slot_free->flags == SLOT_FLAG_FREE, "flags=%u", slot_free->flags);

        if (slot_free->flags == SLOT_FLAG_DATA) {
            *ptr = d_pointer_add (void, slot_free, sizeof (imdb_slot_free_t));
            offset_add =
                (ds_type == DATA_SLOT_TYPE_2) ? (obj_bsize_min + data_slot_type_bsize[ds_type]) : slot_free->length;
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
        d_assert (false, "ds_type=%u", ds_type);
    }

    *slot_offset = offset;
}

/*
[private] Initialize Freeslot for new block
  - imdb:
  - ds_type: 
  - page_block:
  - block:
*/
LOCAL void      ICACHE_FLASH_ATTR
imdb_block_slot_init (imdb_t * imdb, imdb_data_slot_type_t ds_type, imdb_block_page_t * page_block,
                      imdb_block_t * block)
{
    d_stat_slot_free (imdb);

    obj_size_t      slen = d_block_lower_data_limit (block);
    imdb_slot_free_t *slot_free = d_pointer_add (imdb_slot_free_t, (block), slen);
    slot_free->flags = SLOT_FLAG_FREE;
    block->footer_offset = 0;
    slot_free->length = d_block_upper_data_blimit (imdb, block) - d_size_bptr (slen);

    if (d_dstype_slot_has_footer (ds_type)) {
        imdb_slot_footer_t *slot_footer = d_block_slot_free_footer (slot_free);
        os_memset (slot_footer, 0, sizeof (imdb_slot_footer_t));
        slot_footer->flags = SLOT_FLAG_FREE;
        slot_footer->length = slot_free->length;
    }

    d_log_dprintf (IMDB_SERVICE_NAME, "slot_init: slot=%p, len=%u", slot_free, slot_free->length);

    d_imdb_block_fl_insert_slot (block, slot_free);
}

LOCAL imdb_block_t *ICACHE_FLASH_ATTR
imdb_page_block_alloc (imdb_t * imdb, imdb_block_class_t * class_block, imdb_block_page_t * page_block);

/*
[private] Recycle next block for target block.
  - imdb:
  - class_block:
  - page_block: results page for recycled block
  - block: target block
  - return: recycled block
*/
LOCAL imdb_block_t *ICACHE_FLASH_ATTR
imdb_page_block_recycle (imdb_t * imdb, imdb_block_class_t * class_block, imdb_block_page_t ** page_block,
                         imdb_block_t * block)
{
    d_assert (class_block->dbclass.page_fl_first.mptr == NULL, "page_fl_first=%p",
              class_block->dbclass.page_fl_first.mptr);

    block_size_t    bsize = imdb->db_def.block_size;
    imdb_block_t   *block_targ = NULL;
    imdb_block_page_t *page_targ = *page_block;

    page_blocks_t   bidx;
    block_ptr_t     block_ptr;

    if (block->block_index == class_block->dbclass.cdef.page_blocks) {
        // try to recycle next page
        d_release_block (imdb, &page_targ->block);

        page_targ =
            (page_targ->page.page_next.raw != BLOCK_PTR_RAW_NONE) ? d_acquire_page_block (imdb,
                                                                                          page_targ->page.page_next,
                                                                                          DATA_LOCK_WRITE) :
            d_pointer_as (imdb_block_page_t, class_block);
        if (!page_targ) {
            d_log_cprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_BLOCK_ACCESS], block_ptr.raw);
            return NULL;
        }

        *page_block = page_targ;
        bidx = 1;
    }
    else                        // try to recycle block in this page
        bidx = block->block_index + 1;

    d_release_block (imdb, block);
    block_ptr.raw = d_page_get_blockid_byidx (page_targ, bidx, bsize);
    block_targ = d_acquire_block (imdb, block_ptr, DATA_LOCK_WRITE);
    if (!block_targ) {
        d_log_cprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_BLOCK_ACCESS], block_ptr.raw);
        return NULL;
    }

#ifdef IMDB_BLOCK_CRC
#else
    d_assert (block_targ->crc16 == IMDB_BLOCK_CRC_DEFAULT, "crc=%u", block_targ->crc16);
#endif

    d_log_dprintf (IMDB_SERVICE_NAME, "recycle: class page=%p block#%d=%p, size=%u", page_targ, bidx, block_targ,
                   bsize);
    d_stat_block_recycle (imdb);
    block->block_fl_next = 0;
    block->free_offset = 0;
    block->lock_flag = 0;

    imdb_block_slot_init (imdb, class_block->dbclass.ds_type, page_targ, block_targ);

    d_setwrite_block (imdb, page_targ);
    d_imdb_page_fl_insert_block (page_targ, block_targ);

    d_setwrite_block (imdb, class_block);
    d_imdb_class_fl_insert_page (class_block, page_targ);

#ifdef IMDB_BLOCK_CRC
#else
    block_targ->crc16 = IMDB_BLOCK_CRC_DEFAULT;
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
LOCAL imdb_errcode_t ICACHE_FLASH_ATTR
imdb_slot_free_extract (imdb_t * imdb, imdb_block_class_t * class_block,
                        imdb_free_slot_find_ctx_t * find_ctx, obj_size_t slot_bsize, obj_size_t extra_bsize)
{
    d_setwrite_block (imdb, find_ctx->block);

    imdb_slot_free_t *slot_free = find_ctx->slot_free;
    d_assert (slot_free->flags == SLOT_FLAG_FREE, "flags=%u", slot_free->flags);

    imdb_slot_free_t *slot_free_next = NULL;

    if (slot_free->length >= slot_bsize + extra_bsize + class_block->dbclass.obj_bsize_min) {
        d_stat_slot_split (imdb);
        slot_free_next = d_pointer_add (imdb_slot_free_t, slot_free, d_bptr_size (slot_bsize));
        slot_free_next->flags = SLOT_FLAG_FREE;
        slot_free_next->length = slot_free->length - slot_bsize;
        slot_free_next->next_offset = slot_free->next_offset;

        d_log_dprintf (IMDB_SERVICE_NAME, "slot_extract: split free slot=%p, len=%u", slot_free_next,
                       slot_free_next->length);

        if (d_dstype_slot_has_footer (class_block->dbclass.ds_type)) {
            imdb_slot_footer_t *slot_footer = d_block_slot_free_footer (slot_free);
            d_assert (slot_free->flags == SLOT_FLAG_FREE, "flags=%u", slot_free->flags);
            slot_footer->length = slot_free_next->length;
            slot_footer->skip_count = 0;        // reset skip count
        }

        slot_free->length = slot_bsize;
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
        d_setwrite_block (imdb, find_ctx->page_block);

        find_ctx->block->free_offset = 0;
        if (!find_ctx->block_fl_prev.mptr) {
            if (!find_ctx->block->block_fl_next) {
                find_ctx->page_block->page.block_fl_first = 0;
                if (find_ctx->page_block->page.alloc_hwm < class_block->dbclass.cdef.page_blocks) {
                    // allocate next block in this page
                    if (!imdb_page_block_alloc (imdb, class_block, find_ctx->page_block))
                        return IMDB_INTERNAL_ERROR;
                }
            }
            else {
                find_ctx->page_block->page.block_fl_first = find_ctx->block->block_fl_next;
            }
        }
        else {
            find_ctx->block_fl_prev.mptr->block_fl_next = find_ctx->block->block_fl_next;
        }

        if (!find_ctx->page_block->page.block_fl_first) {
            // delete page from Page Free List
            if (find_ctx->page_block_fl_prev.raw != BLOCK_PTR_RAW_NONE) {
                imdb_block_page_t *page = d_acquire_page_block (imdb, find_ctx->page_block_fl_prev, DATA_LOCK_WRITE);
                if (!page) {
                    d_log_cprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_BLOCK_ACCESS],
                                   find_ctx->page_block_fl_prev.raw);
                    return IMDB_BLOCK_ACCESS;
                }

                page->page.page_fl_next.raw = find_ctx->page_block->page.page_fl_next.raw;
            }
            else {
                d_setwrite_block (imdb, class_block);
                class_block->dbclass.page_fl_first.raw = find_ctx->page_block->page.page_fl_next.raw;
                if (!class_block->dbclass.page_fl_first.raw && class_block->dbclass.cdef.opt_recycle) {
                    // recycle next block
                    if (!imdb_page_block_recycle (imdb, class_block, &find_ctx->page_block, find_ctx->block))
                        return IMDB_INTERNAL_ERROR;
                }
            }
        }
    }

    slot_free->flags = SLOT_FLAG_DATA;
    return IMDB_ERR_SUCCESS;
}

LOCAL imdb_block_page_t *imdb_page_alloc (imdb_t * imdb, imdb_block_class_t * class_block);

/*
[private]: Find FreeSlot in Class FreeList
  - imdb:
  - class_block: 
  - slot_bsize: search user data size in block units
  - find_ctx: FreeSlot search context
*/
LOCAL imdb_errcode_t ICACHE_FLASH_ATTR
imdb_slot_free_find (imdb_t * imdb, imdb_block_class_t * class_block, obj_size_t slot_bsize,
                     imdb_free_slot_find_ctx_t * find_ctx)
{
    // find suitable free slot
    uint32          skipscan = 0;
    if (class_block->dbclass.page_fl_first.raw != BLOCK_PTR_RAW_NONE) {
        block_size_t    bsize = imdb->db_def.block_size;
        page_ptr_t      page_ptr;
        page_ptr.raw = class_block->dbclass.page_fl_first.raw;
        // find suitable page
        while (page_ptr.raw != BLOCK_PTR_RAW_NONE) {
            if (page_ptr.raw == class_block->block.id.raw) {
                find_ctx->page_block = d_pointer_as (imdb_block_page_t, class_block);
            }
            else {
                find_ctx->page_block = d_acquire_page_block (imdb, page_ptr, DATA_LOCK_READ);
                if (!find_ctx->page_block) {
                    d_log_eprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_BLOCK_ACCESS], page_ptr.raw);
                    return IMDB_BLOCK_ACCESS;
                }
            }

            page_blocks_t   bidx = find_ctx->page_block->page.block_fl_first;
            find_ctx->block_fl_prev.raw = BLOCK_PTR_RAW_NONE;
            // find suitable block
            while (bidx) {
                if (bidx == 1) {
                    find_ctx->block = d_pointer_as (imdb_block_t, find_ctx->page_block);
                }
                else {
                    block_ptr_t     block_ptr;
                    block_ptr.raw = d_page_get_blockid_byidx (find_ctx->page_block, bidx, bsize);
                    find_ctx->block = d_acquire_block (imdb, block_ptr, DATA_LOCK_READ);
                    if (!find_ctx->block) {
                        d_log_eprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_BLOCK_ACCESS], block_ptr.raw);
                        return IMDB_BLOCK_ACCESS;
                    }
                }

                find_ctx->slot_free = d_block_slot_free (find_ctx->block);
                find_ctx->slot_free_prev = NULL;
                // find suitable slot
                while (find_ctx->slot_free) {
                    d_assert (find_ctx->slot_free->flags == SLOT_FLAG_FREE, "flags=%u", find_ctx->slot_free->flags);
                    if (slot_bsize <= find_ctx->slot_free->length) {
                        goto slot_found;
                    }
                    if (class_block->dbclass.ds_type == DATA_SLOT_TYPE_4) {
                        imdb_slot_footer_t *slot_footer = d_block_slot_free_footer (find_ctx->slot_free);
                        slot_footer->skip_count++;
                    }
                    skipscan++;

                    find_ctx->slot_free_prev = find_ctx->slot_free;
                    find_ctx->slot_free = d_block_next_slot_free (find_ctx->block, find_ctx->slot_free);
                }
                find_ctx->block_fl_prev.raw = find_ctx->block->id.raw;
                bidx = find_ctx->block->block_fl_next;

                d_release_block (imdb, find_ctx->block);
                find_ctx->block = NULL;
            }
            find_ctx->slot_free_prev = NULL;

            // allocate new block in page
            if (find_ctx->page_block->page.alloc_hwm < class_block->dbclass.cdef.page_blocks) {
                find_ctx->block = imdb_page_block_alloc (imdb, class_block, find_ctx->page_block);
                if (!find_ctx->block)
                    return IMDB_INTERNAL_ERROR;
                find_ctx->slot_free = d_block_slot_free (find_ctx->block);
                goto slot_found;
            }

            find_ctx->page_block_fl_prev.raw = page_ptr.raw;
            page_ptr.raw = find_ctx->page_block->page.page_fl_next.raw;

            d_release_page_block (imdb, find_ctx->page_block);
            find_ctx->page_block = NULL;
        }
        find_ctx->page_block_fl_prev.raw = BLOCK_PTR_RAW_NONE;
    }

    if (!find_ctx->page_block) {
        if (class_block->dbclass.page_count < class_block->dbclass.cdef.pages_max) {
            // allocate new page
            imdb_block_page_t *new_page = imdb_page_alloc (imdb, class_block);
            if (!new_page) {
                d_log_eprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_NOMEM]);
                return IMDB_NOMEM;
            }

            find_ctx->page_block = new_page;
            find_ctx->block = d_pointer_as (imdb_block_t, new_page);
            find_ctx->slot_free = d_block_slot_free (find_ctx->block);
        }
        else {
            return IMDB_ALLOC_PAGES_MAX;
        }
    }

  slot_found:
    d_assert (slot_bsize <= find_ctx->slot_free->length, "size=%u, len=%u", slot_bsize, find_ctx->slot_free->length);
    d_stat_slot_data (imdb, skipscan);
    return IMDB_ERR_SUCCESS;
}

/*
[private]: Get FreeSlot or recycle block
  - dbclass: 
  - slot_bsize: search user data size in block units
  - find_ctx: FreeSlot search context
*/
LOCAL imdb_errcode_t ICACHE_FLASH_ATTR
imdb_slot_free_get_or_recycle (imdb_t * imdb, imdb_block_class_t * class_block, obj_size_t slot_bsize,
                               imdb_free_slot_find_ctx_t * find_ctx)
{
    d_assert (class_block->dbclass.page_fl_first.raw, "page_fl_first=%p", class_block->dbclass.page_fl_first.raw);
    find_ctx->page_block = d_acquire_page_block (imdb, class_block->dbclass.page_fl_first, DATA_LOCK_WRITE);
    if (!find_ctx->page_block) {
        d_log_eprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_BLOCK_ACCESS], class_block->dbclass.page_fl_first.raw);
        return IMDB_BLOCK_ACCESS;
    }

    page_blocks_t   bidx = find_ctx->page_block->page.block_fl_first;
    d_assert (bidx > 0, "bidx=%u", bidx);

    block_size_t    bsize = imdb->db_def.block_size;
    block_ptr_t     block_ptr;
    block_ptr.raw = d_page_get_blockid_byidx (find_ctx->page_block, bidx, bsize);
    find_ctx->block = d_acquire_block (imdb, block_ptr, DATA_LOCK_WRITE);
    if (!find_ctx->block) {
        d_log_eprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_BLOCK_ACCESS], block_ptr.raw);
        return IMDB_BLOCK_ACCESS;
    }

    find_ctx->slot_free = d_block_slot_free (find_ctx->block);
    d_assert (find_ctx->slot_free, "slot=%p", find_ctx->slot_free);
    d_assert (find_ctx->slot_free->flags == SLOT_FLAG_FREE, "flags=%u", find_ctx->slot_free->flags);
    if (slot_bsize <= find_ctx->slot_free->length) {
        return IMDB_ERR_SUCCESS;
    }

    find_ctx->block->free_offset = 0;
    find_ctx->page_block->page.block_fl_first = 0;

    if (class_block->dbclass.page_count < class_block->dbclass.cdef.pages_max) {        // allocate new page
        imdb_block_page_t *new_page = imdb_page_alloc (imdb, class_block);
        if (!new_page) {
            d_log_eprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_NOMEM]);
            return IMDB_NOMEM;
        }

        find_ctx->page_block = new_page;
        find_ctx->block = d_pointer_as (imdb_block_t, new_page);
    }
    else {
        if (find_ctx->page_block->page.alloc_hwm < class_block->dbclass.cdef.page_blocks) {     // allocate next block in this page
            d_assert (find_ctx->page_block->page.alloc_hwm == find_ctx->block->block_index, "hwm=%u, bidx=%u",
                      find_ctx->page_block->page.alloc_hwm, find_ctx->block->block_index);
            find_ctx->block = imdb_page_block_alloc (imdb, class_block, find_ctx->page_block);
            if (!find_ctx->block)
                return IMDB_INTERNAL_ERROR;
        }
        else {                  // recycle next block;
            d_setwrite_block (imdb, class_block);
            class_block->dbclass.page_fl_first.raw = BLOCK_PTR_RAW_NONE;
            find_ctx->block = imdb_page_block_recycle (imdb, class_block, &find_ctx->page_block, find_ctx->block);
        }
    }
    find_ctx->slot_free = d_block_slot_free (find_ctx->block);

    return IMDB_ERR_SUCCESS;
}

/*
[inline] Initialize new Page
  - imdb:
  - class_block:
  - page_block:
  - btype: block type
*/
INLINED void    ICACHE_FLASH_ATTR
imdb_page_init (imdb_t * imdb, imdb_block_class_t * class_block, imdb_block_page_t * page_block,
                imdb_block_type_t btype)
{
    imdb_block_t   *block = &(page_block->block);
    imdb_page_t    *page = &page_block->page;

    block->btype = btype;
    page->alloc_hwm = 1;
    block->block_index = 1;

    d_stat_block_init (imdb);
    imdb_block_slot_init (imdb, class_block->dbclass.ds_type, page_block, block);
    d_imdb_page_fl_insert_block (page_block, block);

#ifdef IMDB_BLOCK_CRC
#else
    block->crc16 = IMDB_BLOCK_CRC_DEFAULT;
#endif
}

/*
[private]: Allocate new Block in Page
  - dbclass: 
  - page_block: target page block
  - result: allocated block
*/
LOCAL imdb_block_t *ICACHE_FLASH_ATTR
imdb_page_block_alloc (imdb_t * imdb, imdb_block_class_t * class_block, imdb_block_page_t * page_block)
{
    d_assert (page_block->block.btype == BLOCK_TYPE_PAGE
              || page_block->block.btype == BLOCK_TYPE_CLASS, "btype=%u", page_block->block.btype);

#ifdef IMDB_BLOCK_CRC
#else
    d_assert (page_block->block.crc16 == IMDB_BLOCK_CRC_DEFAULT, "crc=%u", page_block->block.crc16);
#endif

    block_size_t    block_size = imdb->db_def.block_size;
    imdb_page_t    *page = &page_block->page;
    d_assert (page->alloc_hwm < class_block->dbclass.cdef.page_blocks, "hwm=%u, blocks=%u", page->alloc_hwm,
              class_block->dbclass.cdef.page_blocks);
    page_blocks_t   bidx = ++page->alloc_hwm;
    d_assert (bidx > 0, "bidx=%u", bidx);

    block_ptr_t     block_ptr;
    block_ptr.raw = d_page_get_blockid_byidx (page_block, bidx, block_size);

    imdb_block_t   *block = NULL;
    if (imdb->db_def.opt_media) {
        block = fdb_cache_get (d_pointer_as (imdb_bc_t, imdb), block_ptr.raw, true, DATA_LOCK_WRITE);
        if (!block)
            return NULL;
    }
    else {
        block = d_pointer_as (imdb_block_t, block_ptr.mptr);
#ifdef IMDB_ZERO_MEM
        os_memset (block, 0, block_size);
#else
        os_memset (block, 0, sizeof (imdb_block_t));
#endif
        block->id.mptr = block_ptr.mptr;
    }

    d_stat_block_init (imdb);
    block->block_index = bidx;

    imdb_block_slot_init (imdb, class_block->dbclass.ds_type, page_block, block);
    d_imdb_page_fl_insert_block (page_block, block);

#ifdef IMDB_BLOCK_CRC
#else
    block->crc16 = IMDB_BLOCK_CRC_DEFAULT;
#endif

    return block;
}

LOCAL imdb_block_page_t *ICACHE_FLASH_ATTR
imdb_internal_page_alloc (imdb_t * imdb, imdb_class_def_t * cdef, bool fclass)
{
    size_t          psize = cdef->page_blocks * imdb->db_def.block_size;

    imdb_block_page_t *page_block = NULL;
    if (imdb->db_def.opt_media) {
        imdb_file_t     hdr_file;
        imdb->stat.header_read++;
        if (fdb_header_read (&hdr_file) || (hdr_file.file_size - hdr_file.file_hwm <= cdef->page_blocks))
            return NULL;

        size_t          block_addr = (hdr_file.file_hwm + 1) * imdb->db_def.block_size;
        hdr_file.file_hwm += cdef->page_blocks;
        if (fclass)
            hdr_file.class_last = block_addr;

        imdb->stat.header_write++;
        if (fdb_header_write (&hdr_file))
            return NULL;

        page_block =
            d_pointer_as (imdb_block_page_t,
                          fdb_cache_get (d_pointer_as (imdb_bc_t, imdb), block_addr, true, DATA_LOCK_WRITE));
        d_log_dprintf (IMDB_SERVICE_NAME, "page_alloc: id=%p, page=%p, size=%u", block_addr, page_block, psize);
    }
    else {
        page_block = (imdb_block_page_t *) os_malloc (psize);
        if (page_block) {
#ifdef IMDB_ZERO_MEM
            os_memset (page_block, 0, imdb->db_def.block_size);
#else
            os_memset (page_block, 0, sizeof (imdb_block_page_t));
#endif
            page_block->block.id.mptr = d_pointer_as (imdb_block_t, page_block);
            d_stat_alloc (imdb, psize);
        }
        d_log_dprintf (IMDB_SERVICE_NAME, "page_alloc: page=%p, size=%u", page_block, psize);
    }

    if (page_block) {
        d_stat_page_alloc (imdb);
        d_stat_block_alloc (imdb, cdef->page_blocks);
    }
    return page_block;
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
    imdb_block_class_t *class_block = d_pointer_as (imdb_block_class_t, imdb_internal_page_alloc (imdb, cdef, true));
    if (!class_block)
        return NULL;
    imdb_class_t   *dbclass = &class_block->dbclass;
#ifndef IMDB_ZERO_MEM
    os_memset (dbclass, 0, sizeof (imdb_class_t));
#endif

    imdb_block_page_t *page_block = d_pointer_as (imdb_block_page_t, class_block);

    dbclass->page_count = 1;
    dbclass->page_last.raw = page_block->block.id.raw;
    if (!cdef->opt_variable) {
        dbclass->ds_type = (cdef->opt_tx_control || !cdef->opt_recycle) ? DATA_SLOT_TYPE_2 : DATA_SLOT_TYPE_1;
    }
    else {
        dbclass->ds_type = (cdef->opt_tx_control || !cdef->opt_recycle) ? DATA_SLOT_TYPE_4 : DATA_SLOT_TYPE_3;
    }
    os_memcpy (&dbclass->cdef, cdef, sizeof (imdb_class_def_t));

    imdb_page_init (imdb, class_block, page_block, BLOCK_TYPE_CLASS);
    d_imdb_class_fl_insert_page (class_block, page_block);

    return class_block;
}

/*
[private] Allocate next (not initial) page for Class storage.
  - dbclass: Class pointer
  - result: Page block pointer
*/
LOCAL imdb_block_page_t *ICACHE_FLASH_ATTR
imdb_page_alloc (imdb_t * imdb, imdb_block_class_t * class_block)
{
    imdb_block_page_t *page_block =
        d_pointer_as (imdb_block_page_t, imdb_internal_page_alloc (imdb, &class_block->dbclass.cdef, false));
    if (!page_block)
        return NULL;

    imdb_page_init (imdb, class_block, page_block, BLOCK_TYPE_PAGE);
    d_imdb_class_fl_insert_page (class_block, page_block);

    imdb_page_t    *page = &page_block->page;
    class_block->dbclass.page_count++;

    imdb_block_page_t *last_page = d_acquire_page_block (imdb, class_block->dbclass.page_last, DATA_LOCK_WRITE);
    if (!last_page) {
        d_log_eprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_BLOCK_ACCESS], class_block->dbclass.page_last.raw);
        return NULL;
    }

    last_page->page.page_next.raw = page_block->block.id.raw;
    page->page_prev.raw = last_page->block.id.raw;
    d_release_page_block (imdb, last_page);

    d_setwrite_block (imdb, class_block);
    class_block->dbclass.page_last.raw = page_block->block.id.raw;

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
imdb_class_instance_alloc (imdb_t * imdb, imdb_block_class_t * class_block, void **ptr, size_t length)
{
    imdb_class_t   *dbclass = &class_block->dbclass;
    imdb_class_def_t *cdef = &(dbclass->cdef);

    obj_size_t      extra_bsize = data_slot_type_bsize[dbclass->ds_type];
    obj_size_t      slot_bsize;
    if (cdef->opt_variable) {
        slot_bsize = d_size_bptr_align (length);
        if (slot_bsize > imdb->obj_bsize_max) {
            return IMDB_INVALID_OBJSIZE;
        }
    }
    else {
        slot_bsize = dbclass->obj_bsize_min;
    }
    slot_bsize += extra_bsize;

    d_log_dprintf (IMDB_SERVICE_NAME, "alloc: class=%p searching free slot [fl=%u,el=%u]", class_block, slot_bsize,
                   extra_bsize);

    imdb_free_slot_find_ctx_t find_ctx;
    os_memset (&find_ctx, 0, sizeof (imdb_free_slot_find_ctx_t));

    if (dbclass->cdef.opt_recycle) {
        d_imdb_check_error (imdb_slot_free_get_or_recycle (imdb, class_block, slot_bsize, &find_ctx));
    }
    else {
        d_imdb_check_error (imdb_slot_free_find (imdb, class_block, slot_bsize, &find_ctx));
    }

    d_log_dprintf (IMDB_SERVICE_NAME, "alloc: free slot found - page=%p, block=%p, slot=%p, len=%u",
                   find_ctx.page_block, find_ctx.block, find_ctx.slot_free, find_ctx.slot_free->length);

    imdb_slot_free_extract (imdb, class_block, &find_ctx, slot_bsize, extra_bsize);
    d_log_dprintf (IMDB_SERVICE_NAME, "alloc: data slot=%p, rid=%p:%u:%u", find_ctx.slot_free,
                   find_ctx.page_block->block.id.raw, find_ctx.block->block_index, d_pointer_diff (find_ctx.slot_free,
                                                                                                   find_ctx.block));
    d_release_page_block (imdb, find_ctx.page_block);

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

    d_release_block (imdb, find_ctx.block);

    return IMDB_ERR_SUCCESS;
}

/*
[public] Initialize imdb instance
  - hndlr: result handler to imdb instance
  - hcurmdb: handler
  - result: imdb error code
*/
imdb_errcode_t  ICACHE_FLASH_ATTR
imdb_init (imdb_def_t * imdb_def, imdb_hndlr_t * himdb)
{
    imdb_t         *imdb;

    if (imdb_def->opt_media) {
        uint8           bucket_size = MAX (MIN (256, imdb_def->buffer_size << 2), 8);
        size_t          bcmap_size =
            d_hash8_fixedmap_size (sizeof (size_t), sizeof (imdb_bc_block_t), bucket_size, imdb_def->buffer_size);
        imdb_bc_t      *imdb_bc;
        imdb_bc = os_malloc (sizeof (imdb_bc_t) + bcmap_size);
        os_memset (imdb_bc, 0, sizeof (imdb_bc_t));
        imdb_bc->bcmap_size = bcmap_size;
        // hash-map block_id->bc_addr
        ih_init8 (imdb_bc->bcmap, bcmap_size, bucket_size, sizeof (size_t), sizeof (imdb_bc_block_t), &imdb_bc->hbcmap);
        // create buffer cache
        imdb_bc->buffer_cache = os_malloc (imdb_def->buffer_size * imdb_def->block_size);
        imdb_bc->bc_free_list = d_pointer_as (imdb_bc_free_block_t, imdb_bc->buffer_cache);

        {                       // make free list
            imdb_bc_free_block_t *fl = imdb_bc->bc_free_list;
            imdb_bc_free_block_t *max_ptr =
                d_pointer_add (imdb_bc_free_block_t, fl, imdb_def->buffer_size * imdb_def->block_size);
            while (true) {
                imdb_bc_free_block_t *flnext = d_pointer_add (imdb_bc_free_block_t, fl, imdb_def->block_size);
                if (flnext >= max_ptr) {
                    fl->fl_next_block = NULL;
                    break;
                }
                fl->fl_next_block = flnext;
                fl = flnext;
            }
        }

        d_log_wprintf (IMDB_SERVICE_NAME, "buffers:%p, len:%u, maplen:%u", imdb_bc->buffer_cache,
                       imdb_def->buffer_size * imdb_def->block_size, bcmap_size);

        imdb = &imdb_bc->base;
        d_stat_alloc (imdb, sizeof (imdb_bc_t) + bcmap_size + imdb_def->buffer_size * imdb_def->block_size);
    }
    else {
        st_zalloc (imdb, imdb_t);
        d_stat_alloc (imdb, sizeof (imdb_t));
    }

    if (!imdb_def->block_size) {
        imdb_def->block_size = IMDB_BLOCK_SIZE_DEFAULT;
    }
    else {
        imdb_def->block_size = MAX (IMDB_BLOCK_SIZE_MIN, d_size_align (imdb_def->block_size));
    }
    os_memcpy (&imdb->db_def, imdb_def, sizeof (imdb_def_t));
    imdb->obj_bsize_max = d_size_bptr (imdb->db_def.block_size - block_header_size[BLOCK_TYPE_CLASS]);
    imdb->class_first.raw = BLOCK_PTR_RAW_NONE;
    imdb->class_last.raw = BLOCK_PTR_RAW_NONE;

    d_log_dprintf (IMDB_SERVICE_NAME, "init: instance init %p (bsz=%u)", imdb, imdb->db_def.block_size);
    d_log_dprintf (IMDB_SERVICE_NAME,
                   "init: structures size:\n\timdb\t: %lu,\n\tclass\t: %lu,\n\tpage\t: %lu,\n\tblock\t: %lu,\n\tslot\t: %lu:%lu",
                   sizeof (imdb_t), sizeof (imdb_class_t), sizeof (imdb_page_t), sizeof (imdb_block_t),
                   sizeof (imdb_slot_free_t), sizeof (imdb_slot_footer_t));

    *himdb = d_obj2hndlr (imdb);

    if (imdb_def->opt_media) {
        // read header
        imdb_file_t     hdr_file;
        imdb->stat.header_read++;
        imdb_errcode_t  hres = fdb_header_read (&hdr_file);
        if ((hres != IMDB_ERR_SUCCESS) || (hdr_file.block_size != imdb_def->block_size)) {
            hdr_file.version = IMDB_FILE_HEADER_VERSION;
            hdr_file.block_size = imdb_def->block_size;
            hdr_file.class_last = BLOCK_PTR_RAW_NONE;
            hdr_file.file_size = MIN (imdb_def->file_size, fio_user_size () / imdb_def->block_size);
            hdr_file.file_hwm = 0;
            hdr_file.scn = 0;
            d_log_wprintf (IMDB_SERVICE_NAME, "create new file: %ubl", hdr_file.file_size);
            imdb->stat.header_write++;
            fdb_header_write (&hdr_file);
        }
        else {
            d_log_wprintf (IMDB_SERVICE_NAME, "read data file [SCN:%u,size:%ubl]", hdr_file.scn, hdr_file.file_size);
            imdb->class_first.fptr = (hdr_file.file_hwm) ? imdb_def->block_size : BLOCK_PTR_RAW_NONE;
            imdb->class_last.fptr = hdr_file.class_last;
        }
    }

    return IMDB_ERR_SUCCESS;
}

imdb_errcode_t  ICACHE_FLASH_ATTR
imdb_flush (imdb_hndlr_t hmdb)
{
    d_imdb_check_hndlr (hmdb);
    imdb_t         *imdb = d_hndlr2obj (imdb_t, hmdb);
    if (imdb->db_def.opt_media) {
        fdb_flush_data_t data;
        data.imdb_bc = d_pointer_as (imdb_bc_t, imdb);
        if (ih_hash8_forall (data.imdb_bc->hbcmap, fdb_forall_cache_flush, &data) != IH_ERR_SUCCESS)
            return IMDB_INTERNAL_ERROR;
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

    if (imdb->db_def.opt_media) {
        imdb_flush (hmdb);
        imdb_bc_t      *imdb_bc = d_pointer_as (imdb_bc_t, imdb);
        os_free (imdb_bc->buffer_cache);
        d_stat_free (imdb,
                     sizeof (imdb_bc_t) + imdb_bc->bcmap_size + imdb->db_def.buffer_size * imdb->db_def.block_size);
    }
    else {
        imdb_block_class_t *class_block = imdb->class_first.mptr;
        while (class_block) {
            imdb_hndlr_t    hclass = d_obj2hndlr (class_block);
            class_block = class_block->dbclass.class_next.mptr;
            imdb_class_destroy (hmdb, hclass);
        }
        d_stat_free (imdb, sizeof (imdb_t));
    }

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
    class_ptr_t     class_ptr;
    class_ptr.raw = imdb->class_first.raw;
    while (class_ptr.raw != BLOCK_PTR_RAW_NONE) {
        imdb_block_class_t *class_block = d_acquire_class_block (imdb, class_ptr);
        if (!class_block) {
            d_log_eprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_BLOCK_ACCESS], class_ptr.raw);
            return IMDB_BLOCK_ACCESS;
        }

        imdb_info->class_count++;
        if (array_len >= imdb_info->class_count) {
            imdb_class_info (hmdb, d_obj2hndlr (class_block->block.id.raw), &info_array[imdb_info->class_count - 1]);
        }
        d_release_class_block (imdb, class_block);

        class_ptr.raw = class_block->dbclass.class_next.raw;
    }

    return IMDB_ERR_SUCCESS;
}


/*
 * [public] Find class by name
 *  - hmdb: Handler to imdb instance
 *  - name: class name
 *  - hclass: Result Class instance handler
 *  - result: imdb error code
 */
imdb_errcode_t
imdb_class_find (imdb_hndlr_t hmdb, const char *name, imdb_hndlr_t * hclass)
{
    d_imdb_check_hndlr (hmdb);
    imdb_t         *imdb = d_hndlr2obj (imdb_t, hmdb);

    *hclass = 0;
    class_ptr_t     class_ptr;
    class_ptr.raw = imdb->class_first.raw;
    while (class_ptr.raw != BLOCK_PTR_RAW_NONE) {
        imdb_block_class_t *class_block = d_acquire_class_block (imdb, class_ptr);
        if (!class_block) {
            d_log_eprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_BLOCK_ACCESS], class_ptr.raw);
            return IMDB_BLOCK_ACCESS;
        }
        class_ptr.raw = class_block->dbclass.class_next.raw;
        int             cmpres = os_strncmp (class_block->dbclass.cdef.name, name, sizeof (class_name_t));
        d_release_class_block (imdb, class_block);

        if (cmpres == 0) {
            *hclass = d_obj2hndlr (class_block->block.id.raw);
            return IMDB_ERR_SUCCESS;
        }

    }

    return IMDB_ERR_SUCCESS;
}

/*
 * [public] Create class storage for object.
 *   - hmdb: Handler to imdb instance
 *   - cdef: Class definition
 *   - hclass: Result Class instance handler
 *   - result: imdb error code
 */
imdb_errcode_t  ICACHE_FLASH_ATTR
imdb_class_create (imdb_hndlr_t hmdb, imdb_class_def_t * cdef, imdb_hndlr_t * hclass)
{
    d_imdb_check_hndlr (hmdb);
    imdb_t         *imdb = d_hndlr2obj (imdb_t, hmdb);

    cdef->page_blocks = MAX (IMDB_FIRST_PAGE_BLOCKS_MIN, cdef->page_blocks);
    cdef->obj_size = d_size_align (cdef->obj_size);
    obj_size_t      obj_bsize_min = d_size_bptr (cdef->obj_size);
    if ((obj_bsize_min > imdb->obj_bsize_max) || (obj_bsize_min == 0 && !cdef->opt_variable)) {
        return IMDB_INVALID_OBJSIZE;
    }

    if (cdef->opt_recycle && cdef->page_blocks <= 2) {
        return IMDB_INVALID_RECYCLE_STORAGE;
    }

    if (cdef->pct_free > IMDB_PCT_FREE_MAX) {
        cdef->pct_free = IMDB_PCT_FREE_MAX;
    }

    imdb_block_class_t *class_block = imdb_class_page_alloc (imdb, cdef);
    if (!class_block) {
        d_log_eprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_NOMEM]);
        return IMDB_NOMEM;
    }
    size_t          rawid = class_block->block.id.raw;

    class_block->dbclass.class_next.raw = BLOCK_PTR_RAW_NONE;
    class_block->dbclass.class_prev.raw = BLOCK_PTR_RAW_NONE;
    class_block->dbclass.obj_bsize_min = obj_bsize_min;

    if (imdb->class_last.raw != BLOCK_PTR_RAW_NONE) {
        class_block->dbclass.class_prev.raw = imdb->class_last.raw;

        imdb_block_class_t *class_last = d_acquire_class_block (imdb, imdb->class_last);
        if (!class_last) {
            d_log_cprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_BLOCK_ACCESS], imdb->class_last.raw);
            return IMDB_BLOCK_ACCESS;
        }

        class_last->dbclass.class_next.raw = rawid;
        d_release_class_block (imdb, class_last);
    }
    else
        imdb->class_first.raw = rawid;
    imdb->class_last.raw = rawid;

    d_release_class_block (imdb, class_block);
    *hclass = d_obj2hndlr (rawid);
    d_log_iprintf (IMDB_SERVICE_NAME, "created \"%s\" %p (type=%u,pbs=%u,os=%u)", cdef->name, *hclass,
                   class_block->dbclass.ds_type, cdef->page_blocks, cdef->obj_size);

    return IMDB_ERR_SUCCESS;
}


/*
[public] Destroy class storage for object and deallocate memory.
  - hcls: handler to class instance
  - result: imdb error code
*/
imdb_errcode_t  ICACHE_FLASH_ATTR
imdb_class_destroy (imdb_hndlr_t hmdb, imdb_hndlr_t hclass)
{
    d_imdb_check_hndlr (hmdb);
    d_imdb_check_hndlr (hclass);

    imdb_t         *imdb = d_hndlr2obj (imdb_t, hmdb);
    class_ptr_t     class_ptr;
    class_ptr.raw = (size_t) hclass;

    imdb_block_class_t *class_block = d_acquire_class_block (imdb, class_ptr);
    if (!class_block) {
        d_log_eprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_BLOCK_ACCESS], class_ptr.raw);
        return IMDB_BLOCK_ACCESS;
    }

    imdb_class_t   *dbclass = &class_block->dbclass;

    if (dbclass->class_prev.raw != BLOCK_PTR_RAW_NONE) {
        imdb_block_class_t *class_prev = d_acquire_class_block (imdb, dbclass->class_prev);
        if (!class_prev) {
            d_log_cprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_BLOCK_ACCESS], dbclass->class_prev.raw);
            return IMDB_BLOCK_ACCESS;
        }
        class_prev->dbclass.class_next.raw = dbclass->class_next.raw;
        d_release_class_block (imdb, class_prev);
    }
    else {
        imdb->class_first.raw = dbclass->class_next.raw;
    }
    if (dbclass->class_next.raw != BLOCK_PTR_RAW_NONE) {
        imdb_block_class_t *class_next = d_acquire_class_block (imdb, dbclass->class_next);
        if (!class_next) {
            d_log_cprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_BLOCK_ACCESS], dbclass->class_next.raw);
            return IMDB_BLOCK_ACCESS;
        }
        class_next->dbclass.class_prev.raw = dbclass->class_prev.raw;
        d_release_class_block (imdb, class_next);
    }

    class_name_t    cname;
    os_memcpy (cname, dbclass->cdef.name, sizeof (class_name_t));
    class_pages_t   pcnt = 0;
    uint32          bcnt = 0;
    // iterate page
    if (imdb->db_def.opt_media) {
        d_release_class_block (imdb, class_block);
        // Fixme: TODO: Must use freelist for mdeia storage
    }
    else {
        imdb_block_page_t *page_targ = d_pointer_as (imdb_block_page_t, class_block);
        while (page_targ) {
            void           *ptr = page_targ;
            d_stat_page_free (imdb);
            d_stat_free (imdb, dbclass->cdef.page_blocks * imdb->db_def.block_size);
            pcnt++;
            bcnt += dbclass->cdef.page_blocks;
            page_targ = page_targ->page.page_next.mptr;
            os_free (ptr);
        }
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
imdb_clsobj_insert (imdb_hndlr_t hmdb, imdb_hndlr_t hclass, void **ptr, size_t length)
{
    d_imdb_check_hndlr (hmdb);
    d_imdb_check_hndlr (hclass);

    imdb_t         *imdb = d_hndlr2obj (imdb_t, hmdb);
    block_ptr_t     block_ptr;
    block_ptr.raw = (size_t) hclass;
    imdb_block_class_t *class_block = d_acquire_class_block (imdb, block_ptr);
    if (!class_block) {
        d_log_eprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_BLOCK_ACCESS], block_ptr.raw);
        return IMDB_BLOCK_ACCESS;
    }

    imdb_errcode_t  res = imdb_class_instance_alloc (imdb, class_block, ptr, length);
    d_release_class_block (imdb, class_block);

    return res;
}

/*
 * [public] write lock object block, need manual unlock.
 *   - hmdb: Handler to imdb instance
 *   - rowid: object row id
 *   - ptr: pointer to deleting object
 *   - result: imdb error code
 */
imdb_errcode_t  ICACHE_FLASH_ATTR
imdb_clsobj_update_init (imdb_hndlr_t hmdb, imdb_rowid_t * rowid, void **ptr)
{
    *ptr = NULL;
    d_imdb_check_hndlr (hmdb);
    imdb_t         *imdb = d_hndlr2obj (imdb_t, hmdb);
    if (imdb->db_def.opt_media) {
        imdb_block_t   *block = fdb_cache_get ((imdb_bc_t *) (imdb), rowid->block_id, false, DATA_LOCK_WRITE);
        if (!block) {
            d_log_eprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_BLOCK_ACCESS], rowid->block_id);
            return IMDB_BLOCK_ACCESS;
        }

        *ptr = d_pointer_add (void, block, d_bptr_size (rowid->slot_offset) +
                              (rowid->ds_type == DATA_SLOT_TYPE_1) ? 0 : sizeof (imdb_slot_free_t));
    }

    return IMDB_ERR_SUCCESS;
}

/*
 * [public] write lock and unlock object block.
 *   - hmdb: Handler to imdb instance
 *   - rowid: object row id
 *   - ptr: pointer to deleting object
 *   - result: imdb error code
 */
imdb_errcode_t  ICACHE_FLASH_ATTR
imdb_clsobj_update (imdb_hndlr_t hmdb, imdb_rowid_t * rowid, void **ptr)
{
    *ptr = NULL;
    d_imdb_check_hndlr (hmdb);
    imdb_t         *imdb = d_hndlr2obj (imdb_t, hmdb);
    if (imdb->db_def.opt_media) {
        imdb_block_t   *block = fdb_cache_get ((imdb_bc_t *) (imdb), rowid->block_id, false, DATA_LOCK_WRITE);
        if (!block) {
            d_log_eprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_BLOCK_ACCESS], rowid->block_id);
            return IMDB_BLOCK_ACCESS;
        }
        block->lock_flag = DATA_LOCK_NONE;

        *ptr = d_pointer_add (void, block, d_bptr_size (rowid->slot_offset) +
                              ((rowid->ds_type == DATA_SLOT_TYPE_1) ? 0 : sizeof (imdb_slot_free_t)));
    }

    return IMDB_ERR_SUCCESS;
}


/*
 * [public] delete object from storage.
 *   - hmdb: Handler to imdb instance
 *   - hclass: handler to class instance
 *   - ptr: pointer to deleting object
 *   - result: imdb error code
 */
imdb_errcode_t  ICACHE_FLASH_ATTR
imdb_clsobj_delete (imdb_hndlr_t hmdb, imdb_hndlr_t hclass, void *ptr)
{
    d_imdb_check_hndlr (hmdb);
    d_imdb_check_hndlr (hclass);

    imdb_t         *imdb = d_hndlr2obj (imdb_t, hmdb);
    class_ptr_t     class_ptr;
    class_ptr.raw = (size_t) hclass;

    imdb_block_class_t *class_block = d_acquire_class_block (imdb, class_ptr);
    if (!class_block) {
        d_log_eprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_BLOCK_ACCESS], class_ptr.raw);
        return IMDB_BLOCK_ACCESS;
    }

    imdb_class_t   *dbclass = &class_block->dbclass;

    imdb_slot_free_t *slot_free = NULL;
    imdb_slot_footer_t *slot_footer = NULL;
    imdb_block_t   *block = NULL;

    obj_size_t      slen = 0;
    switch (dbclass->ds_type) {
    case DATA_SLOT_TYPE_1:
    case DATA_SLOT_TYPE_3:
        return IMDB_INVALID_OPERATION;
        break;
    case DATA_SLOT_TYPE_2:
        {
            imdb_slot_data2_t *slot_data2 = d_pointer_add (imdb_slot_data2_t, ptr, -sizeof (imdb_slot_data2_t));
            d_assert (slot_data2->flags == SLOT_FLAG_DATA, "flags=%u", slot_data2->flags);
            block = d_pointer_add (imdb_block_t, slot_data2, -d_bptr_size (slot_data2->block_offset));
            slen = dbclass->cdef.obj_size + sizeof (imdb_slot_data2_t);
            slot_free = (imdb_slot_free_t *) slot_data2;
        }
        break;
    case DATA_SLOT_TYPE_4:
        {
            imdb_slot_data4_t *slot_data4 = d_pointer_add (imdb_slot_data4_t, ptr, -sizeof (imdb_slot_data4_t));
            d_assert (slot_data4->flags == SLOT_FLAG_DATA, "flags=%u", slot_data4->flags);
            slen = d_bptr_size (slot_data4->length);
            slot_footer = d_pointer_add (imdb_slot_footer_t, slot_data4, slen - sizeof (imdb_slot_footer_t));
            d_assert (slot_footer->flags == SLOT_FLAG_DATA, "flags=%u", slot_footer->flags);

            block = d_pointer_add (imdb_block_t, slot_data4, -d_bptr_size (slot_data4->block_offset));
            slot_free = (imdb_slot_free_t *) slot_data4;
        }
        break;
    default:
        d_assert (false, "ds_type=%u", dbclass->ds_type);
    }

    d_stat_slot_free (imdb);
    slot_free->flags = SLOT_FLAG_FREE;
    slot_free->length = d_size_bptr (slen);

    d_log_dprintf (IMDB_SERVICE_NAME, "delete: class=%p add free slot=%p, len=%u", class_block, slot_free,
                   slot_free->length);

    d_setwrite_block (imdb, block);
    d_imdb_block_fl_insert_slot (block, slot_free);
    if (!slot_free->next_offset) {      // block FL was empty        
        page_ptr_t      page_ptr;
        page_ptr.raw = d_block_get_page_blockid (block, imdb->db_def.block_size);

        imdb_block_page_t *page_block = d_acquire_page_block (imdb, page_ptr, DATA_LOCK_WRITE);
        if (!page_block) {
            d_log_cprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_BLOCK_ACCESS], page_ptr.raw);
            return IMDB_BLOCK_ACCESS;
        }

        d_imdb_page_fl_insert_block (page_block, block);
        if (!block->block_fl_next) {    // page FL was empty            
            d_setwrite_block (imdb, class_block);
            d_imdb_class_fl_insert_page (class_block, page_block);
        }
        d_release_block (imdb, &page_block->block);
    }

    if (slot_footer) {
        os_memset (slot_footer, 0, sizeof (imdb_slot_footer_t));
        slot_footer->flags = SLOT_FLAG_FREE;
        slot_footer->length = slot_free->length;
    }

    // !!! TODO: Coalesce adjoin Slots !!!

    d_release_class_block (imdb, class_block);

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
imdb_clsobj_resize (imdb_hndlr_t hmdb, imdb_hndlr_t hclass, void *ptr_old, void **ptr, size_t length)
{
    return IMDB_INVALID_OPERATION;
}

/*
[public] Return object length.
  - hclass: handler to class instance
  - ptr: pointer to deleting object
  - result: imdb error code
*/
imdb_errcode_t  ICACHE_FLASH_ATTR
imdb_clsobj_length (imdb_hndlr_t hmdb, imdb_hndlr_t hclass, void *ptr, size_t * length)
{
    d_imdb_check_hndlr (hmdb);
    d_imdb_check_hndlr (hclass);

    imdb_t         *imdb = d_hndlr2obj (imdb_t, hmdb);
    class_ptr_t     class_ptr;
    class_ptr.raw = (size_t) hclass;

    imdb_block_class_t *class_block = d_acquire_class_block (imdb, class_ptr);
    if (!class_block) {
        d_log_eprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_BLOCK_ACCESS], class_ptr.raw);
        return IMDB_BLOCK_ACCESS;
    }

    if (class_block->dbclass.cdef.opt_variable) {
        imdb_slot_data4_t *data_slot4 = d_pointer_add (imdb_slot_data4_t, ptr, -sizeof (imdb_slot_data4_t));
        d_assert (data_slot4->flags == SLOT_FLAG_DATA, "flags=%u", data_slot4->flags);
        *length = d_bptr_size (data_slot4->length);
    }
    else {
        *length = class_block->dbclass.cdef.obj_size;
    }

    d_release_class_block (imdb, class_block);

    return IMDB_ERR_SUCCESS;
}

/**
[public] Return Information about class storage.
  - hclass: handler to class instance
  - class_info: result pointer to class_info structure
  - result: imdb error code
*/
imdb_errcode_t  ICACHE_FLASH_ATTR
imdb_class_info (imdb_hndlr_t hmdb, imdb_hndlr_t hclass, imdb_class_info_t * class_info)
{
    d_imdb_check_hndlr (hmdb);
    d_imdb_check_hndlr (hclass);

    imdb_t         *imdb = d_hndlr2obj (imdb_t, hmdb);
    class_ptr_t     class_ptr;
    class_ptr.raw = (size_t) hclass;

    imdb_block_class_t *class_block = d_acquire_class_block (imdb, class_ptr);
    if (!class_block) {
        d_log_eprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_BLOCK_ACCESS], class_ptr.raw);
        return IMDB_BLOCK_ACCESS;
    }

    os_memset (class_info, 0, sizeof (imdb_class_info_t));
    class_info->hclass = hclass;
    os_memcpy (&class_info->cdef, &(class_block->dbclass.cdef), sizeof (imdb_class_def_t));

    imdb_block_page_t *page_targ = NULL;
    imdb_block_t   *block_targ = NULL;
    imdb_slot_free_t *slot_free = NULL;
    block_size_t    bsize = imdb->db_def.block_size;
    if (class_block->dbclass.page_fl_first.raw != BLOCK_PTR_RAW_NONE) {
        page_targ = d_acquire_page_block (imdb, class_block->dbclass.page_fl_first, DATA_LOCK_READ);
        if (!page_targ) {
            d_log_eprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_BLOCK_ACCESS], class_block->dbclass.page_fl_first.raw);
            return IMDB_BLOCK_ACCESS;
        }
        // iterate page
        while (page_targ) {
            page_blocks_t   bidx = page_targ->page.block_fl_first;

            class_info->blocks_free += class_block->dbclass.cdef.page_blocks - page_targ->page.alloc_hwm;
            // iterate block
            while (bidx) {
                block_ptr_t     block_ptr;
                block_ptr.raw = d_page_get_blockid_byidx (page_targ, bidx, bsize);
                block_targ = d_acquire_block (imdb, block_ptr, DATA_LOCK_READ);
                if (!block_targ) {
                    d_log_eprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_BLOCK_ACCESS], block_ptr.raw);
                    return IMDB_BLOCK_ACCESS;
                }

                slot_free = d_block_slot_free (block_targ);

                // iterate slot
                while (slot_free) {
                    class_info->slots_free++;
                    class_info->slots_free_size += d_bptr_size (slot_free->length);
                    if (d_dstype_slot_has_footer (class_block->dbclass.ds_type)) {
                        imdb_slot_footer_t *slot_footer = d_block_slot_free_footer (slot_free);
                        class_info->fl_skip_count += slot_footer->skip_count;
                    }
                    slot_free = d_block_next_slot_free (block_targ, slot_free);
                }
                bidx = block_targ->block_fl_next;

                d_release_block (imdb, block_targ);
            }

            d_release_page_block (imdb, page_targ);
            if (page_targ->page.page_fl_next.raw != BLOCK_PTR_RAW_NONE) {
                page_targ = d_acquire_page_block (imdb, page_targ->page.page_fl_next, DATA_LOCK_READ);
                if (!page_targ) {
                    d_log_eprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_BLOCK_ACCESS],
                                   page_targ->page.page_fl_next.raw);
                    return IMDB_BLOCK_ACCESS;
                }
            }
            else
                page_targ = NULL;
        }
        if (page_targ)
            d_release_page_block (imdb, page_targ);
    }

    class_info->pages = class_block->dbclass.page_count;
    class_info->blocks = class_info->pages * class_block->dbclass.cdef.page_blocks;

    d_release_class_block (imdb, class_block);

    return IMDB_ERR_SUCCESS;
}

/*
[private] Prepare cursor for specified access-path.
  - dbclass: pointer to class instance
  - cur: pointer to cursor
  - access_path: access path (FULL_SCAN, RECYCLE_SCAN, RECYCLE_SCAN_REW, etc.)
  - result: imdb error code
*/
LOCAL imdb_errcode_t ICACHE_FLASH_ATTR
imdb_class_cur_open (imdb_t * imdb, imdb_block_class_t * class_block, imdb_cursor_t * cur,
                     imdb_access_path_t access_path)
{
    os_memset (cur, 0, sizeof (imdb_cursor_t));
    cur->access_path = access_path;
    cur->imdb = imdb;
    cur->class.raw = class_block->block.id.raw;

    cur->rowid_last.ds_type = class_block->dbclass.ds_type;

    switch (access_path) {
    case PATH_FULL_SCAN:
        cur->rowid_last.block_id = class_block->block.id.raw;
        break;
    case PATH_RECYCLE_SCAN:
        // TODO: Not Finished
        return IMDB_CURSOR_INVALID_PATH;
    case PATH_RECYCLE_SCAN_REW:
        {
            imdb_block_page_t *page_block =
                d_acquire_page_block (imdb, class_block->dbclass.page_fl_first, DATA_LOCK_READ);
            if (!page_block) {
                d_log_eprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_BLOCK_ACCESS],
                               class_block->dbclass.page_fl_first.raw);
                return IMDB_BLOCK_ACCESS;
            }
            if (page_block->page.block_fl_first == 1) {
                cur->rowid_last.block_id = page_block->block.id.raw;
                cur->rowid_last.slot_offset = page_block->block.free_offset;
                d_release_page_block (imdb, page_block);
            }
            else {
                d_release_page_block (imdb, page_block);
                cur->rowid_last.block_id =
                    d_page_get_blockid_byidx (page_block, page_block->page.block_fl_first, imdb->db_def.block_size);

                block_ptr_t     block_ptr;
                block_ptr.raw = cur->rowid_last.block_id;
                imdb_block_t   *block = d_acquire_block (imdb, block_ptr, DATA_LOCK_READ);
                if (!block) {
                    d_log_eprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_BLOCK_ACCESS], cur->rowid_last.block_id);
                    return IMDB_BLOCK_ACCESS;
                }
                cur->rowid_last.slot_offset = block->free_offset;
                d_release_block (imdb, block);
            }
        }
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
LOCAL imdb_errcode_t ICACHE_FLASH_ATTR
imdb_class_cur_fetch (imdb_block_class_t * class_block, imdb_cursor_t * cur, uint16 count, uint16 * rowcount,
                      imdb_fetch_obj_t fobj[])
{
    if (!cur->rowid_last.block_id) {
        return IMDB_CURSOR_NO_DATA_FOUND;
    }

    block_ptr_t     block_ptr;
    block_ptr.raw = cur->rowid_last.block_id;

    imdb_block_t   *block = d_acquire_block (cur->imdb, block_ptr, DATA_LOCK_READ);
    if (!block) {
        d_log_eprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_BLOCK_ACCESS], block_ptr.raw);
        return IMDB_BLOCK_ACCESS;
    }

    imdb_block_page_t *page_block;
    if (block->btype == BLOCK_TYPE_PAGE) {
        page_block = d_pointer_as (imdb_block_page_t, block);
    }
    else {
        block_ptr_t     page_ptr;
        page_ptr.raw = d_block_get_page_blockid (block, cur->imdb->db_def.block_size);
        page_block = d_acquire_page_block (cur->imdb, page_ptr, DATA_LOCK_READ);
        if (!page_block) {
            d_log_eprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_BLOCK_ACCESS], page_ptr.raw);
            return IMDB_BLOCK_ACCESS;
        }
    }
    block_size_t    offset = cur->rowid_last.slot_offset;

    bool            foneblock = cur->imdb->db_def.opt_media;    // for safe results access
    block_size_t    offset_limit;

    imdb_fetch_obj_t *pfobj = &fobj[0];
    switch (cur->access_path) {
    case PATH_FULL_SCAN:
        while (page_block) {
            while (block) {
                if (offset == 0) {
                    offset = d_block_lower_data_blimit (block);
                }
                offset_limit = d_block_upper_data_blimit (cur->imdb, block);
                while (offset < offset_limit) {
                    pfobj->dataptr = NULL;
                    while (!pfobj->dataptr && offset < offset_limit) {
                        pfobj->rowid.slot_offset = offset;
                        imdb_block_slot_next (cur->imdb, class_block, block, &offset, &pfobj->dataptr);
                    }
                    if (!pfobj->dataptr)
                        break;

                    pfobj->rowid.block_id = block->id.raw;
                    pfobj->rowid.ds_type = class_block->dbclass.ds_type;

                    cur->rowid_last.slot_offset = offset;

                    pfobj++;
                    (*rowcount)++;
                    if (*rowcount == count) {
                        d_release_block (cur->imdb, block);
                        d_release_page_block (cur->imdb, page_block);
                        return IMDB_ERR_SUCCESS;
                    }
                }
                d_release_block (cur->imdb, block);
                if (foneblock && *rowcount) {
                    d_release_page_block (cur->imdb, page_block);
                    return IMDB_ERR_SUCCESS;
                }

                if (block->block_index < page_block->page.alloc_hwm) {
                    cur->rowid_last.block_id =
                        d_page_get_blockid_byidx (page_block, block->block_index + 1, cur->imdb->db_def.block_size);
                    block_ptr.raw = cur->rowid_last.block_id;
                    block = d_acquire_block (cur->imdb, block_ptr, DATA_LOCK_READ);
                    if (!block) {
                        d_log_eprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_BLOCK_ACCESS], block_ptr.raw);
                        return IMDB_BLOCK_ACCESS;
                    }
                }
                else {
                    cur->rowid_last.block_id = BLOCK_PTR_RAW_NONE;
                    block = NULL;
                }
                cur->rowid_last.slot_offset = offset = 0;
            }
            d_release_page_block (cur->imdb, page_block);

            if (page_block->page.page_next.raw == BLOCK_PTR_RAW_NONE) {
                return IMDB_CURSOR_NO_DATA_FOUND;
            }

            cur->rowid_last.block_id = page_block->page.page_next.raw;
            cur->rowid_last.slot_offset = offset = 0;
            page_block = d_acquire_page_block (cur->imdb, page_block->page.page_next, DATA_LOCK_READ);
            if (!page_block) {
                d_log_eprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_BLOCK_ACCESS], page_block->page.page_next.raw);
                return IMDB_BLOCK_ACCESS;
            }
            block = d_pointer_as (imdb_block_t, page_block);
        }
        break;
    case PATH_RECYCLE_SCAN:
        return IMDB_CURSOR_INVALID_PATH;
        break;
    case PATH_RECYCLE_SCAN_REW:
        while (page_block) {
            while (block) {
                if (offset == 0) {
                    if (block->free_offset) {
                        // because recycle whole block
                        return IMDB_CURSOR_NO_DATA_FOUND;
                    }
                    imdb_block_slot_data_last (cur->imdb, &class_block->dbclass, block, &offset);
                }
                offset_limit = d_block_lower_data_blimit (block);
                while (offset > offset_limit) {
                    pfobj->rowid.slot_offset = offset;
                    imdb_block_slot_prev (class_block, block, &offset, &pfobj->dataptr);
                    pfobj->rowid.block_id = block->id.raw;
                    pfobj->rowid.ds_type = class_block->dbclass.ds_type;

                    cur->rowid_last.slot_offset = offset;

                    pfobj++;
                    (*rowcount)++;

                    if (*rowcount == count) {
                        d_release_block (cur->imdb, block);
                        d_release_page_block (cur->imdb, page_block);
                        return IMDB_ERR_SUCCESS;
                    }
                }

                d_release_block (cur->imdb, block);
                if (foneblock && *rowcount) {
                    d_release_page_block (cur->imdb, page_block);
                    return IMDB_ERR_SUCCESS;
                }

                if (block->block_index > 2) {
                    cur->rowid_last.block_id =
                        d_page_get_blockid_byidx (page_block, block->block_index - 1, cur->imdb->db_def.block_size);
                    block_ptr.raw = cur->rowid_last.block_id;
                    block = d_acquire_block (cur->imdb, block_ptr, DATA_LOCK_READ);
                    if (!block) {
                        d_log_eprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_BLOCK_ACCESS], block_ptr.raw);
                        return IMDB_BLOCK_ACCESS;
                    }
                }
                else if (block->block_index == 2) {
                    block = d_pointer_as (imdb_block_t, page_block);
                    cur->rowid_last.block_id = block->id.raw;
                }
                else {
                    cur->rowid_last.block_id = BLOCK_PTR_RAW_NONE;
                    block = NULL;
                }
                cur->rowid_last.slot_offset = offset = 0;
            }

            d_release_page_block (cur->imdb, page_block);
            page_ptr_t      page_ptr;
            page_ptr.raw =
                (page_block->page.page_prev.raw ==
                 BLOCK_PTR_RAW_NONE) ? class_block->dbclass.page_last.raw : page_block->page.page_prev.raw;
            cur->rowid_last.block_id = page_ptr.raw;
            cur->rowid_last.slot_offset = offset = 0;
            page_block = d_acquire_page_block (cur->imdb, page_ptr, DATA_LOCK_READ);
            if (!page_block) {
                d_log_eprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_BLOCK_ACCESS], page_ptr.raw);
                return IMDB_BLOCK_ACCESS;
            }

            block_ptr.raw =
                d_page_get_blockid_byidx (page_block, page_block->page.alloc_hwm, cur->imdb->db_def.block_size);
            block = d_acquire_block (cur->imdb, block_ptr, DATA_LOCK_READ);
            if (!block) {
                d_log_eprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_BLOCK_ACCESS], block_ptr.raw);
                return IMDB_BLOCK_ACCESS;
            }
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
imdb_class_query (imdb_hndlr_t hmdb, imdb_hndlr_t hclass, imdb_access_path_t access_path, imdb_hndlr_t * hcur)
{
    d_imdb_check_hndlr (hmdb);
    d_imdb_check_hndlr (hclass);

    imdb_cursor_t  *cur;
    st_zalloc (cur, imdb_cursor_t);
    if (!cur)
        return IMDB_NOMEM;

    imdb_t         *imdb = d_hndlr2obj (imdb_t, hmdb);
    class_ptr_t     class_ptr;
    class_ptr.raw = (size_t) hclass;

    imdb_block_class_t *class_block = d_acquire_class_block (imdb, class_ptr);
    if (!class_block) {
        d_log_eprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_BLOCK_ACCESS], class_ptr.raw);
        return IMDB_BLOCK_ACCESS;
    }

    imdb_access_path_t access_path2 = access_path;
    if (access_path2 == PATH_NONE) {
        if (class_block->dbclass.cdef.opt_recycle) {
            access_path2 = PATH_RECYCLE_SCAN_REW;
        }
        else {
            access_path2 = PATH_FULL_SCAN;
        }
    }

    imdb_errcode_t  ret = imdb_class_cur_open (imdb, class_block, cur, access_path2);

    d_release_class_block (imdb, class_block);

    if (ret != IMDB_ERR_SUCCESS) {
        st_free (cur);
    }
    d_log_dprintf (IMDB_SERVICE_NAME, "query: %p open cursor %p, path=%u, res=%u", cur, hclass, access_path, ret);


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
imdb_class_fetch (imdb_hndlr_t hcur, uint16 count, uint16 * rowcount, imdb_fetch_obj_t fobj[])
{
    *rowcount = 0;
    d_imdb_check_hndlr (hcur);
    if (count == 0) {
        return IMDB_ERR_SUCCESS;
    }

    imdb_cursor_t  *cur = d_hndlr2obj (imdb_cursor_t, hcur);
    imdb_block_class_t *class_block = d_acquire_class_block (cur->imdb, cur->class);
    if (!class_block) {
        d_log_eprintf (IMDB_SERVICE_NAME, sz_imdb_error[IMDB_BLOCK_ACCESS], cur->class.raw);
        return IMDB_BLOCK_ACCESS;
    }

    imdb_errcode_t  ret = imdb_class_cur_fetch (class_block, cur, count, rowcount, fobj);
    d_release_class_block (cur->imdb, class_block);

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
    os_free (cur);

    return IMDB_ERR_SUCCESS;
}

#define IMDB_FORALL_FETCH_BULK_COUNT	10

imdb_errcode_t  ICACHE_FLASH_ATTR
imdb_cursor_forall (imdb_hndlr_t hcur, void *data, imdb_forall_func forall_func)
{
    d_imdb_check_hndlr (hcur);
    imdb_fetch_obj_t obj_array[IMDB_FORALL_FETCH_BULK_COUNT];
    imdb_errcode_t  ret = IMDB_ERR_SUCCESS;
    imdb_errcode_t  ret2 = IMDB_ERR_SUCCESS;
    uint16          rcnt;
    uint16          i;

    while (ret == IMDB_ERR_SUCCESS) {
        ret = imdb_class_fetch (hcur, IMDB_FORALL_FETCH_BULK_COUNT, &rcnt, obj_array);
        if (ret != IMDB_ERR_SUCCESS && ret != IMDB_CURSOR_NO_DATA_FOUND) {
            return ret;
        }
        for (i = 0; i < rcnt; i++) {
            ret2 = forall_func (&obj_array[i], data);
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
imdb_class_forall (imdb_hndlr_t hmdb, imdb_hndlr_t hclass, void *data, imdb_forall_func forall_func)
{
    imdb_hndlr_t    hcur;
    imdb_errcode_t  ret = imdb_class_query (hmdb, hclass, PATH_NONE, &hcur);
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
imdb_forall_count (imdb_fetch_obj_t * obj, void *data)
{
    uint32         *objcount = (uint32 *) data;
    (*objcount)++;
    return IMDB_ERR_SUCCESS;
}
