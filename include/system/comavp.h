/* Copyright (c) 2018 by Denis Muratov <xeronm@gmail.com>. All rights reserved

   FileName: comavp.h
   Source: https://dtec.pro/gitbucket/git/esp8266/esp8266_lsh.git

   Description: System common AVP
       Reserved range: 1..99

*/

#ifndef _COMAVP_H_
#define _COMAVP_H_ 1

typedef enum common_avp_code_e {
    COMMON_AVP_APP_PRODUCT = 1,
    COMMON_AVP_APP_VERSION = 2,
    COMMON_AVP_RESULT_CODE = 3,
    COMMON_AVP_RESULT_MESSAGE = 4,
    COMMON_AVP_IPV4_ADDRESS = 5,
    COMMON_AVP_MAC48 = 6,
    COMMON_AVP_IP_PORT = 7,
    COMMON_AVP_RESULT_EXT_CODE = 8,
    COMMON_AVP_EVENT_TIMESTAMP = 9,
    COMMON_AVP_SVC_MESSAGE = 10,
    COMMON_AVP_SVC_MESSAGE_TYPE = 11,
    COMMON_AVP_SVC_CONFIGURATION = 12,
    COMMON_AVP_HOST_NAME = 13,
    COMMON_AVP_TIME_ZONE = 14,
    COMMON_AVP_SERVICE_NAME = 15,
    COMMON_AVP_PEREPHERIAL_GPIO_ID = 16,
} common_avp_code_t;

#endif
