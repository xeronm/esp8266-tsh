/* Copyright (c) 2018 by Denis Muratov <xeronm@gmail.com>. All rights reserved

   FileName: system.h
   Source: https://dtec.pro/gitbucket/git/esp8266/esp8266_lsh.git

   Description: System entry points

*/

#ifndef _SYSTEM_H_
#define _SYSTEM_H_ 1

#include "system/imdb.h"

#define SYSTEM_IMDB_BLOCK_SIZE 1024

void            system_init (void);
void            system_shutdown (void);

imdb_hndlr_t    get_hmdb (void);

uint8           system_get_default_secret (unsigned char *buf, uint8 len);

#endif /* _SYSTEM_H */
