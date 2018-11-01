/* 
 * ESP8266 GPIO Control Service
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
 * Foobar is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ESP8266 Things Shell.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include "sysinit.h"
#include "gpio.h"
#include "eagle_soc.h"
#include "core/logging.h"
#include "system/services.h"
#include "system/comavp.h"
#include "service/gpioctl.h"

typedef struct gpio_data_s {
    imdb_hndlr_t    hdata;

    gpio_use_t      gpio[GPIO_PIN_COUNT];
} gpio_data_t;

LOCAL gpio_data_t *sdata = NULL;

// global interrupt handler
LOCAL void
gpio_intr_handler (void *args)
{
    uint32          gpio_status;
    gpio_status = GPIO_REG_READ (GPIO_STATUS_ADDRESS);

    ETS_GPIO_INTR_DISABLE ();

    int             i;
    for (i = 0; i < GPIO_PIN_COUNT; i++) {
	if ((sdata->gpio[i].intr_cb != NULL) && (gpio_status & BIT (i))) {
	    sdata->gpio[i].intr_cb (i, GPIO_INPUT_GET (GPIO_ID_PIN (i)));
	}
    }

    //clear interrupt status
    GPIO_REG_WRITE (GPIO_STATUS_W1TC_ADDRESS, gpio_status);

    ETS_GPIO_INTR_ENABLE ();
}

// check that at least one GPIO has a interrupt callback
LOCAL bool      ICACHE_FLASH_ATTR
gpio_has_intr (void)
{
    int             i;
    for (i = 0; i < GPIO_PIN_COUNT; i++) {
	if (sdata->gpio[i].intr_cb != NULL)
	    return true;
    }

    return false;
}

// basic GPIO integrity checks
LOCAL gpio_result_t ICACHE_FLASH_ATTR
gpio_common_check (uint8 gpio_id)
{
    if (!GPIO_ID_IS_PIN_REGISTER (gpio_id)) {
	d_log_eprintf (GPIO_SERVICE_NAME, "unregistered gpio_id:%u", gpio_id);
	return GPIO_RESULT_INVALID_GPIOID;
    }

    if ((gpio_layout[gpio_id].addr == 0) || (!gpio_layout[gpio_id].available)) {
	d_log_eprintf (GPIO_SERVICE_NAME, "unusable gpio_id:%u, addr:%p", gpio_id, gpio_layout[gpio_id].addr);
	return GPIO_RESULT_NOT_USABLE;
    }

    return GPIO_RESULT_SUCCESS;
}

// Accuire GPIO. Check that currently GPIO_ID is not using.
//    gpio_id - GPIO idenitfier
//    puulup  - flag to enable internal pull-up
//    intr_cb - interrupt handler callback function
gpio_result_t   ICACHE_FLASH_ATTR
gpio_acquire (uint8 gpio_id, bool pullup, gpio_cb_func_t intr_cb)
{
    if (!sdata)
	return SVCS_NOT_RUN;

    gpio_result_t   res = gpio_common_check (gpio_id);
    if (res != GPIO_RESULT_SUCCESS)
	return res;

    gpio_use_t     *gpio_use = &sdata->gpio[gpio_id];

    if (gpio_use->in_use) {
	d_log_wprintf (GPIO_SERVICE_NAME, "acquire inuse gpio_id:%u", gpio_id);
	return GPIO_RESULT_INUSE;
    }

    if (intr_cb != NULL) {
	if (!gpio_has_intr ()) {
	    ETS_GPIO_INTR_ATTACH (gpio_intr_handler, NULL);

	    // initial cleanup state
	    uint32          gpio_status = GPIO_REG_READ (GPIO_STATUS_ADDRESS);
	    GPIO_REG_WRITE (GPIO_STATUS_W1TC_ADDRESS, gpio_status);
	}

	ETS_GPIO_INTR_DISABLE ();
	//clear status
	GPIO_REG_WRITE (GPIO_STATUS_W1TC_ADDRESS, BIT (gpio_id));
	ETS_GPIO_INTR_ENABLE ();
    }

    gpio_use->in_use = true;
    gpio_use->intr_cb = intr_cb;
    gpio_use->func = gpio_layout[gpio_id].func;
    uint32          addr = gpio_layout[gpio_id].addr;

    PIN_FUNC_SELECT (addr, gpio_use->func);

    if (pullup) {
	PIN_PULLUP_EN (addr);
    }
    else {
	PIN_PULLUP_DIS (addr);
    }

    return GPIO_RESULT_SUCCESS;
}


// Release GPIO. Check that currently GPIO_ID is using.
//    gpio_id - GPIO idenitfier
gpio_result_t   ICACHE_FLASH_ATTR
gpio_release (uint8 gpio_id)
{
    if (!sdata)
	return SVCS_NOT_RUN;

    gpio_result_t   res = gpio_common_check (gpio_id);
    if (res != GPIO_RESULT_SUCCESS)
	return res;

    gpio_use_t     *gpio_use = &sdata->gpio[gpio_id];

    if (!gpio_use->in_use) {
	d_log_wprintf (GPIO_SERVICE_NAME, "release not used gpio_id:%u", gpio_id);
	return GPIO_RESULT_NOT_USED;
    }

    if (gpio_use->intr_cb) {
	ETS_GPIO_INTR_DISABLE ();
	//clear status
	GPIO_REG_WRITE (GPIO_STATUS_W1TC_ADDRESS, BIT (gpio_id));
	// disable interrupt
	gpio_pin_intr_state_set (GPIO_ID_PIN (gpio_id), GPIO_PIN_INTR_DISABLE);

	if (gpio_has_intr ()) {
	    ETS_GPIO_INTR_ENABLE ();
	}
    }
    os_memset (gpio_use, 0, sizeof (gpio_use_t));

    //PIN_FUNC_SELECT(gpio_def->addr, gpio_def->src_func);  
    //(gpio_def->src_pullup)?PIN_PULLUP_EN(gpio_def->addr):PIN_PULLUP_DIS(gpio_def->addr);
    return GPIO_RESULT_SUCCESS;
}

svcs_errcode_t  ICACHE_FLASH_ATTR
gpio_on_start (imdb_hndlr_t hmdb, imdb_hndlr_t hdata, dtlv_ctx_t * conf)
{
    if (sdata)
	return SVCS_SERVICE_ERROR;

    d_svcs_check_imdb_error (imdb_clsobj_insert (hdata, d_pointer_as (void *, &sdata), sizeof (gpio_data_t))
	);
    os_memset (sdata, 0, sizeof (gpio_data_t));
    sdata->hdata = hdata;

    return SVCS_ERR_SUCCESS;
}

svcs_errcode_t  ICACHE_FLASH_ATTR
gpio_on_stop ()
{
    if (!sdata)
	return SVCS_NOT_RUN;

    d_svcs_check_imdb_error (imdb_clsobj_delete (sdata->hdata, sdata));

    sdata = NULL;

    return SVCS_ERR_SUCCESS;
}

svcs_errcode_t  ICACHE_FLASH_ATTR
gpio_on_msg_info (dtlv_ctx_t * msg_out)
{
    dtlv_avp_t     *gavp;
    d_svcs_check_imdb_error (dtlv_avp_encode_list (msg_out, 0, GPIO_GPIO_PORT, DTLV_TYPE_OBJECT, &gavp));

    int             i;
    uint32          value = gpio_input_get ();
    for (i = 0; i < GPIO_PIN_COUNT; i++) {
	gpio_use_t     *gpio_use = &sdata->gpio[i];

	if (!gpio_layout[i].addr)
	    continue;

	dtlv_avp_t     *gavp_in;
	d_svcs_check_dtlv_error (dtlv_avp_encode_grouping (msg_out, 0, GPIO_GPIO_PORT, &gavp_in) ||
				 dtlv_avp_encode_uint8 (msg_out, COMMON_AVP_PEREPHERIAL_GPIO_ID, i) ||
				 dtlv_avp_encode_uint8 (msg_out, GPIO_PORT_AVAILABLE, gpio_layout[i].available) ||
				 dtlv_avp_encode_uint8 (msg_out, GPIO_PORT_INUSE, gpio_use->in_use) ||
				 dtlv_avp_encode_uint8 (msg_out, GPIO_PORT_FUNCTION_DEFAULT, gpio_layout[i].func) ||
				 dtlv_avp_encode_uint8 (msg_out, GPIO_PORT_FUNCTION_SET, gpio_use->func) ||
				 dtlv_avp_encode_uint8 (msg_out, GPIO_PORT_VALUE, (value >> i) & BIT0) ||
				 dtlv_avp_encode_group_done (msg_out, gavp_in));
    }

    d_svcs_check_imdb_error (dtlv_avp_encode_group_done (msg_out, gavp));

    return SVCS_ERR_SUCCESS;
}

svcs_errcode_t  ICACHE_FLASH_ATTR
gpio_on_msg_output_set (dtlv_ctx_t * msg_in, dtlv_ctx_t * msg_out)
{
    if (!msg_in)
	return SVCS_INVALID_MESSAGE;

    dtlv_davp_t     davp;
    uint8           gpio_id = (uint8) GPIO_ID_NONE;
    uint16          pulse_us = 0;
    uint8           value = 0xFF;
    while (dtlv_avp_decode (msg_in, &davp) == DTLV_ERR_SUCCESS) {
	if (!dtlv_check_namespace (&davp, GPIO_SERVICE_ID))
	    break;

	switch (davp.havpd.nscode.comp.code) {
        case COMMON_AVP_PEREPHERIAL_GPIO_ID:
            dtlv_avp_get_uint8 (&davp, &gpio_id);
            break;
        case GPIO_PORT_PULSE_US:
            dtlv_avp_get_uint16 (&davp, &pulse_us);
            break;
        case GPIO_PORT_VALUE:
            dtlv_avp_get_uint8 (&davp, &value);
            break;
        }
    }

    if (gpio_id == GPIO_ID_NONE)
	return SVCS_INVALID_MESSAGE;

    if (value == 0xFF) {
        GPIO_DIS_OUTPUT (GPIO_ID_PIN (gpio_id));
    }
    else {
        GPIO_OUTPUT_SET (GPIO_ID_PIN (gpio_id), (value != 0) );
        if (pulse_us > 0) {
            os_delay_us (pulse_us);
            GPIO_OUTPUT_SET (GPIO_ID_PIN (gpio_id), (value == 0));
        }
    }


    return SVCS_ERR_SUCCESS;
}

svcs_errcode_t  ICACHE_FLASH_ATTR
gpio_on_message (service_ident_t orig_id, service_msgtype_t msgtype, void *ctxdata, dtlv_ctx_t * msg_in,
		 dtlv_ctx_t * msg_out)
{
    svcs_errcode_t  res = SVCS_ERR_SUCCESS;
    switch (msgtype) {
    case SVCS_MSGTYPE_INFO:
	res = gpio_on_msg_info (msg_out);
	break;
    case GPIO_MSGTYPE_OUTPUT_SET:
	res = gpio_on_msg_output_set (msg_in, msg_out);
	break;
    default:
	res = SVCS_MSGTYPE_INVALID;
    }

    return res;
}

svcs_errcode_t  ICACHE_FLASH_ATTR
gpio_service_install (void)
{
    svcs_service_def_t sdef;
    os_memset (&sdef, 0, sizeof (sdef));
    sdef.enabled = true;
    sdef.on_start = gpio_on_start;
    sdef.on_stop = gpio_on_stop;
    sdef.on_message = gpio_on_message;

    return svcctl_service_install (GPIO_SERVICE_ID, GPIO_SERVICE_NAME, &sdef);
}

svcs_errcode_t  ICACHE_FLASH_ATTR
gpio_service_uninstall (void)
{
    return svcctl_service_uninstall (GPIO_SERVICE_NAME);
}
