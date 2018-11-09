/* 
 * ESP8266 DHT11/DHT22 Sensor Service
 * Copyright (c) 2016-2018 Denis Muratov <xeronm@gmail.com>.
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
#include "core/utils.h"
#include "core/logging.h"
#include "system/services.h"
#include "system/comavp.h"
#ifdef ARCH_XTENSA
#  include "gpio.h"
#endif
#include "service/gpioctl.h"
#include "service/device/dhtxx.h"
#include "service/lsh.h"

#define DHTxx_HOST_PRE_QUERY_US		10
#define DHTxx_HOST_LOW_QUERY_US		(18*USEC_PER_MSEC) + 500
#define DHTxx_SLAVE_READY_10US			800
#define DHTxx_SLAVE_LOW_DATA_PREFIX_10US	500
#define DHTxx_SLAVE_HIGH_DATA0_10US		260
#define DHTxx_SLAVE_HIGH_DATA1_10US		700
#define DHTxx_SAMPLE_US				5
#define DHTxx_SAMPLE_SIZE			80 // 2*80 us prefix + max_len = 120us * 40 bit = 4960us

#define DHTxx_ANSWER_LENGTH_BYTE	5
#define DHTxx_ANSWER_LENGTH_BIT		40

#define DHTxx_EMA_INIT_COUNT		10

#define d_sample_check_len(x, dur, sdur)	( ((x) >= ((dur)/(sdur) - 2)) && ((x) <= ((dur)/(sdur) + 2)) )

typedef union dhtxx_resp_u {
    struct {
	uint16          hmdt;
	uint16          temp;
	uint8           chksum;
    } comp;
    uint8           bytes[DHTxx_ANSWER_LENGTH_BYTE];
} dhtxx_resp_t;

typedef uint8   dhtxx_resp_sample_t[DHTxx_SAMPLE_SIZE];	

typedef struct dht_conf_s {
    uint8           gpio_id;	// default GPIO
    dht_type_t      type;
    uint8           stat_timeout;	// moving average timeout
    uint8           hist_interval;	// history flush stat interval
    uint8           ema_alpha_pct;	// alpha pct
} dht_conf_t;

typedef struct dht_data_s {
    imdb_hndlr_t    hmdb;
    imdb_hndlr_t    hdata;
    dht_conf_t      conf;

    gpio_result_t   gpio_res;

    // stat
    os_time_t       stat_last_time;
    dht_t           stat_last_value;
    dht_t           stat_ema_value;	// moving average value
    uint8           stat_ema_initcnt;

    dht_result_t    stat_last_result;
    uint8           stat_retry_count;

    dht_hist_t      stat_hist;
#ifdef ARCH_XTENSA
    os_timer_t      stat_timer;	// stat timer
    os_timer_t      stat_fail_timer;	// stat query fail timer
#endif
} dht_data_t;

LOCAL dht_data_t *sdata = NULL;

dht_result_t    ICACHE_FLASH_ATTR
dht_query (dht_t * value)
{
    if (!sdata) {
	d_log_eprintf (DHT_SERVICE_NAME, "not started");
	return DHT_RESULT_UNAVAILABLE;
    }
    if (sdata->gpio_res != GPIO_RESULT_SUCCESS) {
	d_log_eprintf (DHT_SERVICE_NAME, "gpio not init");
	return DHT_RESULT_UNAVAILABLE;
    }

    dhtxx_resp_sample_t sample_buf;
    os_memset (&sample_buf, 0, sizeof (dhtxx_resp_sample_t));
    //  _      _
    //   \____/ ... ready
    //    18ms
    // Host signal
#ifdef ARCH_XTENSA
    uint8           gpio_pin = GPIO_ID_PIN (sdata->conf.gpio_id);
    GPIO_OUTPUT_SET (gpio_pin, true);
    os_delay_us (DHTxx_HOST_PRE_QUERY_US);

    GPIO_OUTPUT_SET (gpio_pin, false);
    os_delay_us (DHTxx_HOST_LOW_QUERY_US);

    GPIO_DIS_OUTPUT (gpio_pin);
#endif

    // slave signal
    uint32          _ts = system_get_time ();
    //  2*80 us prefix + max_len = 120us * 40 bit = 4960us
    int             i;
    uint8          *bptr = (uint8 *) sample_buf;
    for (i = 0; i < DHTxx_SAMPLE_SIZE*8; i += 1) {
        uint8           offset = i & 7;
#ifdef ARCH_XTENSA
	if (GPIO_INPUT_GET (gpio_pin)) {
            *bptr = *bptr | (1 << (7-offset));
        }
#endif
        if (offset == 7)
            bptr++;
#ifdef ARCH_XTENSA
	os_delay_us (DHTxx_SAMPLE_US);
#endif
    }
    _ts = 10*(system_get_time () - _ts) / i;

    d_log_dbprintf (DHT_SERVICE_NAME, (char *) &sample_buf, sizeof (dhtxx_resp_sample_t), "sample buffer [count:%u,len10us:%u]", i, _ts);

    //  _      _     _         _______
    //   \____/  ...  \_______/       \_ ... data
    //    18us           80us    80us   
    //    Host signal    Slave signal                                                         
    dhtxx_resp_t    data;
    os_memset (&data, 0, sizeof (dhtxx_resp_t));
    uint8           flevel;
    uint8           flevel_prev = 1;	// starts from high level
    uint8           flevel_dur = 0;
    int             lidx = 0;
    int             didx = 0;
    for (i = 0; i < DHTxx_SAMPLE_SIZE*8; i += 1) {
	flevel = d_bitbuf_get (sample_buf, i);
	if (flevel != flevel_prev) {

	    if (((lidx == 1) && flevel_prev)
		|| ((lidx == 2) && (!flevel_prev || !d_sample_check_len (flevel_dur, DHTxx_SLAVE_READY_10US, _ts)))) {
		d_log_dprintf (DHT_SERVICE_NAME, "invalid response pos:%d[%d], siglvl:%d, lvldur:%d", i, lidx,
			       flevel_prev, flevel_dur);
		return DHT_RESULT_INVALID_RESPONSE;
	    }

	    if (lidx > 2) {
		// _       ________
		//  \_____/        \_ ... data
		//    50us  30-70us
		//         Data bit                                              
		if ((lidx % 2 == 1) && ((!d_sample_check_len (flevel_dur, DHTxx_SLAVE_LOW_DATA_PREFIX_10US, _ts)) || flevel_prev)) {
		    d_log_dprintf (DHT_SERVICE_NAME, "invalid data prefix pos:%d[%d], siglvl:%d, lvldur:%d", i, lidx,
				   flevel_prev, flevel_dur);
		    return DHT_RESULT_INVALID_DATA;
		}
		if (lidx % 2 == 0) {
		    uint8           fbit = 0xFF;
		    if (flevel_prev) {
			if (d_sample_check_len (flevel_dur, DHTxx_SLAVE_HIGH_DATA0_10US, _ts)) {
			    fbit = 0;
			}
			if (d_sample_check_len (flevel_dur, DHTxx_SLAVE_HIGH_DATA1_10US, _ts)) {
			    fbit = 1;
			    data.bytes[didx >> 3] |= (1 << (7 - (didx % 8)));
			}
		    }
		    if (fbit == 0xFF) {
			d_log_dprintf (DHT_SERVICE_NAME, "invalid data pos:%d[%d], siglvl:%d, lvldur:%d", i, lidx,
				       flevel_prev, flevel_dur);
			return DHT_RESULT_INVALID_DATA;
		    }
		    didx += 1;
		}
	    }
	    lidx += 1;
	    if (didx > DHTxx_ANSWER_LENGTH_BIT) {
		break;
	    }

	    flevel_dur = 1;
	    flevel_prev = flevel;
	}
	else {
	    flevel_dur += 1;
	}
    }

    if (lidx <= 2) {
	d_log_dprintf (DHT_SERVICE_NAME, "invalid response pos:%d[%d], siglvl:%d, lvldur:%d", i, lidx, flevel_prev,
		       flevel_dur);
	return DHT_RESULT_INVALID_RESPONSE;
    }

    if (didx != DHTxx_ANSWER_LENGTH_BIT) {
	d_log_dprintf (DHT_SERVICE_NAME, "invalid received data len:%d", didx);
	return DHT_RESULT_ERROR_DATA_LEN;
    }

    d_log_dbprintf (DHT_SERVICE_NAME, (char *) &data, DHTxx_ANSWER_LENGTH_BYTE, "result");

    uint8           chksum = 0;
    for (i = 0; i < DHTxx_ANSWER_LENGTH_BYTE - 1; i += 1) {
	chksum += data.bytes[i];
    }
    if (chksum != data.comp.chksum) {
	d_log_dprintf (DHT_SERVICE_NAME, "invalid checksum:%02x", chksum);
	return DHT_RESULT_ERROR_CHKSUM;
    }

    value->hmdt = be16toh(data.comp.hmdt);
    value->temp = be16toh(data.comp.temp);
    if (sdata->conf.type == DHT_DHT11) {
        value->hmdt = (value->hmdt >> 8)*100;
        value->temp = (value->temp >> 8)*100;
    }

    return DHT_RESULT_SUCCESS;
}

LOCAL void      ICACHE_FLASH_ATTR
dht_hist_timeout (void *args)
{
    if (!sdata) {
	d_log_eprintf (DHT_SERVICE_NAME, "not started");
	return;
    }


    dht_t           value;
    os_memset (&value, 0, sizeof (dht_t));
    dht_result_t    dht_result = dht_query (&value);

    os_time_t       _ts = lt_ctime ();
    if (sdata->stat_last_result == DHT_RESULT_SUCCESS) {
        if (sdata->stat_ema_initcnt > 0)
	    sdata->stat_ema_initcnt--;
	sdata->stat_last_result = dht_result;
	sdata->stat_last_time = _ts;
	os_memcpy (&sdata->stat_last_value, &value, sizeof (dht_t));

	if (sdata->stat_ema_initcnt != DHTxx_EMA_INIT_COUNT) {
	    sdata->stat_ema_value.hmdt =
		(sdata->stat_ema_value.hmdt * (100 - DHT_DEFAULT_EMA_ALPHA_PCT) +
		 value.hmdt * DHT_DEFAULT_EMA_ALPHA_PCT) / 100;
	    sdata->stat_ema_value.temp =
		(sdata->stat_ema_value.temp * (100 - DHT_DEFAULT_EMA_ALPHA_PCT) +
		 value.temp * DHT_DEFAULT_EMA_ALPHA_PCT) / 100;
	}
	else
	    os_memcpy (&sdata->stat_ema_value, &value, sizeof (dht_t));

    }
    else {
	sdata->stat_retry_count++;
	if (sdata->stat_retry_count >= DHT_FAIL_RETRY_COUNT) {
	    sdata->stat_last_result = dht_result;
	    sdata->stat_last_time = _ts;

	    sdata->stat_retry_count = 0;
	    sdata->stat_ema_initcnt = DHTxx_EMA_INIT_COUNT;
	}
#ifdef ARCH_XTENSA
	else
	    os_timer_arm (&sdata->stat_fail_timer, DHT_MIN_QUERY_TIMEOUT_SEC * MSEC_PER_SEC, false);
#endif
    }

}

/*
 * ema_flag: (0|1) return EMA values wher true
 * humidity: in 1/100-th of %
 * temperature: in 1/100-th of C
 * result: dht_result_t
 */
