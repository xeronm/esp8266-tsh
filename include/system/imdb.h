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


/*
 * TODO: Should make hash-map storage and replace some heap storage, for example: functions, configurations, etc.
 */

#ifndef _IMDB_H_
#define _IMDB_H_ 1

#include "sysinit.h"

#undef IMDB_ZERO_MEM

typedef void   *imdb_hndlr_t;

typedef enum imdb_block_crc_s {
    BLOCK_CRC_NONE = 0,  // validate CRC only for media I/O
    BLOCK_CRC_META = 1,  // validate CRC for media I/O and for Metadata RW operations
    BLOCK_CRC_ALL  = 2   // validate CRC for media I/O and for all RW operations, requires implicit object write call
} imdb_block_crc_t;

typedef enum imdb_errcode_e {
    IMDB_ERR_SUCCESS = 0,
    IMDB_INTERNAL_ERROR = 1,
    IMDB_NOMEM = 2,
    IMDB_INVALID_HNDLR = 3,
    IMDB_INVALID_OPERATION = 4,
    IMDB_ALLOC_PAGES_MAX = 5,
    IMDB_INVALID_OBJSIZE = 6,
    IMDB_INVALID_RECYCLE_STORAGE = 7,
    IMDB_CURSOR_INVALID_PATH = 8,
    IMDB_CURSOR_NO_DATA_FOUND = 9,
    IMDB_CURSOR_BREAK = 10,
    IMDB_CURSOR_FORALL_FUNC = 11,
    IMDB_FILE_READ_ERROR = 12,
    IMDB_FILE_WRITE_ERROR = 13,
    IMDB_FILE_CRC_ERROR = 14,
    IMDB_FILE_LOCK_ERROR = 15,
    IMDB_CACHE_CAPACITY = 16,
    IMDB_BLOCK_ACCESS = 17,
} imdb_errcode_t;

typedef enum imdb_access_path_s {
    PATH_NONE = 0,
    PATH_FULL_SCAN = 1,
    PATH_RECYCLE_SCAN = 2,
    PATH_RECYCLE_SCAN_REW = 3,
} imdb_access_path_t;


#define IMDB_BLOCK_UNIT_ALIGN	2	// 4 byte aligment
#define IMDB_CLASS_NAME_LEN	16	//
#define IMDB_BLOCK_CRC
#define IMDB_BLOCK_CRC_DEFAULT	0xFFFF

#define IMDB_FILE_HEADER_VERSION	1

typedef uint16  obj_size_t;	// aligned by IMDB_BLOCK_UNIT_ALIGN
typedef uint16  block_size_t;	// aligned by IMDB_BLOCK_UNIT_ALIGN

#ifdef IMDB_SMALL_RAM
typedef uint8   page_blocks_t;	// 
typedef uint8   class_pages_t;	// 
typedef uint16  stat_count_t;
#else
typedef uint16  page_blocks_t;	// 
typedef uint32  class_pages_t;	// 
typedef uint32  stat_count_t;
#endif

typedef char    class_name_t[IMDB_CLASS_NAME_LEN];

/*
 * imdb statistics definition
 *   - mem_alloc  : total allocated memory in bytes
 *   - mem_free   : total frees memory in bytes
 *   - page_alloc : total pages allocated
 *   - block_alloc: total blocks allocated
 *   - block_recycle : total recycles of data block
 *   - slot_free  : total initialization of free slots
 *   - slot_data  : total initialization of data slot 
 *   - slot_split : total split free slot on data + free
 *   - slot_coalesce: total coalesce 2 free slots
 *   - slot_skipscan: total slot skip skan for find suitable free slot
 */
typedef struct imdb_stat_s {
    size_t          mem_alloc;
    size_t          mem_free;
    stat_count_t    header_read;
    stat_count_t    header_write;
    stat_count_t    block_read;
    stat_count_t    block_write;
    stat_count_t    page_alloc;
    stat_count_t    page_free;
    stat_count_t    block_alloc;
    stat_count_t    block_init;
    stat_count_t    block_recycle;
    stat_count_t    slot_free;
    stat_count_t    slot_data;
    stat_count_t    slot_split;
    stat_count_t    slot_coalesce;
    stat_count_t    slot_skipscan;
} imdb_stat_t;

/*
 * imdb general storage definition
 *   - block_size: block size in bytes
 *   - block_crc: block CRC mode
 *   - opt_media: use media storage
 *   - buffer_size: buffer cache size in blocks when use media storage
 */
typedef struct imdb_def_s {
    block_size_t    block_size;
    imdb_block_crc_t block_crc: 7;
    bool            opt_media: 1;
    uint32          buffer_size; //
    uint32          file_size; // file size in blocks
    imdb_hndlr_t    hcur; // handler to cursor
} imdb_def_t;

/*
 * imdb info definition
 *   - stat:
 */
typedef struct imdb_info_s {
    imdb_def_t      db_def;
    imdb_stat_t     stat;
    uint32          class_count;
    size_t          size_class;
    size_t          size_page;
    size_t          size_block;
    size_t          size_rowid;
    size_t          size_cursor;
} imdb_info_t;


