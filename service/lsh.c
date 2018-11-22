/* 
 * ESP8266 Light-weight shell
 * Copyright (c) 2016-2018 Denis Muratov <xeronm@gmail.com>.
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
   Examples:
		## last_dt;
		## last_ev;
		# hum := dht11_get_humidity();
		# relay_state := gpioget(12);
		# sys_date := sysdate();
		((relay_state = 1) & (last_ev <= 0)) ?? 
			{ setctx(2, 3); setctx(1, sys_date) };
		((hum >= 6000) & (last_dt + 30 < sys_date)) ?? 
			{ gpioset(12, 1); last_ev := 1; last_dt := sys_date };
		((hum < 5800) & (last_dt + 30 < sys_date)) ?? 
			{ gpioset(12, 0); last_ev := 2; last_dt := sys_date };
		((last_ev = 1) & (last_dt + 600 < sys_date)) ?? 
			{ gpioset(12, 0); last_ev := 0; last_dt := sys_date };

	Bytecode:
		 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
		+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		|     OpCode    |    Length     |  *Arguments bc-Type Bit-Mask  |
		+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		|             Arguments Pointer Bit-Mask (optional)             |
		+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		|                           Arguments                           |
		+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		  * For variable declaration this field contains Data-Type
		     1-st bit: 0 - integer, 1 - extended
			 2-nd bit onliy for extended: 0 - inlined (constant or buffer*), 1 - pointer (virtual or absolute*)
			 * - depends from operator and argument position
*/

#include "sysinit.h"
#include "misc/idxhash.h"
#include "core/logging.h"
#include "core/system.h"
#include "core/ltime.h"
#include "proto/dtlv.h"
#include "system/comavp.h"
#include "system/imdb.h"
#include "service/lsh.h"

#define LSH_FUNC_STORAGE_PAGES			1
#define LSH_FUNC_STORAGE_PAGE_BLOCKS		1

#define LSH_STMT_STORAGE_PAGES			2
#define LSH_STMT_STORAGE_PAGE_BLOCKS		4

#define LSH_STMT_SRC_STORAGE_PAGES		8
#define LSH_STMT_SRC_STORAGE_PAGE_BLOCKS	1

#define LSH_TOKENIDX_BUFFER_SIZE		512
#define LSH_OPER_ARG_COUNT_MAX			14

#define LSH_IMDB_CLS_FUNC		"lsh$func"
#define LSH_IMDB_CLS_STMT		"lsh$stmt"
#define LSH_IMDB_CLS_STMT_SRC		"lsh$src"

#undef LSH_BUFFERS_IMDB

#ifdef IMDB_SMALL_RAM
#define LSH_STMT_PARSE_BUFFER_SIZE		512
#define LSH_STMT_BUFFER_SIZE			864
#define LSH_STMT_VARIDX_BUFFER_SIZE		512
#else
#define LSH_STMT_PARSE_BUFFER_SIZE		1024
#define LSH_STMT_BUFFER_SIZE			3936
#define LSH_STMT_VARIDX_BUFFER_SIZE		1024
#endif

#define d_obj2hndlr(obj)		(sh_hndlr_t) (obj)
#define d_hndlr2obj(type, hndlr)	(type *) (hndlr)	// TODO: сделать проверку на тип

#define	OD2T(opdesc)	(opdesc)->token, (opdesc)->term, (opdesc)->precedence
#define	OD2T_STR		"opdesc=[\"%s\",\"%s\",prio=%u]"

#define	OP2T(ctx, oper)	\
	(oper)->optype, ((oper)->stmt_start - (ctx)->stmt_start)
#define	OP2T_STR		"op=[type=%u,pos=%u]"

#define d_check_init()	    \
    if (! sdata) { \
        d_log_eprintf (LSH_SERVICE_NAME, sz_sh_error[SH_INTERNAL_ERROR]); \
        return SH_INTERNAL_ERROR; \
    } \

typedef uint16  parse_size_t;
typedef uint8   parse_depth_t;

LOCAL const char *sz_sh_error[] RODATA = {
    "",
    "internal error",
    "invalid handler",
    "function \"%s\" not exists",
    "function \"%s\" already exists (serivce_id:%u)",
    "invalid number at %u",
    "invalid string at %u",
    "invalid token at %u",
    "unclosed \"%s\", opened at %u",
    "%s operand missing",
    "uexpected %s operand",
    "out of buffer \"%s\", reqested %u free %u",
    "duplicate variable \"%s\" at %u",
    "unknown variable \"%s\" at %u",
    "invalid function for operator \"" OD2T_STR "\"",
    "invalid argument type %u",
    "memory allocation error: %u",
    "statement \"%s\" already exists",
    "statement \"%s\" not exists",
    "function \"%s\" call error",
};

typedef enum sh_oper_type_e {
    SH_OPER_NONE = 0,
    SH_OPER_FUNC = 1,
    SH_OPER_BLOCK = 2,
    SH_OPER_NOT = 3,
    SH_OPER_MULTIPLY = 4,
    SH_OPER_DIV = 5,
    SH_OPER_MOD = 6,
    SH_OPER_PLUS = 7,
    SH_OPER_MINUS = 8,
    SH_OPER_BIT_AND = 9,
    SH_OPER_BIT_NOT = 10,
    SH_OPER_BIT_SR = 11,
    SH_OPER_BIT_SL = 12,
    SH_OPER_BIT_OR = 13,
    SH_OPER_BIT_XOR = 14,
    SH_OPER_LT = 15,
    SH_OPER_GT = 16,
    SH_OPER_LTEQ = 17,
    SH_OPER_GTEQ = 18,
    SH_OPER_EQ = 19,
    SH_OPER_NOTEQ = 20,
    SH_OPER_AND = 21,
    SH_OPER_OR = 22,
    SH_OPER_ASSIGN = 23,
    SH_OPER_VAR = 24,
    SH_OPER_GVAR = 25,
    SH_OPER_IF = 26,
    SH_OPER_IFRET = 27,
    SH_OPER_FOREACH = 28,
    SH_OPER_ELSE = 29,
    SH_OPER_ARGLIST = 30,
    //
    SH_OPER_MAX = 31
} sh_oper_type_t;

typedef enum sh_operand_pos_s {
    SH_OPERAND_NONE = 0,
    SH_OPERAND_OPT,
    SH_OPERAND_MAND,
} sh_operand_pos_t;

typedef struct lsh_data_s {
    const svcs_resource_t * svcres;
    imdb_hndlr_t    hfunc;	// function storage
    imdb_hndlr_t    hstmt;	// parsed statement storage
    imdb_hndlr_t    hstmt_src;	// statement source storage
    char            token_idx[LSH_TOKENIDX_BUFFER_SIZE];	// hash map 
    // Fixme: should make separate index segment in imdb
} lsh_data_t;

LOCAL lsh_data_t *sdata = NULL;

typedef struct sh_stmt_s {
    sh_stmt_info_t  info;
    ALIGN_DATA char vardata[];
} sh_stmt_t;

typedef struct sh_func_find_ctx_s {
    const char      *func_name;
    sh_func_entry_t *entry;
} sh_func_find_ctx_t;

/*
[private]
*/
LOCAL imdb_errcode_t ICACHE_FLASH_ATTR
sh_forall_func_find (void *ptr, void *data)
{
    sh_func_entry_t *entry = d_pointer_as (sh_func_entry_t, ptr);
    sh_func_find_ctx_t *find_ctx = d_pointer_as (sh_func_find_ctx_t, data);
    if (os_strncmp (entry->func_name, find_ctx->func_name, sizeof (sh_func_name_t)) == 0) {
	find_ctx->entry = entry;
	return IMDB_CURSOR_BREAK;
    }
    return IMDB_ERR_SUCCESS;
}

/*
[public] look for the external function by name, that may used in lsh
  - func_name: the functon name (alias)
  - return: the pointer on function entry
*/
sh_errcode_t    ICACHE_FLASH_ATTR
sh_func_get (const char *func_name, sh_func_entry_t ** entry)
{
    d_check_init();

    sh_func_find_ctx_t find_ctx;
    os_memset (&find_ctx, 0, sizeof (sh_func_find_ctx_t));
    find_ctx.func_name = func_name;

    d_sh_check_imdb_error (imdb_class_forall (sdata->svcres->hmdb, sdata->hfunc, (void *) &find_ctx, sh_forall_func_find));

    *entry = find_ctx.entry;
    return (*entry) ? SH_ERR_SUCCESS : SH_FUNC_NOT_EXISTS;
}

/*
[public] register a function that may used in lsh
  - func_entry: external function definition
  - result: 
*/
sh_errcode_t    ICACHE_FLASH_ATTR
sh_func_register (sh_func_entry_t * func_entry)
{
    d_check_init();

    sh_func_entry_t *entry;

    sh_errcode_t res = sh_func_get (func_entry->func_name, &entry);
    if ((res == SH_ERR_SUCCESS) && (entry)) {
	d_log_wprintf (LSH_SERVICE_NAME, "register: function \"%s\" exists, service_id=%u", func_entry->func_name,
		       entry->service_id);
	return SH_FUNC_EXISTS;
    } 
    else if (res != SH_FUNC_NOT_EXISTS)
        return SH_INTERNAL_ERROR;

    d_sh_check_imdb_error (imdb_clsobj_insert (sdata->svcres->hmdb, sdata->hfunc, (void **) &entry, 0)
	);

    //os_memset(entry, 0, sizeof(sh_func_entry_t));
    os_memcpy (entry, func_entry, sizeof (sh_func_entry_t));
    //debug// os_printf("-- register: func=%s, ptr=%p\n", entry->func_name, entry->func.ptr);
    d_log_dprintf (LSH_SERVICE_NAME, "register: func=%s, ptr=%p\n", entry->func_name, entry->func.ptr);

    return SH_ERR_SUCCESS;
}

typedef         sh_errcode_t (*op_eval_t) (sh_eval_ctx_t * ctx);

typedef struct sh_oper_desc_s {
    uint8           precedence;
    bool            concat:1;	// concatenate the same operations in one statement
    bool            control:1;
    bool            result:1;
    sh_operand_pos_t opd_left:2;
    sh_operand_pos_t opd_right:2;
    char           *token;	// sign
    char           *term;	// terminator
} sh_oper_desc_t;