LOCAL void    ICACHE_FLASH_ATTR
fn_dht_get (sh_bc_arg_t * ret_arg, const arg_count_t arg_count, sh_bc_arg_type_t arg_type[], sh_bc_arg_t * bc_args[]) 
{
    if (arg_count != 3) {
        ret_arg->arg.value = DHT_RESULT_INTERNAL_ERROR;
        return;
    }
    if (sdata->stat_ema_initcnt == DHTxx_EMA_INIT_COUNT) {
        ret_arg->arg.value = DHT_RESULT_UNAVAILABLE;
        return;
    }

    ret_arg->arg.value = sdata->stat_last_result;
    if (sdata->stat_last_result != DHT_RESULT_SUCCESS) {
        return;
    }

    if (bc_args[0]->arg.value == 0) {
        bc_args[1]->arg.value = sdata->stat_last_value.hmdt;
        bc_args[2]->arg.value = sdata->stat_last_value.temp;
    }
    else {
        if (sdata->stat_ema_initcnt) {
            ret_arg->arg.value = DHT_RESULT_UNAVAILABLE;
            return;
        } else {
            bc_args[1]->arg.value = sdata->stat_ema_value.hmdt;
            bc_args[2]->arg.value = sdata->stat_ema_value.temp;
        }
    }
}

