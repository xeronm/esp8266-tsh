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
#include "fwupgrade.h"
#include "service/espadmin.h"
#include "service/gpioctl.h"
#include "service/device/dhtxx.h"
#endif

typedef struct system_data_s {
    imdb_hndlr_t    hmdb;
    imdb_hndlr_t    hfdb;
    char            sysdescr[SYSTEM_DESCRIPTION_LENGTH + 1];
    uint16          softap_timeout;
    bool            safe_mode;
#ifdef ARCH_XTENSA
    os_timer_t      timer_overflow;
    os_timer_t      timer_softap;
    os_timer_t      timer_safemode;
    os_event_t      task_queue[TASK_QUEUE_LENGTH];
#endif
} system_data_t;

LOCAL system_data_t *sdata = NULL;

#define STARTUP_SERVICE_NAME	"startup"
#define SHUTDOWN_SERVICE_NAME	"shutdown"

typedef         svcs_errcode_t (*service_install) (bool enabled);

typedef struct reg_service_s {
    service_install fn_install;
    service_ident_t service_id;
    service_name_t  name;
    bool            safe_mode;
} reg_service_t;

#ifdef ARCH_XTENSA
LOCAL reg_service_t const reg_services[] RODATA = {
    {syslog_service_install, SYSLOG_SERVICE_ID, SYSLOG_SERVICE_NAME, true},
    {lsh_service_install, LSH_SERVICE_ID, LSH_SERVICE_NAME, false},
    {sched_service_install, SCHED_SERVICE_ID, SCHED_SERVICE_NAME, false},
    {espadmin_service_install, ESPADMIN_SERVICE_ID, ESPADMIN_SERVICE_NAME, true},
    {gpio_service_install, GPIO_SERVICE_ID, GPIO_SERVICE_NAME, false},
    {udpctl_service_install, UDPCTL_SERVICE_ID, UDPCTL_SERVICE_NAME, true},
    {ntp_service_install, NTP_SERVICE_ID, NTP_SERVICE_NAME, false},
    {dht_service_install, DHT_SERVICE_ID, DHT_SERVICE_NAME, false},
};

#define REG_SERVICE_MAX	8
#else

LOCAL reg_service_t const reg_services[] RODATA = {
    {syslog_service_install, SYSLOG_SERVICE_ID, SYSLOG_SERVICE_NAME, true},
    {lsh_service_install, LSH_SERVICE_ID, LSH_SERVICE_NAME, false},
    {sched_service_install, SCHED_SERVICE_ID, SCHED_SERVICE_NAME, false},
    {udpctl_service_install, UDPCTL_SERVICE_ID, UDPCTL_SERVICE_NAME, true},
    {ntp_service_install, NTP_SERVICE_ID, NTP_SERVICE_NAME, false},
};

#define REG_SERVICE_MAX	5
#endif

LOCAL imdb_def_t db_def RODATA = {
    SYSTEM_IMDB_BLOCK_SIZE,
    BLOCK_CRC_NONE,
    false,
    0,
    0
};

LOCAL imdb_def_t fdb_def RODATA = {
    SYSTEM_FDB_BLOCK_SIZE,
    BLOCK_CRC_META,
    true,
    SYSTEM_FDB_CACHE_BLOCKS,
    SYSTEM_FDB_FILE_SIZE
};

LOCAL void      ICACHE_FLASH_ATTR
time_overflow_timeout (void *args)
{
    lt_timestamp_t  ts;
    lt_get_ctime (&ts);
}

LOCAL void      ICACHE_FLASH_ATTR
softap_timeout (void *args)
{
    d_log_wprintf (MAIN_SERVICE_NAME, "softap timeout");
    uint8 opmode = wifi_get_opmode ();
    wifi_set_opmode_current (opmode & ~(uint8)SOFTAP_MODE );
}

LOCAL void      ICACHE_FLASH_ATTR
safemode_timeout (void *args)
{
    d_log_wprintf (MAIN_SERVICE_NAME, "safemode timeout");

    sdata->safe_mode = false;

    svcctl_service_stop (ESPADMIN_SERVICE_ID, NULL);
    svcctl_service_start (ESPADMIN_SERVICE_ID, NULL);

    int             i;
    for (i = 0; i < REG_SERVICE_MAX; i++) {
        if (reg_services[i].safe_mode)
            continue;

        svcctl_service_start (reg_services[i].service_id, NULL);
    }
}