LOCAL sh_oper_desc_t sh_oper_desc[] RODATA = {
    {0, false, false, false, SH_OPERAND_NONE, SH_OPERAND_NONE, "\0", "\0"},	// 
    {1, false, false, true, SH_OPERAND_OPT, SH_OPERAND_OPT, "(", ")"},	// ... (...)    function call
    {2, false, true, false, SH_OPERAND_NONE, SH_OPERAND_MAND, "{", "}"},	// {...}        statement block
    {3, false, false, true, SH_OPERAND_NONE, SH_OPERAND_MAND, "!", "\0"},	// ! ...
    {4, true, false, true, SH_OPERAND_MAND, SH_OPERAND_MAND, "*", "\0"},	// ... * ...
    {4, true, false, true, SH_OPERAND_MAND, SH_OPERAND_MAND, "/", "\0"},	// ... / ...
    {4, true, false, true, SH_OPERAND_MAND, SH_OPERAND_MAND, "%", "\0"},	// ... % ...
    {5, true, false, true, SH_OPERAND_MAND, SH_OPERAND_MAND, "+", "\0"},	// ... + ...
    {5, true, false, true, SH_OPERAND_MAND, SH_OPERAND_MAND, "-", "\0"},	// ... - ...
    {6, true, false, true, SH_OPERAND_MAND, SH_OPERAND_MAND, "&", "\0"},	// ... BIT_AND ... 
    {6, false, false, true, SH_OPERAND_MAND, SH_OPERAND_MAND, "~", "\0"},	// ... BIT_NOT ... 
    {6, true, false, true, SH_OPERAND_MAND, SH_OPERAND_MAND, ">>", "\0"},	// ... BIT_SR ... 
    {6, true, false, true, SH_OPERAND_MAND, SH_OPERAND_MAND, "<<", "\0"},	// ... BIT_SL ... 
    {7, true, false, true, SH_OPERAND_MAND, SH_OPERAND_MAND, "|", "\0"},	// ... BIT_OR ... 
    {7, true, false, true, SH_OPERAND_MAND, SH_OPERAND_MAND, "^", "\0"},	// ... BIT_XOR ... 
    {8, false, false, true, SH_OPERAND_MAND, SH_OPERAND_MAND, "<", "\0"},	// ... < ...
    {8, false, false, true, SH_OPERAND_MAND, SH_OPERAND_MAND, ">", "\0"},	// ... > ...
    {8, false, false, true, SH_OPERAND_MAND, SH_OPERAND_MAND, "<=", "\0"},	// ... <= ...
    {8, false, false, true, SH_OPERAND_MAND, SH_OPERAND_MAND, ">=", "\0"},	// ... >= ...
    {9, false, false, true, SH_OPERAND_MAND, SH_OPERAND_MAND, "=", "\0"},	// ... = ... 
    {9, false, false, true, SH_OPERAND_MAND, SH_OPERAND_MAND, "!=", "\0"},	// ... != ... 
    {10, true, false, true, SH_OPERAND_MAND, SH_OPERAND_MAND, "&&", "\0"},	// ... AND ... 
    {11, true, false, true, SH_OPERAND_MAND, SH_OPERAND_MAND, "||", "\0"},	// ... OR ... 
    {14, false, false, false, SH_OPERAND_MAND, SH_OPERAND_MAND, ":=", "\0"},	// ... := ... 
    {0, false, false, false, SH_OPERAND_NONE, SH_OPERAND_MAND, "#", "\0"},	// VAR ...
    {0, false, false, false, SH_OPERAND_NONE, SH_OPERAND_MAND, "##", "\0"},	// GLOBAL VAR ...
    {12, false, true, false, SH_OPERAND_MAND, SH_OPERAND_MAND, "?", "\0"},	// ... IF ...
    {12, false, true, false, SH_OPERAND_MAND, SH_OPERAND_MAND, "??", "\0"},	// ... IFRES ...
    {12, false, true, false, SH_OPERAND_MAND, SH_OPERAND_MAND, "@", "\0"},	// ... FOREACH ...
    {13, false, true, false, SH_OPERAND_MAND, SH_OPERAND_MAND, ":", "\0"},	// ... ELSE ...
    {16, true, false, false, SH_OPERAND_MAND, SH_OPERAND_OPT, ",", "\0"},	// ... , ...
};

typedef enum sh_parse_assoc_type_e {
    SH_PARSE_ASSOC_TYPE_NONE = 0,
    SH_PARSE_ASSOC_TYPE_L2R,
    SH_PARSE_ASSOC_TYPE_CONCAT,
    SH_PARSE_ASSOC_TYPE_R2L,
} sh_parse_assoc_type_t;

#define ERROR_MESSAGE_LENGTH	80

typedef enum sh_parse_arg_type_e {
    SH_ARG_NONE,
    SH_ARG_INT,
    SH_ARG_CHAR,
    SH_ARG_FUNC,
    SH_ARG_TOKEN,
    SH_ARG_POINTER,
} sh_parse_arg_type_t;

typedef struct sh_parse_arg_s {
    sh_parse_arg_type_t type;
    parse_size_t    length;
    ALIGN_DATA char data[];
} sh_parse_arg_t;

/* used in parsing process */
typedef struct sh_parse_oper_s {
    const char     *stmt_start;
    bool            control:1;
    sh_oper_type_t  optype:7;
    char            term;
    arg_count_t     arg_count;
    sh_parse_arg_t *left_arg;
    struct sh_parse_oper_s *prev_oper;
    ALIGN_DATA char varargs[];
} sh_parse_oper_t;


typedef struct sh_parse_buffers_s {
    char            bc[LSH_STMT_BUFFER_SIZE];
    char            pbuf[LSH_STMT_PARSE_BUFFER_SIZE];
    char            varmap[LSH_STMT_VARIDX_BUFFER_SIZE];
} sh_parse_buffers_t;

/*
 * - stmt_start: start of current parsing statement
 * - parse_buf: temporary parse buffer
 * - bc_buf: bytecode buffer
 * - depth: tree depth
 * - line: current line
 * - pos: current position
 * - ret_state: return state
 * - errcode: result code
 * - errmsg: error text message
 */
typedef struct sh_parse_ctx_s {
    const char     *stmt_start;
    char           *parse_buf;
    char           *bc_buf;
    ih_hndlr_t      varmap;
    parse_depth_t   depth;
    sh_parse_oper_t *term_oper;
    sh_errcode_t    errcode;	//
    char            errmsg[ERROR_MESSAGE_LENGTH + 1];	//
} sh_parse_ctx_t;

/* used in bytecode */
typedef struct sh_bc_oper_s {
    sh_oper_type_t  optype:8;
    arg_count_t     arg_count;
    uint16          bitmask;
    ALIGN_DATA char varargs[];
} sh_bc_oper_t;

typedef struct sh_gvar_s {
    sh_bc_arg_type_t type;
    uint16          use_count;
    sh_bc_arg_t     arg;
} sh_gvar_t;

// check next operator char, used in parse_optype
#define d_optype_check_next(szstr, char, _EXPR_) \
		if (*(*(szstr) + 1) == (char)) { \
			(*(szstr))++; \
			_EXPR_; \
			break; \
		}

/*
[inline] parse operator token from string
  - szstr: current position in stmt
  - result: operator type
*/
INLINED sh_oper_type_t ICACHE_FLASH_ATTR
parse_optype (const char **szstr)
{
    sh_oper_type_t  optype = SH_OPER_NONE;
    switch (**szstr) {
    case '=':
	optype = SH_OPER_EQ;
	break;
    case '+':
	optype = SH_OPER_PLUS;
	break;
    case '-':
	optype = SH_OPER_MINUS;
	break;
    case '*':
	optype = SH_OPER_MULTIPLY;
	break;
    case '^':
	optype = SH_OPER_BIT_XOR;
	break;
    case '~':
	optype = SH_OPER_BIT_NOT;
	break;
    case '/':
	optype = SH_OPER_DIV;
	break;
    case '%':
	optype = SH_OPER_MOD;
	break;
    case '#':
	d_optype_check_next (szstr, '#', optype = SH_OPER_GVAR);
	optype = SH_OPER_VAR;
	break;
    case '>':
	d_optype_check_next (szstr, '=', optype = SH_OPER_GTEQ);
	d_optype_check_next (szstr, '>', optype = SH_OPER_BIT_SR);
	optype = SH_OPER_GT;
	break;
    case '<':
	d_optype_check_next (szstr, '=', optype = SH_OPER_LTEQ);
	d_optype_check_next (szstr, '<', optype = SH_OPER_BIT_SL);
	optype = SH_OPER_LT;
	break;
    case '!':
	d_optype_check_next (szstr, '=', optype = SH_OPER_NOTEQ);
	optype = SH_OPER_NOT;
	break;
    case '|':
	d_optype_check_next (szstr, '|', optype = SH_OPER_OR);
	optype = SH_OPER_BIT_OR;
	break;
    case '&':
	d_optype_check_next (szstr, '&', optype = SH_OPER_AND);
	optype = SH_OPER_BIT_AND;
	break;
    case '(':
	optype = SH_OPER_FUNC;
	break;
    case '{':
	optype = SH_OPER_BLOCK;
	break;
    case ',':
	optype = SH_OPER_ARGLIST;
	break;
    case '?':
	d_optype_check_next (szstr, '?', optype = SH_OPER_IFRET);
	optype = SH_OPER_IF;
	break;
    case ':':
	d_optype_check_next (szstr, '=', optype = SH_OPER_ASSIGN);
	optype = SH_OPER_ELSE;
	break;
    default:
	return SH_OPER_NONE;
    }

    (*szstr)++;
    return optype;
}

