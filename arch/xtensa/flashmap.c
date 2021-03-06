/* Copyright (c) 2018 by Denis Muratov <xeronm@gmail.com>. All rights reserved

   FileName: flashmap.c
   Source: https://dtec.pro/gitbucket/git/esp8266/esp8266_lsh.git

   Description: Flash Address Map

*/

#include "sysinit.h"
#include "flashmap.h"

flash_ota_map_t flash_ota_map[] RODATA = {
    {0x0, 0x1000, 0x040000, 0x041000, 0x080000, 0x03B000}
    ,                           // 4Mbit 256+256
    {0x0, 0x0, 0x0, 0x0, 0x0, 0x0}
    ,                           // not supported
    {0x0, 0x1000, 0x080000, 0x081000, 0x100000, 0x07B000}
    ,                           // 8Mbit 512+512
    {0x0, 0x1000, 0x080000, 0x081000, 0x200000, 0x07B000}
    ,                           // 16Mbit 512+512
    {0x0, 0x1000, 0x080000, 0x081000, 0x400000, 0x07B000}
    ,                           // 32Mbit 512+512
    {0x0, 0x1000, 0x100000, 0x101000, 0x200000, 0x0FB000}
    ,                           // 16Mbit 1024+1024
    {0x0, 0x1000, 0x100000, 0x101000, 0x400000, 0x0FB000}
    ,                           // 32Mbit 1024+1024
    {0x0, 0x0, 0x0, 0x0, 0x0, 0x0}
    ,                           // not supported
    {0x0, 0x0, 0x0, 0x0, 0x0, 0x0}
    ,                           // not supported
    {0x0, 0x0, 0x0, 0x0, 0x0, 0x0}
    ,                           // not supported
};

LOCAL flash_ota_map_t *__fwmap = NULL;

flash_ota_map_t *ICACHE_FLASH_ATTR
get_flash_ota_map (void)
{
    if (!__fwmap)
        __fwmap = &flash_ota_map[system_get_flash_size_map ()];
    return __fwmap;
}
