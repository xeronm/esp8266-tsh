/* 
 * ESP8266 GPIO Control Service
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
 * Foobar is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Foobar.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#ifndef __USER_GPIO_H__
#define __USER_GPIO_H__	1

#define GPIO_SERVICE_ID		7
#define GPIO_SERVICE_NAME	"gpioctl"

typedef void    (*gpio_cb_func_t) (const uint8 gpio_id, const bool state);

typedef struct gpio_def_s {
    uint32          addr;
    uint8           func;
    bool            available;
} gpio_def_t;

#ifdef ARCH_XTENSA
// ESP-12E Layout
LOCAL gpio_def_t const gpio_layout[GPIO_PIN_COUNT] = {
    {PERIPHS_IO_MUX_GPIO0_U, FUNC_GPIO0, true},	// GPIO_0    High (Low for flash)
    {PERIPHS_IO_MUX_U0TXD_U, FUNC_U0TXD, false},	// GPIO_1    U0TXD
    {PERIPHS_IO_MUX_GPIO2_U, FUNC_GPIO2, true},	// GPIO_2    High
    {PERIPHS_IO_MUX_U0RXD_U, 0, false},	// GPIO_3    U0RXD
    {PERIPHS_IO_MUX_GPIO4_U, FUNC_GPIO4, true},	// GPIO_4
    {PERIPHS_IO_MUX_GPIO5_U, FUNC_GPIO5, true},	// GPIO_5
    {0, 0, false},		// GPIO_6
    {0, 0, false},		// GPIO_7
    {0, 0, false},		// GPIO_8
    {PERIPHS_IO_MUX_SD_DATA2_U, FUNC_GPIO9, true},	// GPIO_9
    {PERIPHS_IO_MUX_SD_DATA3_U, FUNC_GPIO10, true},	// GPIO_10
    {0, 0, false},		// GPIO_11
    {PERIPHS_IO_MUX_MTDI_U, FUNC_GPIO12, true},	// GPIO_12
    {PERIPHS_IO_MUX_MTCK_U, FUNC_GPIO13, true},	// GPIO_13
    {PERIPHS_IO_MUX_MTMS_U, FUNC_GPIO14, true},	// GPIO_14
    {PERIPHS_IO_MUX_MTDO_U, FUNC_GPIO15, true},	// GPIO_15
};
#else
LOCAL gpio_def_t const gpio_layout[] = {};
#endif

typedef struct gpio_use_s {
    uint8           func;
    bool            in_use:1;
    bool            src_pullup:1;
    gpio_cb_func_t  intr_cb;
} gpio_use_t;

typedef enum gpio_msgtype_e {
    GPIO_MSGTYPE_OUTPUT_SET = 10,
} gpio_msgtype_t;

typedef enum gpio_result_e {
    GPIO_RESULT_SUCCESS = 0,
    GPIO_RESULT_ERROR,
    GPIO_RESULT_INVALID_GPIOID,
    GPIO_RESULT_NOT_USABLE,
    GPIO_RESULT_INUSE,
    GPIO_RESULT_NOT_USED,
} gpio_result_t;

typedef enum gpio_avp_code_e {
    GPIO_GPIO_PORT = 101,
    GPIO_PORT_FUNCTION_DEFAULT = 103,
    GPIO_PORT_FUNCTION_SET = 104,
    GPIO_PORT_INUSE = 105,
    GPIO_PORT_AVAILABLE = 106,
    GPIO_PORT_VALUE = 107,
    GPIO_PORT_PULSE_US = 108,
    GPIO_PORT_PULLUP = 109,
} gpio_avp_code_t;

gpio_result_t   gpio_acquire (uint8 gpio_id, bool pullup, gpio_cb_func_t intr_cb);
gpio_result_t   gpio_release (uint8 gpio_id);

svcs_errcode_t  gpio_service_install (void);
svcs_errcode_t  gpio_service_uninstall (void);
svcs_errcode_t  gpio_on_start (imdb_hndlr_t hmdb, imdb_hndlr_t hdata, dtlv_ctx_t * conf);
svcs_errcode_t  gpio_on_stop (void);
svcs_errcode_t  gpio_on_message (service_ident_t orig_id, service_msgtype_t msgtype, void *ctxdata, dtlv_ctx_t * msg_in,
				 dtlv_ctx_t * msg_out);

#endif
