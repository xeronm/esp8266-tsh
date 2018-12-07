/* 
 * ESP8266 Things Shell System Configuration
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
 * ESP8266 Things Shell is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ESP8266 Things Shell.  If not, see <https://www.gnu.org/licenses/>.
 *
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

#define BUILD_NUMBER		683

#define APP_VERSION_RELEASE_DATE	1544027787

#define APP_INIT_DIGEST		"FF0000000000000000000000000000FF"

#ifndef APP_VERSION
#define APP_VERSION	STR(APP_VERSION_MAJOR) "." STR(APP_VERSION_MINOR) "." STR(APP_VERSION_PATCH) APP_VERSION_SUFFIX "(" STR(BUILD_NUMBER) ")"
#endif

#endif
