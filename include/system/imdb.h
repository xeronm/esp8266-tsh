/******************************************************************************
 * Copyright (c) 2015 by Denis Muratov <xeronm@gmail.com>. All rights reserved
 *
 * FileName: imdb.h
 *
 * Description: Light weight time-conversion utility
 *
 * API
 *
 * Modification history:
 *     2016/06/01, v1.0 create this file.
 *     2017/11/07, v1.1 added: recycle storage, query.
 *******************************************************************************/

#ifndef _IMDB_H_
#define _IMDB_H_ 1

#include "sysinit.h"

#undef IMDB_ZERO_MEM

typedef void   *imdb_hndlr_t;

typedef enum PACKED imdb_block_crc_s {
    BLOCK_CRC_NONE = 0,
    BLOCK_CRC_META = 1,
    BLOCK_CRC_WRITE = 2,
    BLOCK_CRC_ALL = 3
} imdb_block_crc_t;

typedef enum PACKED imdb_errcode_e {
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
} imdb_errcode_t;

typedef enum PACKED imdb_access_path_s {
    PATH_NONE = 0,
    PATH_FULL_SCAN = 1,
    PATH_RECYCLE_SCAN = 2,
    PATH_RECYCLE_SCAN_REW = 3,
} imdb_access_path_t;


#define IMDB_BLOCK_UNIT_ALIGN	2	// 4 byte aligment
#define IMDB_CLASS_NAME_LEN	16	//

typedef uint16  obj_size_t;	// aligned by IMDB_BLOCK_UNIT_ALIGN
typedef uint16  block_size_t;	// aligned by IMDB_BLOCK_UNIT_ALIGN

#ifdef IMDB_SMALL_RAM
typedef uint8   page_blocks_t;	// 
typedef uint8   class_pages_t;	// 
#else
typedef uint16  page_blocks_t;	// 
typedef uint32  class_pages_t;	// 
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
    uint32          page_alloc;
    uint32          page_free;
    uint32          block_alloc;
    uint32          block_init;
    uint32          block_recycle;
    uint32          slot_free;
    uint32          slot_data;
    uint32          slot_split;
    uint32          slot_coalesce;
    uint32          slot_skipscan;
} imdb_stat_t;

/*
 * imdb general storage definition
 *   - block_size: block size in bytes
 *   - block_crc: block CRC mode
 */
typedef struct imdb_def_s {
    block_size_t    block_size;
    imdb_block_crc_t block_crc;
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
    page_blocks_t   init_blocks;
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
    class_pages_t   page_id;	// page index
    page_blocks_t   block_id;	// block index in page
    uint8           reserved:2;	// reserved
    uint16          slot_offset:14;	// slot offset in block
} imdb_rowid_t;

imdb_errcode_t  imdb_init (imdb_def_t * imdb_def, imdb_hndlr_t * himdb);
imdb_errcode_t  imdb_done (imdb_hndlr_t hmdb);
imdb_errcode_t  imdb_info (imdb_hndlr_t hmdb, imdb_info_t * imdb_info, imdb_class_info_t info_array[], uint8 array_len);

imdb_errcode_t  imdb_class_create (imdb_hndlr_t hmdb, imdb_class_def_t * class_def, imdb_hndlr_t * hclass);
imdb_errcode_t  imdb_class_destroy (imdb_hndlr_t hclass);
imdb_errcode_t  imdb_class_info (imdb_hndlr_t hclass, imdb_class_info_t * class_info);

imdb_errcode_t  imdb_clsobj_insert (imdb_hndlr_t hclass, void **ptr, size_t length);
imdb_errcode_t  imdb_clsobj_delete (imdb_hndlr_t hclass, void *ptr);
imdb_errcode_t  imdb_clsobj_resize (imdb_hndlr_t hclass, void *ptr_old, void **ptr, size_t length);
imdb_errcode_t  imdb_clsobj_length (imdb_hndlr_t hclass, void *ptr, size_t * length);

imdb_errcode_t  imdb_class_query (imdb_hndlr_t hclass, imdb_access_path_t path, imdb_hndlr_t * hcur);
imdb_errcode_t  imdb_class_fetch (imdb_hndlr_t hcur, uint16 count, uint16 * rowcount, void *ptr[]);
imdb_errcode_t  imdb_class_close (imdb_hndlr_t hcur);

typedef         imdb_errcode_t (*imdb_forall_func) (void *ptr, void *data);
imdb_errcode_t  imdb_class_forall (imdb_hndlr_t hcur, void *data, imdb_forall_func forall_func);

// forall helpers
imdb_errcode_t  forall_count (void *ptr, void *data);


#define d_imdb_check_hndlr(hndlr) 	if (!(hndlr)) { return IMDB_INVALID_HNDLR; }

#endif /* _IMDB_H_ */
