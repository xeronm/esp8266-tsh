/* Copyright (c) 2018 by Denis Muratov <xeronm@gmail.com>. All rights reserved

   FileName: fwupgrade.h
   Source: https://dtec.pro/gitbucket/git/esp8266/esp8266_lsh.git

   Description: OTA Firmware Upgrade high-level functions

*/

#ifndef __USER_FWUPGRADE_H__
#define __USER_FWUPGRADE_H__	1

#include "sysinit.h"
#include "core/utils.h"

#define	FWUPG_SUB_SERVICE_NAME	".fwupg"

#define FWUPG_IDLE_TIMEOUT_SEC		60	// seconds
#define FWUPG_REBOOT_TIMEOUT_SEC	1	// seconds

#define FWUPG_BIN_CHECKSUM_SIZE		1	//

typedef digest256_t firmware_digest_t;

typedef enum __packed upgrade_err_e {
    UPGRADE_ERR_SUCCESS = 0,
    UPGRADE_NOT_INIT = 1,
    UPGRADE_NOT_SUPPORTED = 2,
    UPGRADE_INVALID_STATE = 3,
    UPGRADE_INVALID_SYSFLAG = 4,
    UPGRADE_INVALID_SIZE = 5,
    UPGRADE_SIZE_OVERFLOW = 6,
    UPGRADE_READ_ERROR = 7,
    UPGRADE_WRITE_ERROR = 8,
    UPGRADE_DIGEST_ERROR = 9,
    UPGRADE_OUT_OF_MEMORY = 10,
} upgrade_err_t;

typedef enum __packed upgrade_sate_e {
    UPGRADE_NONE = 0,
    UPGRADE_ERROR = 1,
    UPGRADE_READY = 2,
    UPGRADE_UPLOADING = 3,
    UPGRADE_COMPLETE = 4,
    UPGRADE_ABORT = 5,
} upgrade_sate_t;

typedef struct firmware_info_s {
    char            product[40];
    union version {
	struct version_comp {
	    uint8           major;
	    uint8           minor;
	    uint16          patch;
	} comp;
	uint32          raw;
    } version;
    char            ver_suffix[8];
    uint32          build;
    uint16          flash_comp;	// bit-mask of flash_size_map compartibility
    uint32          release_date;
    uint32          digest_pos; // written by digest.py: digest position in binary file
    uint32          binsize;	// written by digest.py: binary size
    uint32          bindate;    // written by digest.py: bin make POSIX Timestamp
    firmware_digest_t digest;
} firmware_info_t;

#define FW_VERSTR		"%u.%u.%u%s(%u)"
#define FW_VER2STR(fwi)		(fwi)->version.comp.major, (fwi)->version.comp.minor, (fwi)->version.comp.patch, (fwi)->ver_suffix, (fwi)->build

typedef struct upgrade_info_s {
    upgrade_sate_t  state;
    uint32          fwbin_start_addr;
    uint32          fwbin_curr_addr;
} upgrade_info_t;

upgrade_err_t   fwupdate_init (firmware_info_t * fwinfo, firmware_digest_t * init_digest);
upgrade_err_t   fwupdate_upload (uint8 * data, size_t length);
upgrade_err_t   fwupdate_done (void);
upgrade_err_t   fwupdate_abort (void);

upgrade_err_t   fw_verify (firmware_info_t * fwinfo, firmware_digest_t * init_digest);

upgrade_err_t   fwupdate_info (upgrade_info_t * info);

#define d_fwupdate_check_error(ret) \
	{ \
		upgrade_err_t r = (ret); \
		if (r != UPGRADE_ERR_SUCCESS) return r; \
	}

#endif