/*
  imdb class storage definition
    - name       : class name, used for pretty print
    - opt_recycle: recyclable storage
    - opt_variable: variable object size
    - opt_tx_control: transaction control suuport
    - pct_free   : block PCT free threshold, when used data reached this threshold block removed from freelist
    - pages_max  : maximum pages limit
    - init_blocks  : initial page size in blocks
    - page_blocks  : page size in blocks
    - obj_size   : object size in bytes, 0 - variable size
*/
typedef struct imdb_class_def_s {
    class_name_t    name;
    bool            opt_recycle:1;
    bool            opt_variable:1;
    bool            opt_tx_control:1;
    uint8           pct_free:5;
    class_pages_t   pages_max;
    //page_blocks_t   init_blocks;
    page_blocks_t   page_blocks;
    obj_size_t      obj_size;	// fixed part size
} imdb_class_def_t;


/**
  * @brief  imdb class info definition
  * @attribute  cdef   : 
  * @attribute  pages  :
  * @attribute  blocks :
  * @attribute  blocks_free:
  * @attribute  slots_free:
  * @attribute  slots_free_size:
*/
typedef struct imdb_class_info_s {
    imdb_hndlr_t    hclass;
    imdb_class_def_t cdef;
    class_pages_t   pages;
#ifdef IMDB_SMALL_RAM
    uint16          blocks;
    uint16          blocks_free;
    uint16          slots_free;
#else
    uint32          blocks;
    uint32          blocks_free;
    uint32          slots_free;
#endif
    size_t          slots_free_size;
    uint32          fl_skip_count;
} imdb_class_info_t;

typedef struct imdb_rowid_s {
    size_t   	    block_id;	// block rawid
    uint8           reserved:2;	// reserved
    uint16          slot_offset:14;	// slot offset in block
} imdb_rowid_t;

imdb_errcode_t  imdb_init (imdb_def_t * imdb_def, imdb_hndlr_t * himdb);
imdb_errcode_t  imdb_done (imdb_hndlr_t hmdb);
imdb_errcode_t  imdb_info (imdb_hndlr_t hmdb, imdb_info_t * imdb_info, imdb_class_info_t info_array[], uint8 array_len);
imdb_errcode_t  imdb_flush (imdb_hndlr_t hmdb);

imdb_errcode_t  imdb_class_find (imdb_hndlr_t hmdb, const char *name, imdb_hndlr_t * hclass);
imdb_errcode_t  imdb_class_create (imdb_hndlr_t hmdb, imdb_class_def_t * class_def, imdb_hndlr_t * hclass);
imdb_errcode_t  imdb_class_destroy (imdb_hndlr_t hmdb, imdb_hndlr_t hclass);
imdb_errcode_t  imdb_class_info (imdb_hndlr_t hmdb, imdb_hndlr_t hclass, imdb_class_info_t * class_info);

imdb_errcode_t  imdb_clsobj_insert (imdb_hndlr_t hmdb, imdb_hndlr_t hclass, void **ptr, size_t length);
imdb_errcode_t  imdb_clsobj_delete (imdb_hndlr_t hmdb, imdb_hndlr_t hclass, void *ptr);
imdb_errcode_t  imdb_clsobj_resize (imdb_hndlr_t hmdb, imdb_hndlr_t hclass, void *ptr_old, void **ptr, size_t length);
imdb_errcode_t  imdb_clsobj_length (imdb_hndlr_t hmdb, imdb_hndlr_t hclass, void *ptr, size_t * length);

imdb_errcode_t  imdb_class_query (imdb_hndlr_t hmdb, imdb_hndlr_t hclass, imdb_access_path_t path, imdb_hndlr_t * hcur);
imdb_errcode_t  imdb_class_fetch (imdb_hndlr_t hcur, uint16 count, uint16 * rowcount, void *ptr[]);
imdb_errcode_t  imdb_class_close (imdb_hndlr_t hcur);

typedef         imdb_errcode_t (*imdb_forall_func) (void *ptr, void *data);
imdb_errcode_t  imdb_class_forall (imdb_hndlr_t hmdb, imdb_hndlr_t hclass, void *data, imdb_forall_func forall_func);

// forall helpers
imdb_errcode_t  imdb_forall_count (void *ptr, void *data);


typedef struct imdb_file_s {
    uint16          version;
    uint16          crc16;
    uint32          scn;
    block_size_t    block_size;
    size_t          class_last;
    size_t          file_size;
    size_t          file_hwm;
} imdb_file_t;

imdb_errcode_t fdb_header_read (imdb_file_t * hdr_file);

#define d_imdb_check_hndlr(hndlr) 	if (!(hndlr)) { return IMDB_INVALID_HNDLR; }

#define d_imdb_check_error(ret) \
	{ \
		imdb_errcode_t r = (ret); \
		if (r != IMDB_ERR_SUCCESS) return r; \
	}

#endif /* _IMDB_H_ */
