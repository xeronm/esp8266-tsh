/* 
 * ESP8266 OTA Firmware Upgrade High-Level functions
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

#include "sysinit.h"
#include "fwupgrade.h"
#include "flashmap.h"
#include "core/utils.h"
#include "core/logging.h"
#include "core/system.h"
#include "crypto/sha.h"

#include "upgrade.h"

typedef struct upgrade_data_s {
    upgrade_sate_t  state;
    uint16          curr_erased_sec;
    uint32          fwbin_start_addr;	// upload start address
    uint32          fwbin_curr_addr;	// last written address
    uint16          buffer_pos;	// buffer position
    os_timer_t      tx_timer;
    SHA256Context   sha256;
    firmware_info_t fwinfo;
    firmware_digest_t init_digest;
    uint8           buffer[SPI_FLASH_SEC_SIZE];
} upgrade_data_t;

LOCAL upgrade_data_t *sdata = NULL;


LOCAL const char *sz_upgrade_error[] ICACHE_RODATA_ATTR = {
    "",
    "not initialized",
    "OTA not supported, fwaddr:0x%06x",
    "%s invalid state:%u",
    "invalid system flag:%u, must:%u",
    "invalid binsize:%u",
    "binsize overflow:%u",
    "read error at:0x%06x",
    "write error at:0x%06x",
    "digest error",
    "out of memory",
};


LOCAL void      ICACHE_FLASH_ATTR
firmware_flash_done (bool fabort)
{
    d_assert (sdata, "sdata is null");

    os_timer_disarm (&sdata->tx_timer);

    if (fabort) {
	system_upgrade_flag_set (UPGRADE_FLAG_IDLE);
	sdata->state = UPGRADE_ABORT;

	d_log_wprintf (MAIN_SERVICE_NAME FWUPG_SUB_SERVICE_NAME, "aborted, addr:0x%06x, wrlen:%u",
		       sdata->fwbin_start_addr, sdata->fwbin_curr_addr - sdata->fwbin_start_addr);
    }
    else {
	system_upgrade_flag_set (UPGRADE_FLAG_FINISH);
	sdata->state = UPGRADE_COMPLETE;

	d_log_wprintf (MAIN_SERVICE_NAME FWUPG_SUB_SERVICE_NAME, "comited, addr:0x%06x, wrlen:%u",
		       sdata->fwbin_start_addr, sdata->fwbin_curr_addr - sdata->fwbin_start_addr);
    }

    os_timer_arm (&sdata->tx_timer, FWUPG_REBOOT_TIMEOUT_SEC * MSEC_PER_SEC, false);
}

LOCAL bool      ICACHE_FLASH_ATTR
check_system_upgrade_flag (uint8 flag)
{
    uint8           upgrd_flag = system_upgrade_flag_check ();
    if (flag != upgrd_flag) {
	d_log_eprintf (MAIN_SERVICE_NAME FWUPG_SUB_SERVICE_NAME, sz_upgrade_error[UPGRADE_INVALID_SYSFLAG], upgrd_flag,
		       flag);
	return false;
    }

    return true;
}

LOCAL void      ICACHE_FLASH_ATTR
buffer_swap_digest (size_t position, uint8 * buffer, size_t length, size_t digest_pos, uint8 * init_digest)
{
    // buffer includes start of digest
    size_t          offset = 0;
    if ((position < digest_pos) && (position + length >= digest_pos)) {
	offset = digest_pos - position;
	os_memcpy (buffer + offset, init_digest, MIN (sizeof (firmware_digest_t), length - offset));
    }
    // buffer includes end of digest
    else if ((position < digest_pos + sizeof (firmware_digest_t))
	     && (position + length >= digest_pos + sizeof (firmware_digest_t))) {
	offset = position - digest_pos;
	os_memcpy (buffer, init_digest + offset, sizeof (firmware_digest_t) - offset);
    }
}

LOCAL upgrade_err_t ICACHE_FLASH_ATTR
upgrade_flush_buffer (void)
{
    uint16          sec = sdata->fwbin_curr_addr / SPI_FLASH_SEC_SIZE;
    SpiFlashOpResult fres = SPI_FLASH_RESULT_OK;
    while ((sec > sdata->curr_erased_sec)
	   && (sec <= (sdata->fwbin_curr_addr + sdata->buffer_pos - 1) / SPI_FLASH_SEC_SIZE) && !fres) {
	sdata->curr_erased_sec = sec;
	fres = spi_flash_erase_sector (sec);
	system_soft_wdt_feed ();
	sec++;
    }

    if (fres || spi_flash_write (sdata->fwbin_curr_addr, (uint32 *) sdata->buffer, sdata->buffer_pos)) {
	d_log_eprintf (MAIN_SERVICE_NAME FWUPG_SUB_SERVICE_NAME, sz_upgrade_error[UPGRADE_WRITE_ERROR],
		       sdata->fwbin_curr_addr);
	sdata->state = UPGRADE_ERROR;
	return UPGRADE_WRITE_ERROR;
    }

    size_t          last_pos = sdata->fwbin_curr_addr - sdata->fwbin_start_addr;
    buffer_swap_digest (last_pos, sdata->buffer, sdata->buffer_pos, sdata->fwinfo.digest_pos, sdata->init_digest);

    if (SHA256Input
	(&sdata->sha256, sdata->buffer,
	 MIN (sdata->buffer_pos, sdata->fwinfo.binsize - FWUPG_BIN_CHECKSUM_SIZE - last_pos))) {
	sdata->state = UPGRADE_ERROR;
	return UPGRADE_DIGEST_ERROR;
    }

    sdata->fwbin_curr_addr += sdata->buffer_pos;
    sdata->buffer_pos = 0;

    return UPGRADE_ERR_SUCCESS;
}


LOCAL void      ICACHE_FLASH_ATTR
fwupdate_timeout (void *args)
{
    d_assert (sdata, "sdata is null");

    switch (sdata->state) {
    case UPGRADE_COMPLETE:
	if (check_system_upgrade_flag (UPGRADE_FLAG_FINISH)) {
	    d_log_iprintf (MAIN_SERVICE_NAME FWUPG_SUB_SERVICE_NAME, "complete, reboot...");
            system_shutdown ();
	    system_upgrade_reboot ();
	}
	else {
	    firmware_flash_done (true);
	}
	break;
    case UPGRADE_ABORT:
	st_free (sdata);
	break;
    case UPGRADE_READY:
    case UPGRADE_UPLOADING:
	d_log_wprintf (MAIN_SERVICE_NAME FWUPG_SUB_SERVICE_NAME, "idle timeout");
	firmware_flash_done (true);
	break;
    default:
	d_log_wprintf (MAIN_SERVICE_NAME FWUPG_SUB_SERVICE_NAME, sz_upgrade_error[UPGRADE_INVALID_STATE], "timeout",
		       sdata->state);
	firmware_flash_done (true);
    }
}

LOCAL upgrade_err_t ICACHE_FLASH_ATTR
firmware_flash_init (uint32 start_addr, firmware_info_t * fwinfo, firmware_digest_t * init_digest)
{
    d_assert (!sdata, "sdata not null");

    st_zalloc (sdata, upgrade_data_t);
    if (!sdata)
	return UPGRADE_OUT_OF_MEMORY;

    sdata->fwbin_start_addr = start_addr;
    sdata->fwbin_curr_addr = start_addr;
    sdata->buffer_pos = 0;

    os_memcpy (&sdata->fwinfo, fwinfo, sizeof (firmware_info_t));
    os_memcpy (&sdata->init_digest, init_digest, sizeof (firmware_digest_t));

    SHA256Reset (&sdata->sha256);

    os_timer_disarm (&sdata->tx_timer);
    os_timer_setfn (&sdata->tx_timer, fwupdate_timeout, NULL);
    os_timer_arm (&sdata->tx_timer, FWUPG_IDLE_TIMEOUT_SEC * MSEC_PER_SEC, false);

    sdata->state = UPGRADE_READY;
    system_upgrade_flag_set (UPGRADE_FLAG_START);

    d_log_wprintf (MAIN_SERVICE_NAME FWUPG_SUB_SERVICE_NAME, "start addr:0x%06x, sec_size:%u", start_addr,
		   SPI_FLASH_SEC_SIZE);

    return UPGRADE_ERR_SUCCESS;
}

upgrade_err_t   ICACHE_FLASH_ATTR
fwupdate_init (firmware_info_t * fwinfo, firmware_digest_t * init_digest)
{
    size_t          binlen = fwinfo->binsize;
    d_log_wprintf (MAIN_SERVICE_NAME FWUPG_SUB_SERVICE_NAME, "update init version:" FW_VERSTR ", size:%u",
		   FW_VER2STR (fwinfo), binlen);

    flash_ota_map_t *fwmap = &flash_ota_map[system_get_flash_size_map ()];
    if (fwinfo->binsize < SPI_FLASH_SEC_SIZE) {
	d_log_eprintf (MAIN_SERVICE_NAME FWUPG_SUB_SERVICE_NAME, sz_upgrade_error[UPGRADE_INVALID_SIZE], binlen);
	return UPGRADE_INVALID_SIZE;
    }

    if ((fwinfo->binsize == 0) || (fwmap->bin_max < binlen)) {
	d_log_eprintf (MAIN_SERVICE_NAME FWUPG_SUB_SERVICE_NAME, sz_upgrade_error[UPGRADE_SIZE_OVERFLOW],
		       fwmap->bin_max - binlen);
	return UPGRADE_SIZE_OVERFLOW;
    }

    uint32          addr = system_get_userbin_addr ();
    if ((addr != fwmap->user1) && (addr != fwmap->user2)) {
	d_log_wprintf (MAIN_SERVICE_NAME FWUPG_SUB_SERVICE_NAME, sz_upgrade_error[UPGRADE_NOT_SUPPORTED], addr);
	return UPGRADE_NOT_SUPPORTED;
    }

    if (sdata) {
	d_log_eprintf (MAIN_SERVICE_NAME FWUPG_SUB_SERVICE_NAME, sz_upgrade_error[UPGRADE_INVALID_STATE], "init",
		       sdata->state);
	return UPGRADE_INVALID_STATE;
    }
    if (!check_system_upgrade_flag (UPGRADE_FLAG_IDLE))
	return UPGRADE_INVALID_STATE;

    uint8           fwbin = system_upgrade_userbin_check ();
    uint32          fwaddr;
    if (fwbin == UPGRADE_FW_BIN1) {
	fwbin = UPGRADE_FW_BIN2;
	fwaddr = fwmap->user2;
    }
    else {
	fwbin = UPGRADE_FW_BIN1;
	fwaddr = fwmap->user1;
    }

    return firmware_flash_init (fwaddr, fwinfo, init_digest);
}

upgrade_err_t   ICACHE_FLASH_ATTR
fwupdate_upload (uint8 * data, size_t length)
{
    if (!sdata) {
	d_log_eprintf (MAIN_SERVICE_NAME FWUPG_SUB_SERVICE_NAME, sz_upgrade_error[UPGRADE_NOT_INIT]);
	return UPGRADE_NOT_INIT;
    }
    if ((sdata->state != UPGRADE_READY) && (sdata->state != UPGRADE_UPLOADING)) {
	d_log_wprintf (MAIN_SERVICE_NAME FWUPG_SUB_SERVICE_NAME, sz_upgrade_error[UPGRADE_INVALID_STATE], "upload",
		       sdata->state);
	return UPGRADE_INVALID_STATE;
    }
    if (!check_system_upgrade_flag (UPGRADE_FLAG_START))
	return UPGRADE_INVALID_STATE;

    os_timer_disarm (&sdata->tx_timer);
    os_timer_arm (&sdata->tx_timer, FWUPG_IDLE_TIMEOUT_SEC * MSEC_PER_SEC, false);

    size_t          csize = sdata->fwbin_curr_addr - sdata->fwbin_start_addr + sdata->buffer_pos;
    if (csize + length > sdata->fwinfo.binsize) {
	d_log_eprintf (MAIN_SERVICE_NAME FWUPG_SUB_SERVICE_NAME, "bin overflow:%u",
		       csize + length - sdata->fwinfo.binsize);
	firmware_flash_done (true);
	return UPGRADE_SIZE_OVERFLOW;
    }

    if (sdata->state == UPGRADE_READY)
	sdata->state = UPGRADE_UPLOADING;

    uint8          *data_ptr = data;
    size_t          data_left = length;
    while (data_left > 0) {
	size_t          part_len = MIN (SPI_FLASH_SEC_SIZE - sdata->buffer_pos, data_left);
	// TODO: Not effective when length = N*SPI_FLASH_SEC_SIZE
	os_memcpy (&sdata->buffer[sdata->buffer_pos], data_ptr, part_len);
	data_left -= part_len;
	sdata->buffer_pos += part_len;

	if (sdata->buffer_pos == SPI_FLASH_SEC_SIZE) {
	    d_fwupdate_check_error (upgrade_flush_buffer ());
	}
    }

    // last data part
    if ((sdata->fwinfo.binsize == sdata->fwbin_curr_addr - sdata->fwbin_start_addr + sdata->buffer_pos)
	&& sdata->buffer_pos) {
	d_fwupdate_check_error (upgrade_flush_buffer ());
    }

    return UPGRADE_ERR_SUCCESS;
}

upgrade_err_t   ICACHE_FLASH_ATTR
fwupdate_done (void)
{
    if (!sdata) {
	d_log_eprintf (MAIN_SERVICE_NAME FWUPG_SUB_SERVICE_NAME, sz_upgrade_error[UPGRADE_NOT_INIT]);
	return UPGRADE_NOT_INIT;
    }
    if (sdata->state != UPGRADE_UPLOADING) {
	d_log_wprintf (MAIN_SERVICE_NAME FWUPG_SUB_SERVICE_NAME, sz_upgrade_error[UPGRADE_INVALID_STATE], "done",
		       sdata->state);
	return UPGRADE_INVALID_STATE;
    }
    if (!check_system_upgrade_flag (UPGRADE_FLAG_START))
	return UPGRADE_INVALID_STATE;

    os_timer_disarm (&sdata->tx_timer);

    firmware_digest_t digest;
    if (SHA256Result (&sdata->sha256, digest)) {
	d_log_eprintf (MAIN_SERVICE_NAME FWUPG_SUB_SERVICE_NAME, sz_upgrade_error[UPGRADE_DIGEST_ERROR]);
	firmware_flash_done (true);
	return UPGRADE_DIGEST_ERROR;
    }

    d_log_wbprintf (MAIN_SERVICE_NAME FWUPG_SUB_SERVICE_NAME, (char *) digest, sizeof (firmware_digest_t),
		    "update_done addr:0x%06x, size:%u, digest:", sdata->fwbin_start_addr,
		    sdata->fwbin_curr_addr - sdata->fwbin_start_addr);
    if (os_memcmp (sdata->fwinfo.digest, digest, sizeof (firmware_digest_t)) != 0) {
	d_log_eprintf (MAIN_SERVICE_NAME FWUPG_SUB_SERVICE_NAME, sz_upgrade_error[UPGRADE_DIGEST_ERROR]);
	firmware_flash_done (true);
	return UPGRADE_DIGEST_ERROR;
    }

    firmware_flash_done (false);

    return UPGRADE_ERR_SUCCESS;
}

upgrade_err_t   ICACHE_FLASH_ATTR
fwupdate_info (upgrade_info_t * info)
{
    if (!sdata) {
	d_log_dprintf (MAIN_SERVICE_NAME FWUPG_SUB_SERVICE_NAME, sz_upgrade_error[UPGRADE_NOT_INIT]);
	return UPGRADE_NOT_INIT;
    }

    os_memset (info, 0, sizeof (upgrade_info_t));
    info->state = sdata->state;
    info->fwbin_start_addr = sdata->fwbin_start_addr;
    info->fwbin_curr_addr = sdata->fwbin_curr_addr;

    return UPGRADE_ERR_SUCCESS;
}

upgrade_err_t   ICACHE_FLASH_ATTR
fw_verify (firmware_info_t * fwinfo, firmware_digest_t * init_digest)
{
    uint32          fw_addr = system_get_userbin_addr ();
    flash_ota_map_t *fwmap = &flash_ota_map[system_get_flash_size_map ()];

    if ((fw_addr != fwmap->user1) && (fw_addr != fwmap->user2)) {
	d_log_wprintf (MAIN_SERVICE_NAME FWUPG_SUB_SERVICE_NAME, sz_upgrade_error[UPGRADE_NOT_SUPPORTED], fw_addr);
	return UPGRADE_NOT_SUPPORTED;
    }

    if (fwinfo->binsize > fwmap->bin_max)
	return UPGRADE_SIZE_OVERFLOW;

    uint8          *buffer = os_malloc (SPI_FLASH_SEC_SIZE);
    if (!buffer)
	return UPGRADE_OUT_OF_MEMORY;

    upgrade_err_t   res = UPGRADE_ERR_SUCCESS;

    SHA256Context   sha256;
    SHA256Reset (&sha256);

    uint32          addr = fw_addr;
    size_t          read_len = 0;
    while (read_len < fwinfo->binsize - FWUPG_BIN_CHECKSUM_SIZE) {
	if (spi_flash_read (addr, (uint32 *) buffer, SPI_FLASH_SEC_SIZE)) {
	    d_log_eprintf (MAIN_SERVICE_NAME FWUPG_SUB_SERVICE_NAME, sz_upgrade_error[UPGRADE_READ_ERROR], addr);
	    res = UPGRADE_READ_ERROR;
	    goto validate_final;
	}

	addr += SPI_FLASH_SEC_SIZE;

	buffer_swap_digest (read_len, buffer, SPI_FLASH_SEC_SIZE, fwinfo->digest_pos, init_digest);

	if (SHA256Input
	    (&sha256, (uint8 *) buffer,
	     MIN (SPI_FLASH_SEC_SIZE, fwinfo->binsize - FWUPG_BIN_CHECKSUM_SIZE - read_len))) {
	    res = UPGRADE_DIGEST_ERROR;
	    goto validate_final;
	}
	system_soft_wdt_feed ();
	read_len += SPI_FLASH_SEC_SIZE;
    }

    firmware_digest_t digest;
    SHA256Result (&sha256, digest);

    if (os_memcmp (digest, fwinfo->digest, sizeof (firmware_digest_t)) != 0) {
	d_log_ebprintf (MAIN_SERVICE_NAME FWUPG_SUB_SERVICE_NAME, (char *) digest, sizeof (firmware_digest_t),
			"fw_verify addr:0x%06x, size:%u, digest:", fw_addr, fwinfo->binsize);
	res = UPGRADE_DIGEST_ERROR;
    }

  validate_final:
    os_free (buffer);
    return res;
}
