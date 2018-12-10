/* Copyright (c) 2018 by Denis Muratov <xeronm@gmail.com>. All rights reserved

   FileName: flashmap.h
   Source: https://dtec.pro/gitbucket/git/esp8266/esp8266_lsh.git

   Description: Flash Address Map

*/

#ifndef __USER_FLASHMAP_H__
#define __USER_FLASHMAP_H__	1

#include "sysinit.h"

typedef struct flash_ota_map_s {
    uint32          boot;
    uint32          user1;
    uint32          part1_end;
    uint32          user2;
    uint32          part2_end;
    uint32          bin_max;
} flash_ota_map_t;

#define USER_DATA_BEGIN_OFFSET	0x1000
#define USER2_DATA_END_OFFSET	0x6000

#define d_flash_user1_data_addr(map) 		((map)->user1 + (map)->bin_max + USER_DATA_BEGIN_OFFSET)
#define d_flash_user1_data_addr_end(map) 	((map)->part1_end)

#define d_flash_user2_data_addr(map) 		((map)->user2 + (map)->bin_max + USER_DATA_BEGIN_OFFSET)
#define d_flash_user2_data_addr_end(map) 	((map)->part2_end - USER2_DATA_END_OFFSET)

flash_ota_map_t *get_flash_ota_map (void);

#endif