#ifdef ARCH_XTENSA
LOCAL void      ICACHE_FLASH_ATTR
time_overflow_setup (void)
{
    os_timer_disarm (&sdata->timer_overflow);
    os_timer_setfn (&sdata->timer_overflow, time_overflow_timeout, NULL);

    uint32          timeout_min = (0xFFFFFFFF / USEC_PER_SEC / SEC_PER_MIN / 2) + 1;
    d_log_iprintf (MAIN_SERVICE_NAME, "overflow timer:%u min", timeout_min);
    os_timer_arm (&sdata->timer_overflow, timeout_min * MSEC_PER_MIN, true);
}

void            ICACHE_FLASH_ATTR
softap_timeout_set (uint16 timeout_sec)
{
    if (!sdata)
        return;
    sdata->softap_timeout = timeout_sec;
    os_timer_disarm (&sdata->timer_softap);
    os_timer_setfn (&sdata->timer_softap, softap_timeout, NULL);

    d_log_iprintf (MAIN_SERVICE_NAME, "softap timer:%u sec", timeout_sec);
    os_timer_arm (&sdata->timer_softap, timeout_sec * MSEC_PER_SEC, false);
}
#endif

uint16          ICACHE_FLASH_ATTR
softap_timeout_get_last (void) { return sdata->softap_timeout; }

bool            ICACHE_FLASH_ATTR 
system_get_safe_mode (void) { return sdata->safe_mode; }

#ifdef ARCH_XTENSA
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
#endif

#ifdef ARCH_XTENSA
void            ICACHE_FLASH_ATTR
system_task_delayed_cb (ETSEvent * e)
{
    if (e->sig) {
        ETSTimerFunc   *func_cb = (ETSTimerFunc *) e->sig;
        func_cb ((void *) e->par);
    }
}
#endif

void            ICACHE_FLASH_ATTR
system_init (void)
{
    if (sdata)
        return;
    st_alloc (sdata, system_data_t);

#ifdef ARCH_XTENSA
    system_os_task (system_task_delayed_cb, USER_TASK_PRIO_1, (ETSEvent *) & sdata->task_queue, TASK_QUEUE_LENGTH);
#ifdef ENABLE_AT
    at_init ();
    atcmd_init ();
#else
    uart_init (BIT_RATE_115200, BIT_RATE_115200);
#endif
#endif

    os_printf (LINE_END LINE_END "*** system_startup ***" LINE_END);

#ifdef ARCH_XTENSA
    d_log_wprintf (STARTUP_SERVICE_NAME, "*** %s, ver:%s, sdk:%s (build:" __DATE__ " " __TIME__ ") ***", APP_PRODUCT,
                   APP_VERSION, system_get_sdk_version ());
    d_log_wprintf (STARTUP_SERVICE_NAME, "***  userbin:0x%06x, fmem:%d  ***", system_get_userbin_addr (),
                   system_get_free_heap_size ());

    struct rst_info *rsti = system_get_rst_info ();
    if ((rsti->reason == REASON_EXCEPTION_RST) || (rsti->exccause)) {
        d_log_eprintf (STARTUP_SERVICE_NAME, "restart reason:%u,cause:%u, entering safe mode, timeout:%u", rsti->reason,
                   rsti->exccause, SYSTEM_SAFEMODE_TIMEOUT_SEC);
        sdata->safe_mode = true;
    
        os_timer_disarm (&sdata->timer_safemode);
        os_timer_setfn (&sdata->timer_safemode, safemode_timeout, NULL);
        os_timer_arm (&sdata->timer_safemode, SYSTEM_SAFEMODE_TIMEOUT_SEC * MSEC_PER_SEC, false);
    }

    if ((fwupdate_verify () == UPGRADE_NOT_VERIFIED) && (sdata->safe_mode)) {
        d_log_eprintf (STARTUP_SERVICE_NAME, "firmware upgrade not verified, rollback");
        fwupdate_rollback ();
    }
#endif

#ifndef DISABLE_SYSTEM
    d_log_iprintf (STARTUP_SERVICE_NAME, "block_size imdb:%u,fdb:%u", SYSTEM_IMDB_BLOCK_SIZE, SYSTEM_FDB_BLOCK_SIZE);

    imdb_init (&db_def, &sdata->hmdb);
    imdb_init (&fdb_def, &sdata->hfdb);

    svcctl_start (sdata->hmdb, sdata->hfdb);
    // installing services
    int             i;
    for (i = 0; i < REG_SERVICE_MAX; i++) {
        reg_services[i].fn_install (!sdata->safe_mode || reg_services[i].safe_mode);
    }
#endif

#ifdef ARCH_XTENSA
    time_overflow_setup ();

    wifi_set_event_handler_cb (wifi_event_handler_cb);
#endif

    d_log_wprintf (STARTUP_SERVICE_NAME, "done, fmem:%d", system_get_free_heap_size ());

    svcctl_service_message (0, 0, NULL, SVCS_MSGTYPE_SYSTEM_START, NULL, NULL);

    // flush all changed blocks
    imdb_flush (sdata->hfdb);
}

