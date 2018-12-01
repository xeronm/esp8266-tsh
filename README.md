esp8266-tsh
===============
Light-weight Shell for ESP8266

## Buld Firmware from scratch ##

### Configure ###
Configurables are:
1. APP, SPI_MODE, SDK_IMAGE_TOOL in Makefile
2. Defines in esp8266-tsh/tsh/include/core/config.h 

### Buld Firmware Image ###
```
$ cd esp8266-tsh/tsh
$ sudo docker run --name esp8266 -it --rm -v $PWD:/src/project dtec/esp8266:1.22-p
/src/project# make cleanall
/src/project# make all
```

### Flash Firmware Image ###
Use esptool.py


## Usage

### Service description ###

| Id | Name      | Description                  |
| ---| ----------| -----------------------------|
|  0 | multicast | multicast messaging point    |
|  1 | service   | service management           |
|  2 | syslog    | system message logging       |
|  3 | espadmin  | esp8266 system management    |
|  4 | udpctl    | UDP cotrol server            |
|  5 | lwsh      | Light-weight shell           |
|  6 | ntp       | Network Time Protocol client |
|  7 | gpioctl   | GPIO control management      |
|  8 | sched     | Cron-like scheduler          |

### Supported configuration ###

##### Syslog #####

Example:
```
  { "syslog.Log-Severity": 4 }
```

|Parameter|Description|
|---------|-----------|
|syslog.Log-Severity| Logging severity (1- critical, 2- error, 3 - warning, 4 - information, 5 - debug) |

##### ESP Admin #####

Example:
```
  {
    "esp.Wireless": {
        "esp.WiFi-Operation-Mode": 3,
        "esp.WiFi-Sleep-Type": 2,
        "esp.WIFI-Station": {
            "esp.WiFi-SSID": "router01",
            "esp.WiFi-Password": "router_password",
            "esp.WiFi-Auto-Connect": 1
        },
        "esp.WIFI-Soft-AP": {
            "esp.WiFi-Password": "ap_password",
            "esp.WiFi-Auth-Mode": 4
        }
    }
  }
```

|Parameter|Description|
|---------|-----------|
|esp.WiFi-Operation-Mode| WiFi operation mode (1- station, 2- softap, 3- station + softap) |
|esp.WiFi-Sleep-Type | WiFi sleep type (0- none, 1- light, 2- modem)|
|esp.WIFI-Station| Station mode parameters (object) |
|esp.WIFI-Soft-AP| Soft AP mode parameters (object) |
|esp.WiFi-SSID| SSID |
|esp.WiFi-Password| Password |
|esp.WiFi-Auth-Mode| Soft AP authentication mode (0- open, 1- wep, 2- wpa psk, 3- wpa2 psk, 4- wpa/wpa2 psk) |
|esp.WiFi-Auto-Connect| Station mode auto connect (0- disabled, 1- enabled)|

##### UDP ctl #####

Example:
```
  {
    "common.IP-Port": 3900,
    "uctl.Secret": "mysecret"
  }
```

|Parameter|Description|
|---------|-----------|
|common.IP-Port| Listening UDP port |
|uctl.Secret| authentication secret |

##### NTP #####

Example:
```
  { 
    "common.Time-Zone": "+3:00", 
    "ntp.Poll-Interval": 10, 
    "ntp.Peer": [
      { "common.Host-Name": "0.pool.ntp.org" }, 
      { "common.Host-Name": "1.pool.ntp.org" }
    ]
  }
```

|Parameter|Description|
|---------|-----------|
|common.Time-Zone| Local Timezone (1/4 hours) |
|ntp.Poll-Interval| Poll interval (minutes) |
|ntp.Peer| NTP Server peers (object list 0-2 items) |
|common.Host-Name| NTP Server host |

## Memos

### Indent
```
$ find ./ -name '*.h' -exec indent -l120 -brs -br -i4 -ci4 -di16 -sc {} -o {} \;
$ find ./ -name '*.c' -exec indent -l120 -brs -br -i4 -ci4 -di16 -sc {} -o {} \;
```
