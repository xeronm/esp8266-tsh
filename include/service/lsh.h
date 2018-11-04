/* 
 * ESP8266 Light-weight shell
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

#ifndef _LSH_H_
#define _LSH_H_ 1

#include "sysinit.h"
#include "system/services.h"

#define	LSH_SERVICE_ID		5
#define LSH_SERVICE_NAME	"lwsh"

#define SH_FUNC_NAME_LENGTH	12
#define SH_STATEMENT_NAME_LEN	30

typedef char    sh_stmt_name_t[SH_STATEMENT_NAME_LEN];
typedef void   *sh_hndlr_t;
typedef char    sh_func_name_t[SH_FUNC_NAME_LENGTH];

typedef uint8   arg_count_t;
typedef uint16  bytecode_size_t;

typedef enum PACKED sh_errcode_e {
    SH_ERR_SUCCESS = 0,
    SH_INTERNAL_ERROR = 1,
    SH_INVALID_HNDLR = 2,
    SH_FUNC_NOT_EXISTS = 3,
    SH_FUNC_EXISTS = 4,
    SH_PARSE_ERROR_NUMINV = 5,
    SH_PARSE_ERROR_STRINV = 6,
    SH_PARSE_ERROR_TOKENINV = 7,
    SH_PARSE_ERROR_CLOSING_BRACKET = 8,
    SH_PARSE_ERROR_OPERAND_MISS = 9,
    SH_PARSE_ERROR_OPERAND_UNEXPECT = 10,
    SH_PARSE_ERROR_OUTOFBUF = 11,
    SH_CODE_VARIABLE_EXIST = 12,
    SH_CODE_VARIABLE_UNDEF = 13,
    SH_EVAL_ERROR_INVFUNC = 14,
    SH_EVAL_ERROR_INVARGTYPE = 15,
    SH_ALLOCATION_ERROR = 16,
    SH_STMT_EXISTS = 17,
    SH_STMT_NOT_EXISTS = 18,
    SH_FUNC_ERROR = 19,
} sh_errcode_t;

typedef enum PACKED sh_msgtype_e {
    SH_MSGTYPE_STMT_ADD = 10,
    SH_MSGTYPE_STMT_REMOVE = 11,
    SH_MSGTYPE_STMT_RUN = 12,
    SH_MSGTYPE_STMT_DUMP = 13,
} sh_msgtype_t;

typedef enum PACKED sh_avp_code_e {
    SH_AVP_STATEMENT = 100,
    SH_AVP_STMT_OBJSIZE = 101,
    SH_AVP_STMT_NAME = 102,
    SH_AVP_STMT_TEXT = 103,
    SH_AVP_STMT_CODE = 104,
    SH_AVP_STMT_PARSE_TIME = 105,
    SH_AVP_STMT_ARGUMENTS = 106,
    SH_AVP_FUNCTION_NAME = 110,
} sh_avp_code_t;

typedef struct sh_eval_ctx_s {
    sh_hndlr_t      hstmt;
    bool            opt_only;
    uint16          depth;
    sh_errcode_t    errcode;
    char            errmsg[80];
} sh_eval_ctx_t;

typedef struct sh_stmt_info_s {
    sh_stmt_name_t  name;
    lt_time_t       parse_time;
    obj_size_t      length;
} sh_stmt_info_t;

typedef struct sh_bc_arg_s {
    union sh_bc_arg_u {
	bytecode_size_t dlength;
	void           *ptr;
	uint32          value;
    }               arg;
    _Alignas(uint32) char data[];
} sh_bc_arg_t;

typedef enum PACKED sh_bc_arg_type_e {
    SH_BC_ARG_NONE,
    SH_BC_ARG_INT,
    SH_BC_ARG_CHAR,
    SH_BC_ARG_FUNC,
    SH_BC_ARG_LOCAL,
    SH_BC_ARG_GLOBAL,
} sh_bc_arg_type_t;

sh_bc_arg_type_t sh_pop_bcarg_type(uint16 * mask, sh_bc_arg_t * bc_arg);

typedef void    (*sh_func_t) (sh_bc_arg_t * ret_arg, const arg_count_t arg_count, sh_bc_arg_type_t arg_type[], sh_bc_arg_t * bc_args[]);

/*
External function definition
  - service_id: owner service (default: 0)
  - func_name: the functon name (alias)
  - func_ptr: pointer to function
  - opt_determ: deterministic flag
  - opt_stmt: statement flag
*/
typedef struct sh_func_entry_s {
    service_ident_t service_id;
    bool            opt_determ:1;
    bool            opt_stmt:1;
    uint8           reserved:6;
    sh_func_name_t  func_name;
    union sh_func_u {
	sh_func_t       func;
	sh_hndlr_t      hstmt;
	void           *ptr;
    } func;
} sh_func_entry_t;

sh_errcode_t    sh_func_get (char *func_name, sh_func_entry_t ** entry);
sh_errcode_t    sh_func_register (sh_func_entry_t * func_entry);

sh_errcode_t    stmt_parse (char * szstr, char * stmt_name, sh_hndlr_t * hstmt);
sh_errcode_t    stmt_dump (const sh_hndlr_t hstmt, char *buf, size_t len, bool resolve_glob);
sh_errcode_t    stmt_info (const sh_hndlr_t hstmt, sh_stmt_info_t * info);
sh_errcode_t    stmt_eval (const sh_hndlr_t hstmt, sh_eval_ctx_t * ctx);
sh_errcode_t    stmt_free (const sh_hndlr_t hstmt);

sh_errcode_t    stmt_get (char * stmt_name, sh_hndlr_t * hstmt);
sh_errcode_t    stmt_get2 (sh_stmt_name_t * stmt_name, sh_hndlr_t * hstmt);

// used by services
svcs_errcode_t  lsh_service_install (void);
svcs_errcode_t  lsh_service_uninstall (void);
svcs_errcode_t  lsh_on_start (imdb_hndlr_t hmdb, imdb_hndlr_t hdata, dtlv_ctx_t * conf);
svcs_errcode_t  lsh_on_stop (void);


#define d_sh_check_error(ret) \
	{ \
		sh_errcode_t r = (ret); \
 		if (r) return r; \
	}

#define d_sh_check_hndlr(hndlr) 	if (!(hndlr)) { return IMDB_INVALID_HNDLR; }

#define d_sh_check_imdb_error(ret) \
	{ \
		imdb_errcode_t r = (ret); \
		switch (r) { \
			case IMDB_ERR_SUCCESS: \
			case IMDB_CURSOR_BREAK: \
				break; \
			case IMDB_NOMEM: \
			case IMDB_ALLOC_PAGES_MAX: \
				return SH_ALLOCATION_ERROR; \
			default: \
				return SH_INTERNAL_ERROR; \
		} \
	}

#endif // _LSH_H_
