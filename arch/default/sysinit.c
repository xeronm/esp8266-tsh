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