svcs_errcode_t  ICACHE_FLASH_ATTR
dht_on_start (imdb_hndlr_t himdb, imdb_hndlr_t hdata, dtlv_ctx_t * conf)
{
    if (sdata) {
	return SVCS_SERVICE_ERROR;
    }

    d_svcs_check_imdb_error (imdb_clsobj_insert (hdata, d_pointer_as (void *, &sdata), sizeof (dht_data_t))
	);
    os_memset (sdata, 0, sizeof (dht_data_t));
    sdata->hmdb = himdb;
    sdata->hdata = hdata;

#ifdef ARCH_XTENSA
    os_timer_disarm (&sdata->stat_timer);
    os_timer_setfn (&sdata->stat_timer, dht_hist_timeout, NULL);
    os_timer_disarm (&sdata->stat_fail_timer);
    os_timer_setfn (&sdata->stat_fail_timer, dht_hist_timeout, NULL);
#endif
    // register functions
    sh_func_entry_t fn_entries[1] = {
        { DHT_SERVICE_ID, false, false, 0, "dht_get", { fn_dht_get } },
    };
    
    int i;
    for (i = 0; i < 1; i++) 
        sh_func_register (&fn_entries[i]);

    return dht_on_cfgupd (conf);
}

svcs_errcode_t  ICACHE_FLASH_ATTR
dht_on_stop ()
{
    if (!sdata)
	return SVCS_NOT_RUN;

    if (sdata->gpio_res == GPIO_RESULT_SUCCESS)
	gpio_release (sdata->conf.gpio_id);

    d_svcs_check_imdb_error (imdb_clsobj_delete (sdata->hdata, sdata));
    sdata = NULL;

    return SVCS_ERR_SUCCESS;
}


