/* 
 * ESP8266 System entry points
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

#include "sysinit.h"
#include "core/system.h"
#include "core/logging.h"
#include "core/config.h"
#include "system/imdb.h"
#include "system/services.h"
#include "service/syslog.h"
#include "service/udpctl.h"
#include "service/ntp.h"
#include "service/lsh.h"
#include "service/sched.h"
#ifdef ARCH_XTENSA
#include "uart.h"
#include "service/espadmin.h"
#include "service/gpioctl.h"
#include "service/device/dhtxx.h"
#endif

imdb_hndlr_t    hmdb = 0;
imdb_hndlr_t    hfdb = 0;
os_timer_t      timer_overflow;

#define STARTUP_SERVICE_NAME	"startup"
#define SHUTDOWN_SERVICE_NAME	"shutdown"

typedef         svcs_errcode_t (*service_install) (void);

typedef struct reg_service_s {
    service_install fn_install;
    service_name_t  name;
} reg_service_t;

#ifdef ARCH_XTENSA
#define REG_SERVICE_MAX	8
#else
#define REG_SERVICE_MAX	5
#endif
LOCAL reg_service_t const reg_services[REG_SERVICE_MAX] ICACHE_RODATA_ATTR = {
    {syslog_service_install, SYSLOG_SERVICE_NAME},
    {lsh_service_install, LSH_SERVICE_NAME},
    {sched_service_install, SCHED_SERVICE_NAME},
#ifdef ARCH_XTENSA
    {espadmin_service_install, ESPADMIN_SERVICE_NAME},
    {gpio_service_install, GPIO_SERVICE_NAME},
#endif
    {udpctl_service_install, UDPCTL_SERVICE_NAME},
    {ntp_service_install, NTP_SERVICE_NAME},
#ifdef ARCH_XTENSA
    {dht_service_install, DHT_SERVICE_NAME},
#endif
};

LOCAL void      ICACHE_FLASH_ATTR
time_overflow_timeout (void *args)
{
    lt_timestamp_t  ts;
    lt_get_ctime (&ts);
}

LOCAL void      ICACHE_FLASH_ATTR
time_overflow_setup (void)
{
    // setup time overflow timer
    os_memset (&timer_overflow, 0, sizeof (os_timer_t));
    os_timer_disarm (&timer_overflow);
    os_timer_setfn (&timer_overflow, time_overflow_timeout, NULL);

    uint32          timeout_min = (0xFFFFFFFF / USEC_PER_SEC / SEC_PER_MIN / 2) + 1;
    d_log_iprintf (MAIN_SERVICE_NAME, "overflow timer:%u min", timeout_min);
    os_timer_arm (&timer_overflow, timeout_min * MSEC_PER_MIN, true);
}


LOCAL void      ICACHE_FLASH_ATTR
wifi_event_handler_cb (System_Event_t * event)
{
    if (event->event == EVENT_STAMODE_GOT_IP) {
	svcctl_service_message (0, 0, event, SVCS_MSGTYPE_NETWORK, NULL, NULL);
    }
    else if (event->event == EVENT_STAMODE_DISCONNECTED) {
	svcctl_service_message (0, 0, event, SVCS_MSGTYPE_NETWORK_LOSS, NULL, NULL);
    }
}

void            ICACHE_FLASH_ATTR
system_init (void)
{
#ifdef ENABLE_AT
    at_init ();
    atcmd_init ();
#else
    uart_init (BIT_RATE_115200, BIT_RATE_115200);
#endif
    os_printf (LINE_END LINE_END "*** system_startup ***" LINE_END);

    d_log_wprintf (STARTUP_SERVICE_NAME, "*** %s, ver:%s, sdk:%s (build:" __DATE__ " " __TIME__ ") ***", APP_PRODUCT,
		   APP_VERSION, system_get_sdk_version ());
    d_log_wprintf (STARTUP_SERVICE_NAME, "***  userbin:0x%06x, fmem:%d  ***", system_get_userbin_addr (),
		   system_get_free_heap_size ());

#ifndef DISABLE_SYSTEM
    d_log_iprintf (STARTUP_SERVICE_NAME, "imdb block_size:%u, fdb block_size:%u", SYSTEM_IMDB_BLOCK_SIZE, SYSTEM_FDB_BLOCK_SIZE);
    imdb_def_t      db_def = { SYSTEM_IMDB_BLOCK_SIZE, BLOCK_CRC_NONE, false, 0, 0 };
    imdb_init (&db_def, 0, &hmdb);

    imdb_def_t      db_def2 = { SYSTEM_FDB_BLOCK_SIZE, BLOCK_CRC_WRITE, true, 1, SYSTEM_FDB_FILE_SIZE };
    imdb_init (&db_def2, hmdb, &hfdb);

    svcctl_start (hmdb);
    // installing services
    int             i;
    for (i = 0; i < REG_SERVICE_MAX; i++) {
	reg_services[i].fn_install ();
    }
#endif

    time_overflow_setup ();

    wifi_set_event_handler_cb (wifi_event_handler_cb);

    d_log_wprintf (STARTUP_SERVICE_NAME, "done, fmem:%d", system_get_free_heap_size ());

    svcctl_service_message (0, 0, NULL, SVCS_MSGTYPE_SYSTEM_START, NULL, NULL);
}

void            ICACHE_FLASH_ATTR
system_shutdown (void)
{
    svcctl_service_message (0, 0, NULL, SVCS_MSGTYPE_SYSTEM_STOP, NULL, NULL);

#ifndef DISABLE_SYSTEM
    svcctl_stop ();

    imdb_done (hmdb);
#endif
    wifi_station_disconnect ();
    d_log_wprintf (SHUTDOWN_SERVICE_NAME, "done, fmem:%d", system_get_free_heap_size ());
    os_printf (LINE_END LINE_END "*** system_shutdown ***" LINE_END);
}

imdb_hndlr_t    ICACHE_FLASH_ATTR
get_hmdb (void)
{
    return hmdb;
}

imdb_hndlr_t    ICACHE_FLASH_ATTR
get_fdb (void)
{
    return hfdb;
}

uint8           ICACHE_FLASH_ATTR
system_get_default_secret (unsigned char *buf, uint8 len)
{
    uint8           macaddr[6];
    if (wifi_get_macaddr (STATION_IF, macaddr))
	return buf2hex ((char *) buf, (char *) &macaddr, MIN (sizeof (macaddr), len / 2));
    else
	return 0;
}