#define d_stmt_check_err(ctx) 	if ((ctx)->errcode != SH_ERR_SUCCESS) { return (ctx)->errcode; }
#define d_stmt_pos(ctx, szstr)	((szstr) - (ctx)->stmt_start)
#define d_stmt_err(ctx, ecode, ...) \
	{ \
		(ctx)->errcode = (ecode); \
		os_snprintf((ctx)->errmsg, ERROR_MESSAGE_LENGTH, sz_sh_error[(ctx)->errcode], ##__VA_ARGS__); \
	}

#define d_stmt_err_ret(ctx, ecode, ...) \
	{ \
		(ctx)->errcode = (ecode); \
		if ((ctx)->errcode != SH_ERR_SUCCESS) { \
			os_snprintf((ctx)->errmsg, ERROR_MESSAGE_LENGTH, sz_sh_error[(ctx)->errcode], ##__VA_ARGS__); \
			return (ctx)->errcode; \
		} \
	}
#define d_stmt_err_ret_ext(ctx, ecode, _RSTMT_, ...) \
	{ \
		(ctx)->errcode = (ecode); \
		if ((ctx)->errcode != SH_ERR_SUCCESS) { \
			os_snprintf((ctx)->errmsg, ERROR_MESSAGE_LENGTH, sz_sh_error[(ctx)->errcode], ##__VA_ARGS__); \
			return _RSTMT_; \
		} \
	}

#define d_bc_buffer_alloc(ctx, bc_ptr, length) \
	{ \
		bytecode_size_t blen = (length); \
		bytecode_size_t bfree = LSH_STMT_BUFFER_SIZE - d_pointer_diff(*(bc_ptr), (ctx)->bc_buf); \
		if (bfree < blen) { \
			d_stmt_err_ret((ctx), SH_PARSE_ERROR_OUTOFBUF, "bytecode", blen, bfree); \
		} \
		*(bc_ptr) += blen; \
	}

INLINED void    ICACHE_FLASH_ATTR
bc_set_pointer_arg (sh_parse_ctx_t * ctx, char **pbuf_ptr, sh_bc_oper_t * bc_oper, sh_parse_arg_t ** arg)
{
    sh_parse_arg_t *arg_ptr = *arg;
    arg_ptr->type = SH_ARG_POINTER;
    arg_ptr->length = sizeof (bytecode_size_t);
    bytecode_size_t *vptr = d_pointer_as (bytecode_size_t, &arg_ptr->data);
    *vptr = d_pointer_diff (bc_oper, ctx->bc_buf);
    *pbuf_ptr = d_pointer_add (char, arg_ptr, sizeof (sh_parse_arg_t) + d_align (arg_ptr->length));
    //debug// os_printf("-- 30 bc_set_pointer_arg: arg_addr=%p, vptr=%04x, bufptr=%p \n", arg_ptr, *vptr, *pbuf_ptr);
    d_log_dprintf (LSH_SERVICE_NAME, "bc_set_pointer_arg: arg_addr=%p, vptr=%04x, bufptr=%p", arg_ptr, *vptr,
		   *pbuf_ptr);
}

/*
* [private] add globals, variable or function
*  - arg: parsed argument
*  - type: type of globals
*  - gaddr: result pointer to globals
*/
LOCAL sh_errcode_t ICACHE_FLASH_ATTR
bc_global_add (sh_parse_arg_t * arg, sh_bc_arg_type_t type, sh_gvar_t ** gaddr)
{
    sh_gvar_t      *gvar;
    *gaddr = NULL;
    if (ih_hash8_search (sdata->token_idx, arg->data, arg->length - 1, (char **) &gvar) == IH_ENTRY_NOTFOUND) {
	ih_hash8_add (sdata->token_idx, arg->data, arg->length - 1, (char **) &gvar, 0);
	gvar->type = type;
	gvar->use_count = 1;

	switch (type) {
	case SH_ARG_FUNC:
	    gvar->arg.arg.ptr = 0;
	    break;
	default:
	    gvar->arg.arg.value = 0;
	}
	//debug// os_printf("-- bc_global_add: token=%s, addr=%p\n", arg->data, gvar);

	d_log_dprintf (LSH_SERVICE_NAME, "bc_global_add: token=%s, addr=%p", arg->data, gvar);
    }
    else
	gvar->use_count++;

    *gaddr = gvar;
    return SH_ERR_SUCCESS;
}


/*
* [private] Serialize argumet to bytecode
*  - bc_ptr: current pointer to bytecode buffer
*  - bc_oper: bytecode operator
*  - arg: parsed argument
*  - arg_idx: index of argument
*/
LOCAL sh_errcode_t ICACHE_FLASH_ATTR
bc_serialize_arg (sh_parse_ctx_t * ctx, char **bc_ptr, sh_bc_oper_t * bc_oper, sh_parse_arg_t * arg, uint8 * bytepos)
{
    sh_bc_arg_t    *bc_arg = d_pointer_as (sh_bc_arg_t, *bc_ptr);
    //debug// os_printf("-- serialize arg %p, %u\n", d_pointer_diff(bc_arg, ctx->bc_buf), arg->type);
    d_bc_buffer_alloc (ctx, bc_ptr, sizeof (sh_bc_arg_t));

    switch (arg->type) {
    case SH_ARG_INT:
	bc_arg->arg.value = *d_pointer_as (uint32, arg->data);
	// bytemask = 0
	break;
    case SH_ARG_TOKEN:
	{
	    bytecode_size_t *bptr;
	    if (ih_hash8_search (ctx->varmap, arg->data, arg->length - 1, (char **) &bptr) != IH_ERR_SUCCESS) {
		d_stmt_err_ret (ctx, SH_CODE_VARIABLE_UNDEF, arg->data, 0);
	    }
	    bc_arg->arg.vptr = *bptr;
	    bc_oper->bitmask |= (0x3 << *bytepos);
	    (*bytepos)++;
	    break;
	}
    case SH_ARG_CHAR:
	d_bc_buffer_alloc (ctx, bc_ptr, d_align (arg->length));
	bc_arg->arg.dlength = arg->length;
	os_memcpy (bc_arg->data, arg->data, arg->length);
	bc_oper->bitmask |= (0x1 << *bytepos);
	(*bytepos)++;
	break;
    case SH_ARG_FUNC:
	{
	    sh_gvar_t      *gaddr;
	    sh_errcode_t    err = bc_global_add (arg, SH_BC_ARG_FUNC, &gaddr);
	    if (err != SH_ERR_SUCCESS)
		d_stmt_err_ret (ctx, err);
	    bc_arg->arg.ptr = gaddr;
	    bc_oper->bitmask |= (0x3 << *bytepos);
	    (*bytepos)++;
	    break;
	}
    case SH_ARG_POINTER:
	bc_arg->arg.vptr = *d_pointer_as (bytecode_size_t, arg->data);
        //debug// os_printf("-- serialize arg ptr %p\n", bc_arg->arg.ptr);
	bc_oper->bitmask |= (0x3 << *bytepos);
	(*bytepos)++;
	break;
    default:
	d_assert (false, "unexpected type=%u", arg->type);
    }
    (*bytepos)++;

    return SH_ERR_SUCCESS;
}

/*
* [private] Serialize operation header to bytecode
*  - bc_ptr: current pointer to bytecode buffer
*  - optype: operator type
*  - arg_count: argumnet count
*  - bc_oper: result bytecode operator pointer
*/
LOCAL sh_errcode_t ICACHE_FLASH_ATTR
bc_serialize_oper_header (sh_parse_ctx_t * ctx, char **bc_ptr, sh_oper_type_t optype, arg_count_t arg_count,
			  sh_bc_oper_t ** bc_oper, uint8 * bytepos)
{
    sh_bc_oper_t   *bc_oper_ptr = d_pointer_as (sh_bc_oper_t, *bc_ptr);
    sh_oper_desc_t *opdesc = &sh_oper_desc[optype];

    //debug// os_printf("-- bc_serialize_ophdr: depth=%u, addr=%04x, " OD2T_STR " , args=%u\n", ctx->depth, *bc_ptr - ctx->bc_buf, OD2T (opdesc), arg_count);
    d_log_dprintf (LSH_SERVICE_NAME, "bc_serialize_ophdr: depth=%u, addr=%04x, " OD2T_STR " , args=%u", ctx->depth,
		   *bc_ptr - ctx->bc_buf, OD2T (opdesc), arg_count);

    bytecode_size_t len = sizeof (sh_bc_oper_t);
    if (opdesc->result) {
	len += sizeof (sh_bc_arg_t);
	*bytepos = 1;
    }
    else {
	*bytepos = 0;
    }
    d_bc_buffer_alloc (ctx, bc_ptr, len);
    os_memset (bc_oper_ptr, 0, len);
    bc_oper_ptr->optype = optype;
    bc_oper_ptr->arg_count = arg_count;
    *bc_oper = bc_oper_ptr;

    return SH_ERR_SUCCESS;
}

/*
* [private] Serialize control operation beginings to bytecode
*  - bc_ptr: current pointer to bytecode buffer
*  - pbuf_ptr: current pointer to parsed stack buffer
*  - arg: result pointer to jump pointer argument
*  - optype: operator type
*/
INLINED sh_errcode_t ICACHE_FLASH_ATTR
bc_serialize_oper_ctl (sh_parse_ctx_t * ctx, char **bc_ptr, char **pbuf_ptr, sh_parse_arg_t ** arg,
		       sh_oper_type_t optype)
{
    //debug// os_printf("-- serialize_oper_ctl: depth=%u, addr=%04x, optype=%u\n", ctx->depth, *bc_ptr - ctx->bc_buf, optype);

    d_log_dprintf (LSH_SERVICE_NAME, "serialize_oper_ctl: depth=%u, addr=%04x, optype=%u", ctx->depth,
		   *bc_ptr - ctx->bc_buf, optype);
    sh_bc_oper_t   *bc_oper_ptr;
    uint8           bytepos = 0;
    bc_serialize_oper_header (ctx, bc_ptr, optype, 2, &bc_oper_ptr, &bytepos);
    d_stmt_check_err (ctx);

    if (optype == SH_OPER_ELSE) {
        bytecode_size_t *vptr = d_pointer_as (bytecode_size_t, &(*arg)->data);
        //debug// os_printf("-- cond vptr %p\n", *vptr);
        sh_bc_oper_t * bc_oper0 = d_pointer_add(sh_bc_oper_t, ctx->bc_buf, *vptr);
	sh_bc_arg_t  * cond0_arg = d_pointer_add(sh_bc_arg_t, bc_oper0, sizeof (sh_bc_arg_t));

        //debug// os_printf("-- cond0 vptr %p\n", cond0_arg->arg.ptr);
        bc_oper0 = d_pointer_add(sh_bc_oper_t, ctx->bc_buf, cond0_arg->arg.ptr);
        *vptr = (bytecode_size_t) cond0_arg->arg.vptr;
    }


    // condition arg
    d_assert ((*arg), "left argument missed");
    bc_serialize_arg (ctx, bc_ptr, bc_oper_ptr, *arg, &bytepos);
    d_stmt_check_err (ctx);

    // jump arg
    bc_oper_ptr->bitmask |= (0x3 << bytepos);
    bytepos += 2;
    sh_bc_arg_t    *bc_arg = d_pointer_as (sh_bc_arg_t, *bc_ptr);
    //debug// os_printf("-- serialize jmparg %p\n", d_pointer_diff(bc_arg, ctx->bc_buf));
    d_bc_buffer_alloc (ctx, bc_ptr, sizeof (sh_bc_arg_t));
    bc_arg->arg.ptr = 0;

    // set left_arg pointer to cond oper
    bc_set_pointer_arg (ctx, pbuf_ptr, bc_oper_ptr, arg);

    return SH_ERR_SUCCESS;
}

/*
* [private] Serialize variable to bytecode
*  - bc_ptr: current pointer to bytecode buffer
*  - oper: pointer to parsed operator
*  - bc_oper: result pointer to bytecode operator
*/
INLINED sh_errcode_t ICACHE_FLASH_ATTR
bc_serialize_var (sh_parse_ctx_t * ctx, char **bc_ptr, sh_parse_oper_t * oper, sh_bc_oper_t ** bc_oper)
{
    sh_bc_oper_t   *bc_oper_ptr;
    uint8           bytepos = 0;
    bc_serialize_oper_header (ctx, bc_ptr, oper->optype, oper->arg_count, &bc_oper_ptr, &bytepos);
    d_stmt_check_err (ctx);

    d_assert (oper->arg_count == 1, "argument count for variable");
    sh_parse_arg_t *arg_ptr = d_pointer_as (sh_parse_arg_t, oper->varargs);
    d_assert (arg_ptr->type == SH_ARG_TOKEN, "variable name is token");

    bytecode_size_t *addr;
    ih_errcode_t    ihres = ih_hash8_add (ctx->varmap, arg_ptr->data, arg_ptr->length - 1, (char **) &addr, 0);
    *addr = d_pointer_diff (bc_oper_ptr, ctx->bc_buf);
    if (ihres != IH_ERR_SUCCESS) {
	if (ihres == IH_ENTRY_EXISTS) {
	    d_stmt_err_ret (ctx, SH_CODE_VARIABLE_EXIST, arg_ptr->data, oper->stmt_start);
	}
	else {
	    d_stmt_err_ret (ctx, SH_INTERNAL_ERROR);
	}
    }

    sh_bc_arg_t    *bc_arg = d_pointer_as (sh_bc_arg_t, *bc_ptr);
    d_bc_buffer_alloc (ctx, bc_ptr, sizeof (sh_bc_arg_t));
    if (oper->optype == SH_OPER_GVAR) {
	sh_gvar_t      *gvar;
	sh_errcode_t    err = bc_global_add (arg_ptr, SH_BC_ARG_INT, &gvar);
	if (err != SH_ERR_SUCCESS)
	    d_stmt_err_ret (ctx, err);

	bc_arg->arg.ptr = gvar;
	bc_oper_ptr->bitmask |= 0x3;
    }
    else {
	bc_arg->arg.value = 0;
    }
    *bc_oper = bc_oper_ptr;

    return SH_ERR_SUCCESS;
}

/*
* [private] Serialize functional operator to bytecode
*  - bc_ptr: current pointer to bytecode buffer
*  - oper: pointer to parsed operator
*  - bc_oper: result pointer to bytecode operator
*/
INLINED sh_errcode_t ICACHE_FLASH_ATTR
bc_serialize_foper (sh_parse_ctx_t * ctx, char **bc_ptr, sh_parse_oper_t * oper, sh_bc_oper_t ** bc_oper)
{
    sh_bc_oper_t   *bc_oper_ptr;
    uint8           bytepos = 0;
    bc_serialize_oper_header (ctx, bc_ptr, oper->optype, oper->arg_count, &bc_oper_ptr, &bytepos);
    d_stmt_check_err (ctx);

    arg_count_t     arg_idx = 0;
    if (oper->left_arg) {
	bc_serialize_arg (ctx, bc_ptr, bc_oper_ptr, oper->left_arg, &bytepos);
	d_stmt_check_err (ctx);
	arg_idx++;
    }

    sh_parse_arg_t *arg_ptr = d_pointer_as (sh_parse_arg_t, oper->varargs);
    while (arg_idx < oper->arg_count) {
	if (arg_ptr->type != SH_ARG_NONE) {
	    bc_serialize_arg (ctx, bc_ptr, bc_oper_ptr, arg_ptr, &bytepos);
	    d_stmt_check_err (ctx);
	    arg_idx++;
	}
	arg_ptr = d_pointer_add (sh_parse_arg_t, arg_ptr, sizeof (sh_parse_arg_t) + d_align (arg_ptr->length));
    }
    *bc_oper = bc_oper_ptr;

    return SH_ERR_SUCCESS;
}

/*
* [private] Make forward optimization before serialize to bytecode
*  - bc_ptr: current pointer to bytecode buffer
*  - arg: (not used yet) result pointer to last parsed argument
*  - oper: result pointer to parsed operator
*  - return: bool flag, true when optimazation has made 
*/
INLINED bool    ICACHE_FLASH_ATTR
bc_serialize_optimize (sh_parse_ctx_t * ctx, sh_parse_arg_t ** arg, sh_parse_oper_t ** oper)
{
    bool            optjoin = false;
    bool            optskip = false;
    sh_parse_oper_t *oper_tmp = *oper;

    if (oper_tmp->optype == SH_OPER_ARGLIST) {
	if (oper_tmp->prev_oper->optype == SH_OPER_FUNC) {
	    optjoin = true;
	}
    }
    else if ((oper_tmp->optype == SH_OPER_FUNC) && !oper_tmp->left_arg && (oper_tmp->arg_count == 1)) {
	optskip = true;
    }
    else if (oper_tmp->optype == SH_OPER_BLOCK) {
	optskip = true;
	//*arg = NULL;
    }
    else
	return false;

    if (optjoin) {
	oper_tmp->prev_oper->arg_count += oper_tmp->arg_count - 1;
	sh_parse_arg_t *arg_ptr = d_pointer_as (sh_parse_arg_t, oper_tmp);
	arg_ptr->type = SH_ARG_NONE;
	arg_ptr->length = sizeof (sh_parse_oper_t) - sizeof (sh_parse_arg_t);
    }
    if (optskip) {
	sh_parse_arg_t *arg_ptr = d_pointer_as (sh_parse_arg_t, oper_tmp);
	arg_ptr->type = SH_ARG_NONE;
	arg_ptr->length = sizeof (sh_parse_oper_t) - sizeof (sh_parse_arg_t);
    }

    *oper = oper_tmp->prev_oper;
    ctx->depth--;
    return true;
}


/*
* [private] Serialize operation to bytecode
*  - bc_ptr: current pointer to bytecode buffer
*  - pbuf_ptr: in/out current pointer to parsed stack buffer
*  - arg: in/out pointer to last left parsed argument
*  - oper: in/out pointer to parsed operator
*/
LOCAL sh_errcode_t ICACHE_FLASH_ATTR
bc_serialize_oper (sh_parse_ctx_t * ctx, char **bc_ptr, char **pbuf_ptr, sh_parse_arg_t ** arg, sh_parse_oper_t ** oper)
{
    sh_parse_oper_t *oper_tmp = *oper;

    /* Optimization */
    if (bc_serialize_optimize (ctx, arg, oper))
	return SH_ERR_SUCCESS;

    sh_bc_oper_t   *bc_oper_ptr;
    //debug// os_printf("-- bc_serialize_oper %p\n", bc_oper_ptr);
    if (oper_tmp->optype == SH_OPER_BLOCK) {
	uint8           bytepos;
	bc_serialize_oper_header (ctx, bc_ptr, oper_tmp->optype, 0, &bc_oper_ptr, &bytepos);
    }
    else if (oper_tmp->control) {
	d_assert ((oper_tmp->left_arg), "left argument missed");
        bytecode_size_t *vptr = d_pointer_as (bytecode_size_t, &oper_tmp->left_arg->data);
        //debug// os_printf("-- ctl left vptr: %p\n", *vptr);
	bc_oper_ptr = d_pointer_add (sh_bc_oper_t, ctx->bc_buf, *vptr);
	// use second arg for jump target
	sh_bc_arg_t    *bc_arg = d_pointer_add (sh_bc_arg_t, bc_oper_ptr, sizeof (sh_bc_oper_t) + sizeof (sh_bc_arg_t));
	bc_arg->arg.ptr = (void *) d_pointer_diff (*bc_ptr, ctx->bc_buf);

        //debug// os_printf("-- bc_serialize_oper: ctl jump vptr=+0x %04x\n", bc_arg->arg.ptr);
	d_log_dprintf (LSH_SERVICE_NAME, "bc_serialize_oper: ctl jump vptr=+0x%04x", bc_arg->arg.ptr);
    }
    else {
        //debug// os_printf("-- bc_serialize_oper: depth=%u, addr=%04x, " OP2T_STR " , args=%u\n", ctx->depth, *bc_ptr - ctx->bc_buf, OP2T (ctx, oper_tmp), oper_tmp->arg_count);
	d_log_dprintf (LSH_SERVICE_NAME, "bc_serialize_oper: depth=%u, addr=%04x, " OP2T_STR " , args=%u", ctx->depth,
		       *bc_ptr - ctx->bc_buf, OP2T (ctx, oper_tmp), oper_tmp->arg_count);
	if ((oper_tmp->optype == SH_OPER_VAR) || (oper_tmp->optype == SH_OPER_GVAR)) {
	    bc_serialize_var (ctx, bc_ptr, oper_tmp, &bc_oper_ptr);
	}
	else {
	    bc_serialize_foper (ctx, bc_ptr, oper_tmp, &bc_oper_ptr);
	}
	d_stmt_check_err (ctx);
    }

    if (!bc_oper_ptr) {
	d_stmt_err_ret (ctx, SH_INTERNAL_ERROR);
    }

    // pop oper and set left_arg pointer to bc_oper result
    *oper = oper_tmp->prev_oper;
    sh_parse_arg_t *arg_ptr = oper_tmp->left_arg;
    if (!arg_ptr) {
	arg_ptr = d_pointer_as (sh_parse_arg_t, oper_tmp);	// write position
    }
    *arg = arg_ptr;
    bc_set_pointer_arg (ctx, pbuf_ptr, bc_oper_ptr, arg);
    ctx->depth--;

    return SH_ERR_SUCCESS;
}

/*
[inline] Parse operator token and make operator statement context
  - ctx: current parsing context
  - szstr: current parisng position
  - oper: pointer to last/result operator
  - arg: pointer to last argument
  - result: parse result
*/
INLINED sh_oper_type_t ICACHE_FLASH_ATTR
stmt_parse_oper (sh_parse_ctx_t * ctx,
		 const char **szstr, char **bc_ptr, char **pbuf_ptr, sh_parse_arg_t ** arg, sh_parse_oper_t ** oper)
{
    sh_oper_type_t  optype = parse_optype (szstr);
    if (optype == SH_OPER_NONE)
	return SH_OPER_NONE;

    sh_parse_oper_t *last_oper = *oper;

    sh_oper_desc_t *opdesc = &sh_oper_desc[optype];
    while (last_oper) {
	// determine associativity in stmt context
	sh_parse_assoc_type_t assoc_type = SH_PARSE_ASSOC_TYPE_NONE;
	if (last_oper->optype != SH_OPER_NONE) {
	    sh_oper_desc_t *opdesc2 = &sh_oper_desc[last_oper->optype];
	    if ((*opdesc2->term) || (opdesc2->control && !opdesc->control)) {	// explicit term or control is more precedence
		assoc_type = SH_PARSE_ASSOC_TYPE_R2L;
	    }
	    else if ((last_oper->optype != optype) || (!opdesc->concat)) {
		assoc_type =
		    (opdesc2->precedence > opdesc->precedence) ? SH_PARSE_ASSOC_TYPE_R2L : SH_PARSE_ASSOC_TYPE_L2R;
	    }
	    else {
		assoc_type =
		    (last_oper->arg_count <
		     LSH_OPER_ARG_COUNT_MAX) ? SH_PARSE_ASSOC_TYPE_CONCAT : SH_PARSE_ASSOC_TYPE_L2R;
	    }
	}

	switch (assoc_type) {
	case SH_PARSE_ASSOC_TYPE_L2R:
	    // serialize Prev operator to bytecode
	    if (bc_serialize_oper (ctx, bc_ptr, pbuf_ptr, arg, &last_oper) != SH_ERR_SUCCESS)
		return SH_OPER_NONE;
	    continue;
	case SH_PARSE_ASSOC_TYPE_CONCAT:
	    *oper = last_oper;
	    *arg = NULL;
	    return optype;
	default:
	    break;
	}
	break;
    }

    if (!*arg) {
	if (last_oper) {
	    last_oper->arg_count++;
	}
	if (opdesc->opd_left == SH_OPERAND_MAND) {
	    d_stmt_err_ret_ext (ctx, SH_PARSE_ERROR_OPERAND_MISS, optype, "left");
	}
    }
    else {
	if (opdesc->opd_left == SH_OPERAND_NONE) {
	    d_stmt_err_ret_ext (ctx, SH_PARSE_ERROR_OPERAND_UNEXPECT, optype, "left");
	}
    }


    ctx->depth++;
    if (opdesc->control && *arg) {
	bc_serialize_oper_ctl (ctx, bc_ptr, pbuf_ptr, arg, optype);
    }
    sh_parse_oper_t *oper_ptr = d_pointer_as (sh_parse_oper_t, *pbuf_ptr);
    (*pbuf_ptr) += sizeof (sh_parse_oper_t);
    os_memset (oper_ptr, 0, sizeof (sh_parse_oper_t));
    oper_ptr->prev_oper = last_oper;
    oper_ptr->optype = optype;
    oper_ptr->control = opdesc->control;
    if (*arg) {
	oper_ptr->left_arg = *arg;
	oper_ptr->arg_count = 1;
	if (optype == SH_OPER_FUNC) {
	    (*arg)->type = SH_ARG_FUNC;
	}
    }
    oper_ptr->stmt_start = *szstr - 1;
    *arg = NULL;
    if (*opdesc->term) {
	oper_ptr->term = *opdesc->term;
	ctx->term_oper = oper_ptr;
    }
    *oper = oper_ptr;

    d_log_dprintf (LSH_SERVICE_NAME, "parse_oper: depth=%u, pos=%u, " OP2T_STR " " OD2T_STR, ctx->depth,
		   (*szstr - ctx->stmt_start), OP2T (ctx, oper_ptr), OD2T (opdesc));
    return optype;
}


/*
[inline] Parse statement argument
  - ctx: current parsing context
  - pbuf_ptr: current position in temporary parse buffer
  - szstr: current parisng position
  - arg: pointer to result argument
  - result: parse result
*/
INLINED sh_errcode_t ICACHE_FLASH_ATTR
stmt_parse_arg (sh_parse_ctx_t * ctx, const char **szstr, char **pbuf_ptr, sh_parse_oper_t * oper, sh_parse_arg_t ** arg)
{
    *arg = NULL;
    // Fixme: Check buffer length
    sh_parse_arg_t *arg_ptr = d_pointer_as (sh_parse_arg_t, *pbuf_ptr);

    if (d_char_is_digit (*szstr)) {
	// constant number
	arg_ptr->type = SH_ARG_INT;
	arg_ptr->length = sizeof (uint32);
	uint32         *value_uint = d_pointer_as (uint32, &arg_ptr->data);

	if (!parse_uint (szstr, value_uint)) {
	    d_stmt_err_ret (ctx, SH_PARSE_ERROR_NUMINV, d_stmt_pos (ctx, *szstr));
	}

	d_log_dprintf (LSH_SERVICE_NAME, "parse_arg: depth=%d, pos=%d, num:%u", ctx->depth, (*szstr - ctx->stmt_start),
		       *value_uint);
    }
    else if (d_char_is_quote (*szstr)) {
	// constant string
	arg_ptr->type = SH_ARG_CHAR;
	size_t length;
	estlen_qstr (*szstr, &length);
        arg_ptr->length = (parse_size_t) length;
	// null-terminate
	arg_ptr->data[arg_ptr->length] = '\0';
	arg_ptr->length++;

	if (!parse_qstr (szstr, (char *) arg_ptr->data)) {
	    d_stmt_err_ret (ctx, SH_PARSE_ERROR_STRINV, d_stmt_pos (ctx, *szstr));
	}

	d_log_dprintf (LSH_SERVICE_NAME, "parse_arg: depth=%d, pos=%d, str:%s", ctx->depth, (*szstr - ctx->stmt_start),
		       arg_ptr->data);
    }
    else if (d_char_is_token1 (*szstr)) {
	// function | variable | extended operator | nested statement
	arg_ptr->type = SH_ARG_TOKEN;
	size_t length;
	estlen_token (*szstr, &length);
        arg_ptr->length = (parse_size_t) length;
	// null-terminate
	arg_ptr->data[arg_ptr->length] = '\0';
	arg_ptr->length++;

	if (!parse_token (szstr, (char *) arg_ptr->data)) {
	    d_stmt_err_ret (ctx, SH_PARSE_ERROR_TOKENINV, d_stmt_pos (ctx, *szstr));
	}

	d_log_dprintf (LSH_SERVICE_NAME, "parse_arg: depth=%d, pos=%d, token:%s", ctx->depth,
		       (*szstr - ctx->stmt_start), arg_ptr->data);
    }
    else {
	d_stmt_err_ret (ctx, SH_PARSE_ERROR_TOKENINV, d_stmt_pos (ctx, *szstr));
    }

    if (oper) {
	oper->arg_count++;
    }
    (*pbuf_ptr) += sizeof (sh_parse_arg_t) + d_align (arg_ptr->length);
    *arg = arg_ptr;

    return SH_ERR_SUCCESS;
}

INLINED sh_errcode_t ICACHE_FLASH_ATTR
stmt_parse_ext (sh_parse_ctx_t * ctx, const char **szstr, char **bc_ptr, char **pbuf_ptr)
{
    sh_parse_oper_t *oper = NULL;
    sh_parse_arg_t *arg = NULL;

    d_skip_space (*szstr);
    while (**szstr != '\0') {
	const char     *ptr_start = *szstr;
	// check operator term
        //debug// os_printf("-- 000 %p %c\n", oper, **szstr);

	d_skip_space (*szstr);
	if ((ctx->term_oper) && (ctx->term_oper->term == **szstr)) {
	    (*szstr)++;
	    while (oper != ctx->term_oper) {
        	//debug// os_printf("-- 1\n");
		bc_serialize_oper (ctx, bc_ptr, pbuf_ptr, &arg, &oper);
        	//debug// os_printf("-- 2\n");
		d_stmt_check_err (ctx);
	    }
            //debug// os_printf("-- 3\n");
	    bc_serialize_oper (ctx, bc_ptr, pbuf_ptr, &arg, &oper);
            //debug// os_printf("-- 4\n");
	    d_stmt_check_err (ctx);

	    // look for prev term
	    sh_parse_oper_t *oper2 = oper;
	    while (oper2) {
		if (oper2->term) {
		    break;
		}
		oper2 = oper2->prev_oper;
	    }
	    ctx->term_oper = oper2;
	    continue;
	}
	else if (**szstr == ';') {
	    (*szstr)++;
            if (oper) {	// default terminator
	        while (oper && (oper->optype != SH_OPER_BLOCK)) {
                    //debug// os_printf("-- 5\n");
		    bc_serialize_oper (ctx, bc_ptr, pbuf_ptr, &arg, &oper);
                    //debug// os_printf("-- 6\n");
		    d_stmt_check_err (ctx);
	        }
	    }
	    arg = NULL;
	    continue;
	}

	// try parsing an operator
	//debug// os_printf("-- 7\n");
	if (stmt_parse_oper (ctx, szstr, bc_ptr, pbuf_ptr, &arg, &oper) != SH_OPER_NONE) {
	    continue;		// try parse term and operaror
	}
	d_stmt_check_err (ctx);

	// try to parsing an argument
	//debug// os_printf("-- 8\n");
	stmt_parse_arg (ctx, szstr, pbuf_ptr, oper, &arg);
	//debug// os_printf("-- 9\n");
	d_stmt_check_err (ctx);
	d_skip_space (*szstr);

	if (*szstr == ptr_start) {
	    d_stmt_err_ret (ctx, SH_PARSE_ERROR_TOKENINV, d_stmt_pos (ctx, *szstr));
	}
    }

    while (oper) {
	//debug// os_printf("-- 3\n");
	bc_serialize_oper (ctx, bc_ptr, pbuf_ptr, &arg, &oper);
	//debug// os_printf("-- 4\n");
	d_stmt_check_err (ctx);
    }

    return SH_ERR_SUCCESS;
}


sh_errcode_t    ICACHE_FLASH_ATTR
stmt_parse (const char *szstr, const char * stmt_name, sh_hndlr_t * hstmt)
{
    d_check_init();

    const char     *ptr = szstr;
    char           *bc_ptr;
    char           *pbuf_ptr;
    char           *varmap_ptr;
    d_log_dprintf (LSH_SERVICE_NAME, "parse: \"%s\"", ptr);

    sh_parse_ctx_t  ctx;
    os_memset (&ctx, 0, sizeof (sh_parse_ctx_t));
    ctx.stmt_start = ptr;
    ctx.errcode = SH_ERR_SUCCESS;

#ifdef LSH_BUFFERS_IMDB
    d_sh_check_imdb_error (imdb_clsobj_insert (sdata->svcres->hmdb, sdata->svcres->hdata, (void **) &bc_ptr, LSH_STMT_BUFFER_SIZE));
    d_sh_check_imdb_error (imdb_clsobj_insert (sdata->svcres->hmdb, sdata->svcres->hdata, (void **) &pbuf_ptr, LSH_STMT_PARSE_BUFFER_SIZE));
    d_sh_check_imdb_error (imdb_clsobj_insert (sdata->svcres->hmdb, sdata->svcres->hdata, (void **) &varmap_ptr, LSH_STMT_VARIDX_BUFFER_SIZE));
#else
    sh_parse_buffers_t *buffers = os_malloc (sizeof (sh_parse_buffers_t));
    if (!buffers) {
	d_log_eprintf (LSH_SERVICE_NAME, sz_sh_error[SH_ALLOCATION_ERROR], sizeof (sh_parse_buffers_t));
        return SH_ALLOCATION_ERROR;
    }
    bc_ptr = (char *) &buffers->bc;
    pbuf_ptr = (char *) &buffers->pbuf;
    varmap_ptr = (char *) &buffers->varmap;
#endif

    if (ih_init8 (varmap_ptr, LSH_STMT_VARIDX_BUFFER_SIZE, 16, 0, sizeof (bytecode_size_t), &ctx.varmap) !=
	IH_ERR_SUCCESS) {
	return SH_INTERNAL_ERROR;
    }

    ctx.bc_buf = bc_ptr;
    ctx.parse_buf = pbuf_ptr;

    sh_errcode_t    res = stmt_parse_ext (&ctx, &ptr, &bc_ptr, &pbuf_ptr);

    sh_stmt_t      *stmt = NULL;
    imdb_errcode_t  imdb_res = IMDB_ERR_SUCCESS;
    if (res == SH_ERR_SUCCESS) {
	bytecode_size_t len = bc_ptr - ctx.bc_buf;
	imdb_res = imdb_clsobj_insert (sdata->svcres->hmdb, sdata->hstmt, (void **) &stmt, sizeof (sh_stmt_t) + len);
	if (imdb_res == IMDB_ERR_SUCCESS) {
	    os_memset (&stmt->info, 0, sizeof (sh_stmt_info_t));

	    stmt->info.parse_time = lt_ctime ();
	    stmt->info.length = len;
            os_memcpy (stmt->info.name, stmt_name, os_strnlen (stmt_name, sizeof (sh_stmt_name_t)) );

	    os_memcpy (stmt->vardata, ctx.bc_buf, len);
	}
    }
    else {
	d_log_wprintf (LSH_SERVICE_NAME, "parse: error pos:%u, code:%u, msg:\"%s\"", ptr - ctx.stmt_start, ctx.errcode,
		       ctx.errmsg);
    }

#ifdef LSH_BUFFERS_IMDB
    d_sh_check_imdb_error (imdb_clsobj_delete (sdata->svcres->hmdb, sdata->hdata, varmap_ptr));
    d_sh_check_imdb_error (imdb_clsobj_delete (sdata->svcres->hmdb, sdata->hdata, ctx.bc_buf));
    d_sh_check_imdb_error (imdb_clsobj_delete (sdata->svcres->hmdb, sdata->hdata, ctx.parse_buf));
#else
    os_free (buffers);
#endif

    d_sh_check_imdb_error (imdb_res);

    *hstmt = d_obj2hndlr (stmt);
    return res;
}

sh_errcode_t    ICACHE_FLASH_ATTR
stmt_free (const sh_hndlr_t hstmt)
{
    d_check_init();

    d_sh_check_hndlr (hstmt);
    sh_stmt_t      *stmt = d_hndlr2obj (sh_stmt_t, hstmt);
    d_sh_check_imdb_error (imdb_clsobj_delete (sdata->svcres->hmdb, sdata->hstmt, (void *) stmt));
    return SH_ERR_SUCCESS;
}


/*
 * [public] pop bytecode argument type from mask
 * - mask: arg mask
 * - bc_arg: argumant pointer
 * - returns: bytecode argument type
 */
sh_bc_arg_type_t ICACHE_FLASH_ATTR
sh_pop_bcarg_type(uint16 * mask, sh_bc_arg_t * bc_arg) {
    sh_bc_arg_type_t res = SH_BC_ARG_NONE;

    if (*mask & 1) {
	if (*mask & 0x2)
	    res = (bc_arg->arg.vptr > SH_BYTECODE_SIZE_MAX) ? SH_BC_ARG_GLOBAL : SH_BC_ARG_LOCAL;
	else 
	    res = SH_BC_ARG_CHAR;
        *mask = *mask >> 2;
    }
    else {
        res = SH_BC_ARG_INT;
        *mask = *mask >> 1;
    }

    return res;
}

/*
 * [public] dump bytecode in text presetation
 * - hstmt: handle to statement
 * - buf: output buffer
 * - len: buffer length
 */
sh_errcode_t    ICACHE_FLASH_ATTR
stmt_dump (const sh_hndlr_t hstmt, char *buf, size_t len, bool resolve_glob)
{
    // Fixme: Check output buffer length
    char           *buf_ptr = buf;
    sh_stmt_t      *stmt = d_hndlr2obj (sh_stmt_t, hstmt);

    char           *bc_ptr = stmt->vardata;
    char           *ptr_max = bc_ptr + stmt->info.length;
    // Fixme: Check length when shift bc_ptr
    while (bc_ptr < ptr_max) {
	sh_bc_oper_t   *bc_oper_ptr = d_pointer_as (sh_bc_oper_t, bc_ptr);
	sh_oper_desc_t *opdesc = &sh_oper_desc[bc_oper_ptr->optype];
	buf_ptr +=
	    os_sprintf (buf_ptr, "\t%04x:\t%s%s\t", bc_ptr - (char *) stmt->vardata, opdesc->token, opdesc->term);
	bc_ptr += sizeof (sh_bc_oper_t);

	arg_count_t     idx;
	uint16          mask = bc_oper_ptr->bitmask;
	arg_count_t     count = bc_oper_ptr->arg_count + (opdesc->result ? 1 : 0);
	for (idx = 0; idx < count; idx++) {
	    sh_bc_arg_t    *bc_arg = d_pointer_as (sh_bc_arg_t, bc_ptr);
	    bc_ptr += sizeof (sh_bc_arg_t);
	    if (idx > 0) {
		buf_ptr += os_sprintf (buf_ptr, ", ");
	    }

	    switch (sh_pop_bcarg_type(&mask, bc_arg)) {
	    case SH_BC_ARG_INT:
		buf_ptr += os_sprintf (buf_ptr, "%u", bc_arg->arg.value);
		break;
            case SH_BC_ARG_CHAR:
		buf_ptr += os_sprintf (buf_ptr, "\"%s\"", bc_arg->data);
		bc_ptr += d_align (bc_arg->arg.dlength);
		break;
            case SH_BC_ARG_LOCAL:
		buf_ptr += os_sprintf (buf_ptr, "vptr+0x%x", bc_arg->arg.vptr);
		break;
            case SH_BC_ARG_GLOBAL:
		if (resolve_glob) {
                    sh_gvar_t      *gvar = d_pointer_as(sh_gvar_t, bc_arg->arg.ptr);
                    const char     *gname = ih_hash8_v2key (sdata->token_idx, (const char *) gvar);
                    
		    if (gvar->type == SH_BC_ARG_INT)
                        buf_ptr += os_sprintf (buf_ptr, "<%s: %u>", gname, gvar->arg.arg.value);
		    else if (gvar->type == SH_BC_ARG_CHAR)
                        buf_ptr += os_sprintf (buf_ptr, "<%s: \"%s\">", gname, gvar->arg.data);
                    else 
                        buf_ptr += os_sprintf (buf_ptr, "<%s>", gname);
		}
		else {
		    buf_ptr += os_sprintf (buf_ptr, "ptr=%p", bc_arg->arg.ptr);
		}
		break;
	    default:
                return SH_INTERNAL_ERROR;	
            }

	}

	*buf_ptr = '\n';
	buf_ptr++;
    }
    buf_ptr += os_sprintf (buf_ptr, "\t%04x:\tret", bc_ptr - (char *) stmt->vardata);
    *buf_ptr = '\0';

    return SH_ERR_SUCCESS;
}

sh_errcode_t    ICACHE_FLASH_ATTR
stmt_info (const sh_hndlr_t hstmt, sh_stmt_info_t * info)
{
    sh_stmt_t      *stmt = d_hndlr2obj (sh_stmt_t, hstmt);
    os_memcpy (info, &stmt->info, sizeof (sh_stmt_info_t));

    return SH_ERR_SUCCESS;
}

LOCAL sh_errcode_t ICACHE_FLASH_ATTR
stmt_eval_popvar (sh_stmt_t * stmt, char ** bc_ptr, uint16 * mask, sh_bc_arg_t ** bc_arg, sh_bc_arg_type_t * arg_type)
{
    *bc_arg = d_pointer_as (sh_bc_arg_t, *bc_ptr);
    (*bc_ptr) += sizeof (sh_bc_arg_t);

    char           *ptr_max = d_pointer_add(char, stmt->vardata, stmt->info.length);
    if (*bc_ptr > ptr_max)
        return SH_INTERNAL_ERROR;

    *arg_type = sh_pop_bcarg_type (mask, *bc_arg);
    //debug// os_printf("-- 7.1 %u\n", *arg_type);
    switch (*arg_type) {
    case SH_BC_ARG_INT:
        break;
    case SH_BC_ARG_CHAR:
	(*bc_ptr) += d_align ((*bc_arg)->arg.dlength);
        break;
    case SH_BC_ARG_LOCAL:
    case SH_BC_ARG_GLOBAL:
        {
	    sh_bc_arg_t *arg = NULL;
	    if ((*bc_arg)->arg.vptr > SH_BYTECODE_SIZE_MAX) {
	        sh_gvar_t      *gvar = d_pointer_as(sh_gvar_t, (*bc_arg)->arg.ptr);
                //debug// os_printf("-- 7.2 %p\n", gvar);
	        arg = &gvar->arg;

                if ((gvar->type == SH_BC_ARG_FUNC) && (!arg->arg.ptr)) {
                    const char      *func_name = ih_hash8_v2key (sdata->token_idx, (const char *) gvar);

                    d_sh_check_error (sh_func_get (func_name, (sh_func_entry_t **) &(arg->arg.ptr)));
                    //debug// os_printf("-- 7.2.1 %s %p\n", func_name, arg->arg.ptr);
                }
                *arg_type = gvar->type;
	    }
	    else {
        	sh_bc_oper_t * bc_oper = d_pointer_add(sh_bc_oper_t, stmt->vardata, (*bc_arg)->arg.vptr);
                //debug// os_printf("-- 7.3 %u\n", bc_oper->optype);
        	if ((sh_oper_desc[bc_oper->optype].result) || (bc_oper->optype == SH_OPER_VAR) || (sh_oper_desc[bc_oper->optype].control)) // Check that oper has result
	            arg = d_pointer_add(sh_bc_arg_t, bc_oper, sizeof (sh_bc_arg_t));
        	else if (bc_oper->optype == SH_OPER_GVAR) {
	            arg = d_pointer_add(sh_bc_arg_t, bc_oper, sizeof (sh_bc_arg_t));
        	    sh_gvar_t      *gvar = d_pointer_as(sh_gvar_t, arg->arg.ptr);
	            arg = &gvar->arg;
	        }
	        *arg_type = SH_BC_ARG_INT;
	    }
            //debug// os_printf("-- 7.4 %p\n", arg);
            if (!arg)
                return SH_INTERNAL_ERROR;
            *bc_arg = arg;
        }
        break;
    default:
        return SH_INTERNAL_ERROR;
    }
    //debug// os_printf("-- 7.5 %u\n", *arg_type);

    return SH_ERR_SUCCESS;
}

LOCAL sh_errcode_t    ICACHE_FLASH_ATTR
stmt_eval_assign (sh_stmt_t * stmt, sh_bc_oper_t * bc_oper, char ** bc_ptr)
{
    if (bc_oper->arg_count != 2)
        return SH_INTERNAL_ERROR;

    uint16          mask = bc_oper->bitmask;

    sh_bc_arg_type_t arg_type;
    sh_bc_arg_t    *res_arg;
    sh_bc_arg_t    *src_arg;

    d_sh_check_error (stmt_eval_popvar (stmt, bc_ptr, &mask, &res_arg, &arg_type));
    d_sh_check_error (stmt_eval_popvar (stmt, bc_ptr, &mask, &src_arg, &arg_type));

    res_arg->arg.value = src_arg->arg.value;

    return SH_ERR_SUCCESS;
}

LOCAL sh_errcode_t    ICACHE_FLASH_ATTR
stmt_eval_func (sh_stmt_t * stmt, sh_bc_oper_t * bc_oper, char ** bc_ptr)
{
    uint16          mask = bc_oper->bitmask;

    sh_bc_arg_type_t arg_type;
    sh_bc_arg_t    *res_arg;
    sh_bc_arg_t    *func_arg;

    d_sh_check_error (stmt_eval_popvar (stmt, bc_ptr, &mask, &res_arg, &arg_type));
    d_sh_check_error (stmt_eval_popvar (stmt, bc_ptr, &mask, &func_arg, &arg_type));
    if (arg_type != SH_BC_ARG_FUNC)
        return SH_INTERNAL_ERROR;

    arg_count_t     idx;
    arg_count_t     count = bc_oper->arg_count - 1;
    sh_bc_arg_t    *bc_args[LSH_OPER_ARG_COUNT_MAX];
    sh_bc_arg_type_t arg_types[LSH_OPER_ARG_COUNT_MAX];
    for (idx = 0; idx < count; idx++) {
        d_sh_check_error (stmt_eval_popvar (stmt, bc_ptr, &mask, &bc_args[idx], &arg_types[idx]));
    }

    //debug// os_printf("-- 8.1\n");
    sh_func_entry_t *entry = func_arg->arg.ptr;
    entry->func.func (res_arg, count, arg_types, bc_args);
    //debug// os_printf("-- 8.2\n");

    return SH_ERR_SUCCESS;
}


LOCAL sh_errcode_t    ICACHE_FLASH_ATTR
stmt_eval_foper (sh_stmt_t * stmt, sh_bc_oper_t * bc_oper, char ** bc_ptr)
{
    if ((bc_oper->arg_count != 2) && (bc_oper->arg_count != 1))
        return SH_INTERNAL_ERROR;

    sh_bc_arg_type_t arg_type;
    sh_bc_arg_t    *res_arg;
    sh_bc_arg_t    *left_arg;
    sh_bc_arg_t    *right_arg;

    uint16          mask = bc_oper->bitmask;

    d_sh_check_error (stmt_eval_popvar (stmt, bc_ptr, &mask, &res_arg, &arg_type));
    d_sh_check_error (stmt_eval_popvar (stmt, bc_ptr, &mask, &left_arg, &arg_type));
    if (arg_type != SH_BC_ARG_INT)
        return SH_INTERNAL_ERROR;
    if (bc_oper->arg_count == 2) {
        d_sh_check_error (stmt_eval_popvar (stmt, bc_ptr, &mask, &right_arg, &arg_type));
        if (arg_type != SH_BC_ARG_INT)
            return SH_INTERNAL_ERROR;
    }

    switch (bc_oper->optype) {
    case SH_OPER_NOT:
        res_arg->arg.value = ! left_arg->arg.value;
        break;
    case SH_OPER_BIT_NOT:
        res_arg->arg.value = ~left_arg->arg.value;
        break;
    case SH_OPER_LT:
        res_arg->arg.value = left_arg->arg.value < right_arg->arg.value;
        break;
    case SH_OPER_GT:
        res_arg->arg.value = left_arg->arg.value > right_arg->arg.value;
        break;
    case SH_OPER_LTEQ:
        res_arg->arg.value = left_arg->arg.value <= right_arg->arg.value;
        break;
    case SH_OPER_GTEQ:
        res_arg->arg.value = left_arg->arg.value >= right_arg->arg.value;
        break;
    case SH_OPER_EQ:
        res_arg->arg.value = left_arg->arg.value == right_arg->arg.value;
        break;
    case SH_OPER_NOTEQ:
        res_arg->arg.value = left_arg->arg.value != right_arg->arg.value;
        break;
    default:
        return SH_INTERNAL_ERROR;
    }

    return SH_ERR_SUCCESS;
}

LOCAL sh_errcode_t    ICACHE_FLASH_ATTR
stmt_eval_foper_concat (sh_stmt_t * stmt, sh_bc_oper_t * bc_oper, char ** bc_ptr)
{
    sh_bc_arg_type_t arg_type;
    sh_bc_arg_t    *res_arg;

    uint16          mask = bc_oper->bitmask;

    d_sh_check_error (stmt_eval_popvar (stmt, bc_ptr, &mask, &res_arg, &arg_type));

    uint32 res_value = 0;

    arg_count_t     idx;
    arg_count_t     count = bc_oper->arg_count;
    for (idx = 0; idx < count; idx++) {
        sh_bc_arg_t    *bc_arg;
        d_sh_check_error (stmt_eval_popvar (stmt, bc_ptr, &mask, &bc_arg, &arg_type));
        if (arg_type != SH_BC_ARG_INT)
            return SH_INTERNAL_ERROR;

        uint32 value = bc_arg->arg.value;

        if (idx == 0) {
            res_value = value;
            continue;
        }

        switch (bc_oper->optype) {
	case SH_OPER_PLUS:
	    res_value += value;
            break;
	case SH_OPER_MINUS:
	    res_value -= value;
            break;
        case SH_OPER_MULTIPLY:
	    res_value *= value;
            break;
        case SH_OPER_DIV:
	    res_value /= value;
            break;
        case SH_OPER_MOD:
	    res_value %= value;
            break;
        case SH_OPER_BIT_AND:
	    res_value &= value;
            break;
        case SH_OPER_BIT_SR:
	    res_value >>= value;
            break;
        case SH_OPER_BIT_SL:
	    res_value <<= value;
            break;
        case SH_OPER_BIT_OR:
	    res_value |= value;
            break;
        case SH_OPER_BIT_XOR:
	    res_value ^= value;
            break;
        case SH_OPER_AND:
	    res_value = res_value && value;
            break;
        case SH_OPER_OR:
	    res_value = res_value || value;
            break;
        default:
            return SH_INTERNAL_ERROR;
        }
    }

    res_arg->arg.value = res_value;

    return SH_ERR_SUCCESS;
}


sh_errcode_t    ICACHE_FLASH_ATTR
stmt_eval (const sh_hndlr_t hstmt, sh_eval_ctx_t * ctx)
{
    sh_stmt_t      *stmt = d_hndlr2obj (sh_stmt_t, hstmt);

    char           *bc_ptr = stmt->vardata;
    char           *ptr_max = bc_ptr + stmt->info.length;

    while (bc_ptr < ptr_max) {
	sh_bc_oper_t   *bc_oper_ptr = d_pointer_as (sh_bc_oper_t, bc_ptr);
	sh_oper_desc_t *opdesc = &sh_oper_desc[bc_oper_ptr->optype];
	//debug// os_printf ("%04x:\t%s%s %u\n", bc_ptr - (char *) stmt->vardata, opdesc->token, opdesc->term, bc_oper_ptr->arg_count);

	bc_ptr += sizeof (sh_bc_oper_t);
	if (bc_ptr > ptr_max)
	    return SH_INTERNAL_ERROR;

	switch (bc_oper_ptr->optype) {
	case SH_OPER_VAR:
	case SH_OPER_GVAR:
	    // skip args
	    {
                uint16          mask = bc_oper_ptr->bitmask;
                arg_count_t     idx;
                sh_bc_arg_t    *bc_arg;
                sh_bc_arg_type_t arg_type;
	        arg_count_t     count = bc_oper_ptr->arg_count + (opdesc->result ? 1 : 0);
                for (idx = 0; idx < count; idx++) {
                    d_sh_check_error (stmt_eval_popvar (stmt, &bc_ptr, &mask, &bc_arg, &arg_type));
                }
            }
	    break;
	case SH_OPER_ASSIGN:
	    d_sh_check_error ( stmt_eval_assign(stmt, bc_oper_ptr, &bc_ptr));
	    break;
	case SH_OPER_FUNC:
	    d_sh_check_error ( stmt_eval_func(stmt, bc_oper_ptr, &bc_ptr));
	    break;
	case SH_OPER_IF:
	case SH_OPER_IFRET:
	case SH_OPER_ELSE:
	    {
                uint16          mask = bc_oper_ptr->bitmask;
                sh_bc_arg_type_t arg_type;
                sh_bc_arg_t    *bc_arg = d_pointer_as (sh_bc_arg_t, bc_ptr);
                d_sh_check_error (stmt_eval_popvar (stmt, &bc_ptr, &mask, &bc_arg, &arg_type));

	        sh_bc_arg_t    *jmp_arg = d_pointer_as (sh_bc_arg_t, bc_ptr);
                sh_pop_bcarg_type(&mask, jmp_arg);
	        bc_ptr += sizeof (sh_bc_arg_t);
                //debug// os_printf("-- cond %p %u\n", d_pointer_diff(bc_arg, stmt->vardata), bc_arg->arg.value);

                if ( ((bc_arg->arg.value) && (bc_oper_ptr->optype != SH_OPER_ELSE))
                     || ((!bc_arg->arg.value) && (bc_oper_ptr->optype == SH_OPER_ELSE))) 
                {
	            arg_count_t     count = bc_oper_ptr->arg_count + (opdesc->result ? 1 : 0) - 2;
                    arg_count_t     idx;
                    for (idx = 0; idx < count; idx++) {
                        d_sh_check_error (stmt_eval_popvar (stmt, &bc_ptr, &mask, &bc_arg, &arg_type));
                    }
                }
                else {
                    //debug// os_printf("-- jump %u\n", jmp_arg->arg.value);
                    bc_ptr = stmt->vardata + jmp_arg->arg.value;
                }
            }
	    break;
	default:
	    if (opdesc->concat) {
	        d_sh_check_error ( stmt_eval_foper_concat(stmt, bc_oper_ptr, &bc_ptr));
	    }
	    else {
	        d_sh_check_error ( stmt_eval_foper(stmt, bc_oper_ptr, &bc_ptr));
	    }
	    break;
	}
    }

    return SH_ERR_SUCCESS;
}

LOCAL void    ICACHE_FLASH_ATTR
fn_sysdate (sh_bc_arg_t * ret_arg, const arg_count_t arg_count, sh_bc_arg_type_t arg_type[], sh_bc_arg_t * bc_args[]) 
{
    ret_arg->arg.value = lt_time (NULL);
}

LOCAL void    ICACHE_FLASH_ATTR
fn_sysctime (sh_bc_arg_t * ret_arg, const arg_count_t arg_count, sh_bc_arg_type_t arg_type[], sh_bc_arg_t * bc_args[]) 
{
    ret_arg->arg.value = lt_ctime ();
}

LOCAL void    ICACHE_FLASH_ATTR
fn_print (sh_bc_arg_t * ret_arg, const arg_count_t arg_count, sh_bc_arg_type_t arg_type[], sh_bc_arg_t * bc_args[]) 
{
   char            buffer[80];
   char           *buf_ptr = buffer;
   arg_count_t i;
   for (i = 0; i < arg_count; i++) {
        if (i > 0) {
	    buf_ptr += os_sprintf (buf_ptr, ", ");
        }
       switch (arg_type[i]) {
       case SH_BC_ARG_CHAR:
           buf_ptr += os_sprintf (buf_ptr, "\"%s\"", bc_args[i]->data);
           break;
       case SH_BC_ARG_INT:
           buf_ptr += os_sprintf (buf_ptr, "%u", bc_args[i]->arg.value);
           break;
       default:
           break;
       }
   }

   d_log_iprintf (LSH_SERVICE_NAME, "out: %s", buffer);
}

typedef struct sh_find_ctx_s {
    char             *stmt_name;
    sh_stmt_t        *stmt;
} sh_find_ctx_t;

/*
 * [private] imdb forall callback
 */
LOCAL imdb_errcode_t ICACHE_FLASH_ATTR
sh_forall_find (void *ptr, void *data)
{
    sh_stmt_t *stmt = d_pointer_as (sh_stmt_t, ptr);
    sh_find_ctx_t *find_ctx = d_pointer_as (sh_find_ctx_t, data);
    if (os_strncmp (stmt->info.name, find_ctx->stmt_name, sizeof (sh_stmt_name_t)) == 0) {
	find_ctx->stmt = stmt;
	return IMDB_CURSOR_BREAK;
    }
    return IMDB_ERR_SUCCESS;
}

/*
 * [public] find statement by name
 * - stmt_name: statement name (safe to use char* and sh_stmt_name_t*)
 * - hstmt: result handler to statement
 * - return: the pointer on function entry
 */
sh_errcode_t    ICACHE_FLASH_ATTR
stmt_get (char * stmt_name, sh_hndlr_t * hstmt)
{
    d_check_init();

    sh_find_ctx_t find_ctx;
    os_memset (&find_ctx, 0, sizeof (sh_find_ctx_t));
    find_ctx.stmt_name = stmt_name;

    d_sh_check_imdb_error (imdb_class_forall (sdata->svcres->hmdb, sdata->hstmt, (void *) &find_ctx, sh_forall_find));

    *hstmt = d_obj2hndlr (find_ctx.stmt);
    return (*hstmt) ? SH_ERR_SUCCESS : SH_STMT_NOT_EXISTS;
}

sh_errcode_t    ICACHE_FLASH_ATTR
stmt_get2 (sh_stmt_name_t * stmt_name, sh_hndlr_t * hstmt) 
{
    return stmt_get( (char *) stmt_name, hstmt);
}

LOCAL sh_errcode_t ICACHE_FLASH_ATTR
sh_on_msg_stmt_add (dtlv_ctx_t * msg_in, dtlv_ctx_t * msg_out)
{
    if (!msg_in)
        return SVCS_INVALID_MESSAGE;

    char           *stmt_name = NULL;
    char           *stmt_text = NULL;

    dtlv_seq_decode_begin (msg_in, LSH_SERVICE_ID);
    dtlv_seq_decode_ptr (SH_AVP_STMT_NAME, stmt_name, char);
    dtlv_seq_decode_ptr (SH_AVP_STMT_TEXT, stmt_text, char);
    dtlv_seq_decode_end (msg_in);

    if (!stmt_name || !stmt_text)
	return SVCS_INVALID_MESSAGE;


    sh_hndlr_t       hstmt;

    reset_last_error ();
    sh_errcode_t     res = stmt_get ( (char *)stmt_name, &hstmt);
    if (res == SH_ERR_SUCCESS) {
	d_log_wprintf (LSH_SERVICE_NAME, sz_sh_error[SH_STMT_EXISTS], stmt_name);
    }
    else {
        res = stmt_parse (stmt_text, stmt_name, &hstmt);
    }

    if (res == SH_ERR_SUCCESS)
        d_log_iprintf (LSH_SERVICE_NAME, "add \"%s\"", stmt_name);
    else
        d_svcs_check_svcs_error (encode_service_result_ext (msg_out, res));

    return SVCS_ERR_SUCCESS;
}

LOCAL sh_errcode_t ICACHE_FLASH_ATTR
sh_on_msg_stmt_remove (dtlv_ctx_t * msg_in, dtlv_ctx_t * msg_out)
{
    if (!msg_in)
        return SVCS_INVALID_MESSAGE;

    sh_stmt_name_t *stmt_name = NULL;

    dtlv_seq_decode_begin (msg_in, LSH_SERVICE_ID);
    dtlv_seq_decode_ptr (SH_AVP_STMT_NAME, stmt_name, sh_stmt_name_t);
    dtlv_seq_decode_end (msg_in);

    if (!stmt_name)
	return SVCS_INVALID_MESSAGE;

    sh_hndlr_t       hstmt;

    reset_last_error ();
    sh_errcode_t     res = stmt_get ( (char *)stmt_name, &hstmt);
    if (res == SH_STMT_NOT_EXISTS) {
	d_log_wprintf (LSH_SERVICE_NAME, sz_sh_error[SH_STMT_NOT_EXISTS], stmt_name);
    } else if (res == SH_ERR_SUCCESS) {
        res = stmt_free (hstmt);
        d_log_iprintf (LSH_SERVICE_NAME, "remove \"%s\"", stmt_name);
    }

    if (res != SH_ERR_SUCCESS)
        d_svcs_check_svcs_error (encode_service_result_ext (msg_out, res));

    return SVCS_ERR_SUCCESS;
}

LOCAL svcs_errcode_t ICACHE_FLASH_ATTR
sh_on_msg_stmt_dump (dtlv_ctx_t * msg_in, dtlv_ctx_t * msg_out)
{
    if (!msg_in)
        return SVCS_INVALID_MESSAGE;

    char       *stmt_name = NULL;

    dtlv_seq_decode_begin (msg_in, LSH_SERVICE_ID);
    dtlv_seq_decode_ptr (SH_AVP_STMT_NAME, stmt_name, char);
    dtlv_seq_decode_end (msg_in);

    if (!stmt_name)
	return SVCS_INVALID_MESSAGE;

    sh_hndlr_t  hstmt;
    dtlv_avp_t *avp;

    char       *bufptr = d_ctx_next_avp_data_ptr (msg_out);

    reset_last_error ();
    sh_errcode_t res = stmt_get (stmt_name, &hstmt);
    if (res == SH_ERR_SUCCESS)
        res = stmt_dump (hstmt, bufptr, d_avp_data_length (d_ctx_length_left (msg_out)) - 128, true);

    switch (res) {
    case SH_ERR_SUCCESS:
        d_svcs_check_dtlv_error (dtlv_avp_encode (msg_out, 0, SH_AVP_STMT_CODE, DTLV_TYPE_CHAR, os_strlen(bufptr) + 1, false, &avp));
        break;
    case SH_STMT_NOT_EXISTS:
	d_log_wprintf (LSH_SERVICE_NAME, sz_sh_error[SH_STMT_NOT_EXISTS], stmt_name);
    default:	
        d_svcs_check_svcs_error (encode_service_result_ext (msg_out, res));
    }

    return SVCS_ERR_SUCCESS;
}

LOCAL sh_errcode_t ICACHE_FLASH_ATTR
sh_on_msg_stmt_run (dtlv_ctx_t * msg_in, dtlv_ctx_t * msg_out)
{
    if (!msg_in)
        return SVCS_INVALID_MESSAGE;

    sh_stmt_name_t *stmt_name = NULL;

    dtlv_seq_decode_begin (msg_in, LSH_SERVICE_ID);
    dtlv_seq_decode_ptr (SH_AVP_STMT_NAME, stmt_name, sh_stmt_name_t);
    dtlv_seq_decode_end (msg_in);

    if (!stmt_name)
	return SVCS_INVALID_MESSAGE;

    sh_hndlr_t       hstmt;

    reset_last_error ();
    sh_errcode_t     res = stmt_get ( (char *)stmt_name, &hstmt);
    if (res == SH_STMT_NOT_EXISTS) {
	d_log_wprintf (LSH_SERVICE_NAME, sz_sh_error[SH_STMT_NOT_EXISTS], stmt_name);
    } else if (res == SH_ERR_SUCCESS) {
        sh_eval_ctx_t evctx;
        os_memset (&evctx, 0, sizeof (sh_eval_ctx_t));
        res = stmt_eval (hstmt, &evctx);
    }

    if (res != SH_ERR_SUCCESS)
        d_svcs_check_svcs_error (encode_service_result_ext (msg_out, res));

    return SVCS_ERR_SUCCESS;
}

LOCAL svcs_errcode_t ICACHE_FLASH_ATTR
sh_on_msg_info (dtlv_ctx_t * msg_out)
{
    dtlv_avp_t     *gavp;

    d_svcs_check_dtlv_error ( dtlv_avp_encode_list (msg_out, 0, SH_AVP_STATEMENT, DTLV_TYPE_OBJECT, &gavp));
    imdb_hndlr_t    hcur;
    d_svcs_check_imdb_error (imdb_class_query (sdata->svcres->hmdb, sdata->hstmt, PATH_NONE, &hcur));

    void           *entries[10];
    uint16          rowcount;
    d_svcs_check_imdb_error (imdb_class_fetch (hcur, 10, &rowcount, entries));

    bool            fcont = true;
    while (rowcount && fcont) {
	int             i;
	for (i = 0; i < rowcount; i++) {
            sh_stmt_t      *stmt = d_pointer_as(sh_stmt_t, entries[i]);
	    dtlv_avp_t     *gavp_in;
	    d_svcs_check_dtlv_error (dtlv_avp_encode_grouping (msg_out, 0, SH_AVP_STATEMENT, &gavp_in)
				     || dtlv_avp_encode_nchar (msg_out, SH_AVP_STMT_NAME, sizeof (sh_stmt_name_t), stmt->info.name)
				     || dtlv_avp_encode_uint16 (msg_out, SH_AVP_STMT_OBJSIZE, stmt->info.length)
				     || dtlv_avp_encode_uint32 (msg_out, SH_AVP_STMT_PARSE_TIME, lt_time (&stmt->info.parse_time))
				     || dtlv_avp_encode_group_done (msg_out, gavp_in));
	}

        d_svcs_check_imdb_error (imdb_class_fetch (hcur, 10, &rowcount, entries));
    }
    imdb_class_close (hcur);
    d_svcs_check_imdb_error (dtlv_avp_encode_group_done (msg_out, gavp));


    d_svcs_check_dtlv_error (dtlv_avp_encode_list (msg_out, 0, SH_AVP_FUNCTION_NAME, DTLV_TYPE_CHAR, &gavp));
    d_svcs_check_imdb_error (imdb_class_query (sdata->svcres->hmdb, sdata->hfunc, PATH_NONE, &hcur));

    d_svcs_check_imdb_error (imdb_class_fetch (hcur, 10, &rowcount, entries));
    fcont = true;
    while (rowcount && fcont) {
	int             i;
	for (i = 0; i < rowcount; i++) {
            sh_func_entry_t *func = d_pointer_as(sh_func_entry_t, entries[i]);
	    d_svcs_check_dtlv_error (dtlv_avp_encode_nchar (msg_out, SH_AVP_FUNCTION_NAME, sizeof (sh_func_name_t), func->func_name));
	}

        d_svcs_check_imdb_error (imdb_class_fetch (hcur, 10, &rowcount, entries));
    }
    imdb_class_close (hcur);
    d_svcs_check_imdb_error (dtlv_avp_encode_group_done (msg_out, gavp));

    return SVCS_ERR_SUCCESS;
}


svcs_errcode_t  ICACHE_FLASH_ATTR
lsh_on_message (service_ident_t orig_id,
		   service_msgtype_t msgtype, void *ctxdata, dtlv_ctx_t * msg_in, dtlv_ctx_t * msg_out)
{
    svcs_errcode_t  res = SVCS_ERR_SUCCESS;
    switch (msgtype) {
    case SVCS_MSGTYPE_INFO:
	res = sh_on_msg_info (msg_out);
	break;
    case SH_MSGTYPE_STMT_ADD:
        res = sh_on_msg_stmt_add (msg_in, msg_out);
        break;
    case SH_MSGTYPE_STMT_DUMP:
        res = sh_on_msg_stmt_dump (msg_in, msg_out);
        break;
    case SH_MSGTYPE_STMT_RUN:
        res = sh_on_msg_stmt_run(msg_in, msg_out);
        break;
    case SH_MSGTYPE_STMT_REMOVE:
        res = sh_on_msg_stmt_remove(msg_in, msg_out);
        break;
    default:
	res = SVCS_MSGTYPE_INVALID;
    }

    return res;
}

svcs_errcode_t  ICACHE_FLASH_ATTR
lsh_service_install (void)
{
    svcs_service_def_t sdef;
    os_memset (&sdef, 0, sizeof (svcs_service_def_t));
    sdef.enabled = true;
    sdef.on_start = lsh_on_start;
    sdef.on_message = lsh_on_message;
    sdef.on_stop = lsh_on_stop;
    return svcctl_service_install (LSH_SERVICE_ID, LSH_SERVICE_NAME, &sdef);
}

svcs_errcode_t  ICACHE_FLASH_ATTR
lsh_service_uninstall (void)
{
    return svcctl_service_uninstall (LSH_SERVICE_NAME);
}

svcs_errcode_t  ICACHE_FLASH_ATTR
lsh_on_start (const svcs_resource_t * svcres, dtlv_ctx_t * conf)
{
    if (sdata) {
	return SVCS_SERVICE_ERROR;
    }

    lsh_data_t     *tmp_sdata;
    d_svcs_check_imdb_error (imdb_clsobj_insert (sdata->svcres->hmdb, svcres->hdata, (void **) &tmp_sdata, sizeof (lsh_data_t))
	);
    os_memset (tmp_sdata, 0, sizeof (lsh_data_t));

    tmp_sdata->svcres = svcres;
    imdb_class_def_t cdef =
	{ LSH_IMDB_CLS_FUNC, false, false, false, 0, LSH_FUNC_STORAGE_PAGES, LSH_FUNC_STORAGE_PAGE_BLOCKS, sizeof (sh_func_entry_t) };
    d_svcs_check_imdb_error (imdb_class_create (svcres->hmdb, &cdef, &(tmp_sdata->hfunc))
	);

    imdb_class_def_t cdef2 =
	{ LSH_IMDB_CLS_STMT, false, true, false, 0, LSH_STMT_STORAGE_PAGES, LSH_STMT_STORAGE_PAGE_BLOCKS, sizeof (sh_stmt_t) };
    d_svcs_check_imdb_error (imdb_class_create (svcres->hmdb, &cdef2, &(tmp_sdata->hstmt))
	);

    ih_hndlr_t      varmap;
    if (ih_init8 (tmp_sdata->token_idx, LSH_TOKENIDX_BUFFER_SIZE, 16, 0, sizeof (sh_gvar_t), &varmap) !=
	IH_ERR_SUCCESS) {
	return SH_INTERNAL_ERROR;
    }

    if (svcres->hfdb) {
        imdb_class_def_t cdef3 =
	    { LSH_IMDB_CLS_STMT_SRC, false, true, false, 0, LSH_STMT_SRC_STORAGE_PAGES, LSH_STMT_STORAGE_PAGE_BLOCKS, sizeof (sh_stmt_t) };
        d_svcs_check_imdb_error (imdb_class_create (svcres->hfdb, &cdef3, &(tmp_sdata->hstmt_src))
	    );
    }

    sdata = tmp_sdata;

    // register functions
    sh_func_entry_t fn_entries[3] = {
        { LSH_SERVICE_ID, false, false, 0, "sysdate", { fn_sysdate } },
        { LSH_SERVICE_ID, false, false, 0, "sysctime", { fn_sysctime } },
        { LSH_SERVICE_ID, false, false, 0, "print", { fn_print } },
    };
    
    int i;
    for (i = 0; i < 3; i++) 
        sh_func_register (&fn_entries[i]);

    return SVCS_ERR_SUCCESS;
}

svcs_errcode_t  ICACHE_FLASH_ATTR
lsh_on_stop (void)
{
    if (!sdata) {
	return SVCS_NOT_RUN;
    }

    lsh_data_t     *tmp_sdata = sdata;
    sdata = NULL;
    d_svcs_check_imdb_error (imdb_class_destroy (tmp_sdata->svcres->hmdb, tmp_sdata->hfunc)
	);

    d_svcs_check_imdb_error (imdb_class_destroy (tmp_sdata->svcres->hmdb, tmp_sdata->hstmt)
	);

    d_svcs_check_imdb_error (imdb_clsobj_delete (tmp_sdata->svcres->hmdb, tmp_sdata->svcres->hdata, tmp_sdata)
	);

    return SVCS_ERR_SUCCESS;
}
