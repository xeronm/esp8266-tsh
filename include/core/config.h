/* Copyright (c) 2018 by Denis Muratov <xeronm@gmail.com>. All rights reserved

   FileName: config.h
   Source: https://dtec.pro/gitbucket/git/esp8266/esp8266_lsh.git

   Description: System configuration

*/

#ifndef _CONFIG_H_
#define _CONFIG_H_ 1

#define APP_PRODUCT "esp8266 Things Shell (c) 2018 dtec.pro"


#ifndef STR
#define STR_HELPER(x)	#x
#define STR(x)	STR_HELPER(x)
#endif

#define APP_VERSION_MAJOR	0
#define APP_VERSION_MINOR	1
#define APP_VERSION_PATCH	0
#define APP_VERSION_SUFFIX	"-dev"

#define BUILD_NUMBER		582

#define APP_VERSION_RELEASE_DATE	1541349748

#define APP_INIT_DIGEST		"FF0000000000000000000000000000FF"

#ifndef APP_VERSION
#define APP_VERSION	STR(APP_VERSION_MAJOR) "." STR(APP_VERSION_MINOR) "." STR(APP_VERSION_PATCH) APP_VERSION_SUFFIX "(" STR(BUILD_NUMBER) ")"
#endif

#endif