svcs_errcode_t  ICACHE_FLASH_ATTR
dht_on_msg_info (dtlv_ctx_t * msg_out)
{
    dtlv_avp_t     *gavp;
    d_svcs_check_imdb_error (dtlv_avp_encode_uint8 (msg_out, COMMON_AVP_PEREPHERIAL_GPIO_ID, sdata->conf.gpio_id) ||
			     dtlv_avp_encode_uint8 (msg_out, DHT_SENSOR_TYPE, sdata->conf.type) ||
			     dtlv_avp_encode_uint8 (msg_out, DHT_STAT_TIMEOUT, sdata->conf.stat_timeout) ||
			     dtlv_avp_encode_uint8 (msg_out, DHT_STAT_HIST_INTERVAL, sdata->conf.hist_interval) ||
			     dtlv_avp_encode_uint8 (msg_out, DHT_STAT_EMA_ALPHA_PCT, sdata->conf.ema_alpha_pct));

    if (sdata->stat_ema_initcnt != DHTxx_EMA_INIT_COUNT) {
	d_svcs_check_dtlv_error (dtlv_avp_encode_grouping (msg_out, 0, DHT_STAT_LAST, &gavp) ||
				 dtlv_avp_encode_uint32 (msg_out, DHT_STAT_LAST_TIME, lt_time (&sdata->stat_last_time))
				 || dtlv_avp_encode_uint8 (msg_out, DHT_QUERY_RESULT_CODE, sdata->stat_last_result)
				 || dtlv_avp_encode_uint16 (msg_out, DHT_HUMIDITY, sdata->stat_last_value.hmdt)
				 || dtlv_avp_encode_uint16 (msg_out, DHT_TEMPERATURE, sdata->stat_last_value.temp)
				 || dtlv_avp_encode_group_done (msg_out, gavp)
				 || (!sdata->stat_ema_initcnt) ? (dtlv_avp_encode_grouping (msg_out, 0, DHT_STAT_AVERAGE, &gavp)
				        || dtlv_avp_encode_uint16 (msg_out, DHT_HUMIDITY, sdata->stat_ema_value.hmdt)
				        || dtlv_avp_encode_uint16 (msg_out, DHT_TEMPERATURE, sdata->stat_ema_value.temp)
				        || dtlv_avp_encode_group_done (msg_out, gavp)) : 
                                     dtlv_avp_encode_uint8 (msg_out, DHT_STAT_EMA_INIT_COUNT, sdata->stat_ema_initcnt));
    }
    else {
	d_svcs_check_dtlv_error (dtlv_avp_encode_grouping (msg_out, 0, DHT_STAT_LAST, &gavp) ||
				 dtlv_avp_encode_uint32 (msg_out, DHT_STAT_LAST_TIME, lt_time (&sdata->stat_last_time))
				 || dtlv_avp_encode_uint8 (msg_out, DHT_QUERY_RESULT_CODE, sdata->stat_last_result)
				 || dtlv_avp_encode_group_done (msg_out, gavp));
    }

    return SVCS_ERR_SUCCESS;
}

