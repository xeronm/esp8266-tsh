#ifndef __USER_DHTXX_H__
#define __USER_DHTXX_H__

#include "sysinit.h"
#include "core/utils.h"
#include "system/services.h"

typedef enum dht_result_e {
    DHT_RESULT_SUCCESS = 0,
    DHT_RESULT_INTERNAL_ERROR,
    DHT_RESULT_UNAVAILABLE,
    DHT_RESULT_NO_RESPONSE,
    DHT_RESULT_INVALID_RESPONSE,
    DHT_RESULT_INVALID_DATA,
    DHT_RESULT_ERROR_DATA_LEN,
    DHT_RESULT_ERROR_CHKSUM,
} dht_result_t;

typedef enum dht_type_e {
    DHT_TYPE_DHT11 = 0,
    DHT_TYPE_AM2302 = 1,
} dht_type_t;

typedef struct dht_s {
    uint16          hmdt;
    uint16          temp;
} dht_t;


#define DHT_SERVICE_ID			21
#define DHT_SERVICE_NAME		"dev.dht"

#define DHT_HISTORY_LENGTH		40
#define DHT_MIN_QUERY_TIMEOUT_SEC	5
#define DHT_FAIL_RETRY_COUNT		3
#define DHT_MIN_STAT_TIMEOUT_SEC	20
#define DHT_MIN_EMA_ALPHA_PCT		5
#define DHT_MAX_EMA_ALPHA_PCT		95

#define DHT_DEFAULT_EMA_ALPHA_PCT	90      // Exponentioal moving average alpha PCT
#define DHT_DEFAULT_SENSOR_TYPE		DHT_TYPE_DHT11
#define DHT_DEFAULT_GPIO		4
#define DHT_DEFAULT_STAT_TIMEOUT_SEC	20
#define DHT_DEFAULT_HIST_INTERVAL	4

typedef enum dht_msgtype_e {
    DHT_MSGTYPE_QUERY = 10,
    DHT_MSGTYPE_PURGE_STAT = 11,
} dht_msgtype_t;

typedef enum dht_avp_code_e {
    DHT_SENSOR_TYPE = 100,
    DHT_STAT_TIMEOUT = 101,
    DHT_STAT_HIST_INTERVAL = 102,
    DHT_STAT_EMA_ALPHA_PCT = 103,
    DHT_HUMIDITY = 104,
    DHT_TEMPERATURE = 105,
    DHT_QUERY_RESULT_CODE = 106,
    DHT_STAT_LAST_TIME = 107,
    DHT_STAT_LAST = 108,
    DHT_STAT_AVERAGE = 109,
    DHT_STAT_EMA_INIT_COUNT = 110,
    DHT_THRESHOLD_HIGH = 111,
    DHT_THRESHOLD_LOW = 112,
} dht_avp_code_t;


typedef dht_t   dht_hist_t[DHT_HISTORY_LENGTH];

dht_result_t    dht_query (dht_t * value);
dht_result_t    dht_get (dht_t * value);        // exponential moving average

dht_result_t    dht_get_hist (dht_hist_t * hist);

// used by services
svcs_errcode_t  dht_service_install ();
svcs_errcode_t  dht_service_uninstall ();
svcs_errcode_t  dht_on_start (const svcs_resource_t * svcres, dtlv_ctx_t * conf);
svcs_errcode_t  dht_on_stop ();
svcs_errcode_t  dht_on_cfgupd (dtlv_ctx_t * conf);

svcs_errcode_t  dht_on_message (service_ident_t orig_id,
                                service_msgtype_t msgtype, void *ctxdata, dtlv_ctx_t * msg_in, dtlv_ctx_t * msg_out);

#endif