void            ICACHE_FLASH_ATTR
system_shutdown (void)
{
    if (!sdata)
        return;

    svcctl_service_message (0, 0, NULL, SVCS_MSGTYPE_SYSTEM_STOP, NULL, NULL);

#ifndef DISABLE_SYSTEM
    svcctl_stop ();

    imdb_done (sdata->hmdb);
    imdb_done (sdata->hfdb);
#endif

#ifdef ARCH_XTENSA
    //wifi_station_disconnect ();
    os_timer_disarm (&sdata->timer_overflow);
    os_timer_disarm (&sdata->timer_softap);
    os_timer_disarm (&sdata->timer_safemode);
#endif
    st_free (sdata);

    d_log_wprintf (SHUTDOWN_SERVICE_NAME, "done, fmem:%d", system_get_free_heap_size ());
    os_printf (LINE_END LINE_END "*** system_shutdown ***" LINE_END);
}

imdb_hndlr_t    ICACHE_FLASH_ATTR
get_hmdb (void)
{
    return sdata->hmdb;
}

imdb_hndlr_t    ICACHE_FLASH_ATTR
get_fdb (void)
{
    return sdata->hfdb;
}

uint8           ICACHE_FLASH_ATTR
system_get_default_secret (unsigned char *buf, uint8 len)
{
    uint8           macaddr[6];
#ifdef ARCH_XTENSA
    if (wifi_get_macaddr (STATION_IF, macaddr))
        return buf2hex ((char *) buf, (char *) &macaddr, MIN (sizeof (macaddr), len / 2));
    else
#endif
        return 0;
}

uint8           ICACHE_FLASH_ATTR
system_get_default_ssid (unsigned char *buf, uint8 len)
{
#ifdef ARCH_XTENSA
    char           *hostname = wifi_station_get_hostname ();
    size_t          plen = (hostname) ? os_strnlen (hostname, len) : 0;
    if (plen) {
        os_strncpy ((char*) buf, hostname, len);
        return MIN (plen, len);
    }
    else {
        uint8           macaddr[6];
        if (wifi_get_macaddr (STATION_IF, macaddr)) {
            size_t plen = os_strlen(AP_SSID_PREFIX);
            os_memcpy (buf, AP_SSID_PREFIX, plen);
            return plen + buf2hex ( d_pointer_add(char, buf, plen), d_pointer_add(char, &macaddr, 3), MIN (3, (len - plen)/ 2));
        }        
    }
#endif

    return 0;
}

const char     *ICACHE_FLASH_ATTR
system_get_description ()
{
    return (const char *) sdata->sysdescr;
}

void            ICACHE_FLASH_ATTR
system_set_description (const char *sysdescr)
{
    os_strncpy (sdata->sysdescr, sysdescr, SYSTEM_DESCRIPTION_LENGTH);
}


#ifdef ARCH_XTENSA
bool            ICACHE_FLASH_ATTR
system_post_delayed_cb (ETSTimerFunc task, void *arg)
{
    return system_os_post (USER_TASK_PRIO_1, (os_signal_t) task, (os_param_t) arg);
}
#endif
