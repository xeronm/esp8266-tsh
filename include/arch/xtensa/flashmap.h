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
    uint32          user2;
    uint32          bin_max;
} flash_ota_map_t;

flash_ota_map_t flash_ota_map[] = {
    {0x0, 0x1000, 0x041000, 0x03B000},	// 4Mbit 256+256
    {0x0, 0x0, 0x0, 0x0},	// not supported
    {0x0, 0x1000, 0x081000, 0x07B000},	// 8Mbit 512+512
    {0x0, 0x1000, 0x081000, 0x07B000},	// 16Mbit 512+512
    {0x0, 0x1000, 0x081000, 0x07B000},	// 32Mbit 512+512
    {0x0, 0x1000, 0x101000, 0x0FB000},	// 16Mbit 1024+1024
    {0x0, 0x1000, 0x101000, 0x0FB000},	// 32Mbit 1024+1024
    {0x0, 0x0, 0x0, 0x0},	// not supported
    {0x0, 0x0, 0x0, 0x0},	// not supported
    {0x0, 0x0, 0x0, 0x0},	// not supported
};

#endif
