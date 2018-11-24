/* 
 * ESP8266 Platform-Specific initialization stub
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

#include <stdio.h>
#include <errno.h>
#include "sysinit.h"

//static time_t _start_time = 0;

void           *ICACHE_FLASH_ATTR
os_zalloc (size_t size)
{
    void           *res = malloc (size);
    memset (res, 0, size);
    return res;
}

size_t          ICACHE_FLASH_ATTR
system_get_free_heap_size (void)
{
    return 0;
}

os_time_t       ICACHE_FLASH_ATTR
system_get_time (void)
{
    return 0;
}

uint32          ICACHE_FLASH_ATTR 
system_rtc_clock_cali_proc(void)
{
    return 0;
}

size_t          ICACHE_FLASH_ATTR 
fio_user_format(uint32 size)
{
    FILE * fp = fopen ("./flash_userdata.bin", "w+");
    if (!fp)
        return 0;

    fclose (fp);
    return size;
}

size_t          ICACHE_FLASH_ATTR
fio_user_read(uint32 addr, uint32 *buffer, uint32 size) 
{
    FILE * fp = fopen ("./flash_userdata.bin", "rb");
    if (!fp || fseek (fp, addr, SEEK_SET))
        return 0;
    size_t res = fread (buffer, 1, size, fp);
    fclose (fp);

    return res;
}

size_t          ICACHE_FLASH_ATTR 
fio_user_write(uint32 addr, uint32 *buffer, uint32 size)
{
    FILE * fp = fopen ("./flash_userdata.bin", "rb+");
    if ((!fp) && (errno == ENOENT))
        fp = fopen ("./flash_userdata.bin", "wb+");

    if (!fp || fseek (fp, addr, SEEK_SET))
        return 0;
    size_t res = fwrite (buffer, 1, size, fp);
    fclose (fp);

    return res;
}

size_t          ICACHE_FLASH_ATTR 
fio_user_size(void)
{
    return 2*1024*1024; // 2 Mb
}