svcs_errcode_t  ICACHE_FLASH_ATTR
dht_on_message (service_ident_t orig_id, service_msgtype_t msgtype, void *ctxdata, dtlv_ctx_t * msg_in,
		dtlv_ctx_t * msg_out)
{
    svcs_errcode_t  res = SVCS_ERR_SUCCESS;
    switch (msgtype) {
    case SVCS_MSGTYPE_INFO:
	res = dht_on_msg_info (msg_out);
	break;
    case DHT_MSGTYPE_QUERY:
	{
	    dht_t           value;
	    os_memset (&value, 0, sizeof (dht_t));
	    dht_result_t    dht_result = dht_query (&value);

	    d_svcs_check_dtlv_error (dtlv_avp_encode_uint8 (msg_out, DHT_QUERY_RESULT_CODE, dht_result) ||
				     dtlv_avp_encode_uint16 (msg_out, DHT_HUMIDITY, value.hmdt) ||
				     dtlv_avp_encode_uint16 (msg_out, DHT_TEMPERATURE, value.temp)
		);

	}
	break;
    default:
	res = SVCS_MSGTYPE_INVALID;
    }

    return res;
}


svcs_errcode_t  ICACHE_FLASH_ATTR
dht_on_cfgupd (dtlv_ctx_t * conf)
{
    os_memset (&sdata->conf, 0, sizeof (dht_conf_t));

    sdata->conf.gpio_id = DHT_DEFAULT_GPIO;
    sdata->conf.type = DHT_DEFAULT_SENSOR_TYPE;
    sdata->conf.stat_timeout = MAX (DHT_DEFAULT_STAT_TIMEOUT_SEC, DHT_MIN_STAT_TIMEOUT_SEC);
    sdata->conf.hist_interval = DHT_DEFAULT_HIST_INTERVAL;
    sdata->conf.ema_alpha_pct = DHT_DEFAULT_EMA_ALPHA_PCT;

    d_log_iprintf (DHT_SERVICE_NAME, "gpio:%u, timeout:%u", sdata->conf.gpio_id, sdata->conf.stat_timeout);

    os_memset (sdata->stat_hist, 0, sizeof (dht_hist_t));
    os_memset (&sdata->stat_ema_value, 0, sizeof (dht_t));
    os_memset (&sdata->stat_last_value, 0, sizeof (dht_t));
    sdata->stat_retry_count = 0;
    sdata->stat_ema_initcnt = DHTxx_EMA_INIT_COUNT;
    sdata->stat_last_result = DHT_RESULT_UNAVAILABLE;

#ifdef ARCH_XTENSA
    os_timer_disarm (&sdata->stat_timer);
    sdata->gpio_res = gpio_acquire (sdata->conf.gpio_id, true, NULL);
    if (sdata->gpio_res == GPIO_RESULT_SUCCESS) {
	os_timer_arm (&sdata->stat_timer, sdata->conf.stat_timeout * MSEC_PER_SEC, true);
    }
#endif

    return SVCS_ERR_SUCCESS;
}

svcs_errcode_t  ICACHE_FLASH_ATTR
dht_service_install ()
{
    svcs_service_def_t sdef;
    os_memset (&sdef, 0, sizeof (svcs_service_def_t));
    sdef.enabled = true;
    sdef.on_cfgupd = dht_on_cfgupd;
    sdef.on_message = dht_on_message;
    sdef.on_start = dht_on_start;
    sdef.on_stop = dht_on_stop;

    return svcctl_service_install (DHT_SERVICE_ID, DHT_SERVICE_NAME, &sdef);
}

svcs_errcode_t  ICACHE_FLASH_ATTR
dht_service_uninstall ()
{
    return svcctl_service_uninstall (DHT_SERVICE_NAME);
}
