esp8266-tsh
===============
Light-weight Shell for ESP8266

## Buld Firmware from scratch

### Preparing environment

```
$ sudo docker pull dtec/esp8266
$ git clone https://github.com/xeronm/esp8266-tsh.git
$ cd esp8266-tsh
```

### Configure



Configurables are:
 - APP, SPI_MODE, SDK_IMAGE_TOOL in `Makefile`
 - Defines in `./include/core/config.h`

### Buld Firmware Image
```
$ sudo docker run --name esp8266 -it --rm -v $PWD:/src/project dtec/esp8266:1.22-p
/src/project# make cleanall
/src/project# make build
```

### Flash Firmware Image ###

Use esptool.py
```
$ cd ./bin
$ sudo esptool.py -p /dev/ttyUSB0 -b 115200 write_flash --flash_freq 80m --flash_mode dio --flash_size 32m --verify 0x00000 boot_v1.7.bin 0x01000 tsh-0.1.0-dev.spi4.app1.bin 0x7e000 blank.bin 0x3fc000 esp_init_data_default.bin 0x3fe000 blank.bin
```


## Usage

### Service description

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

### Services configuration

#### Syslog

System logging configuration parameters:

|Parameter|Level|Description|Default|
|---------|-----|-----------|-------|
|syslog.Log-Severity| 0 | Logging severity (1- critical, 2- error, 3- warning, 4- information, 5- debug) | 4- information |

Example:
```
  { "syslog.Log-Severity": 4 }
```

#### ESP Admin

esp8266 system configuration parameters:

|Parameter|Level|Description|Default|
|---------|-----|-----------|-------|
|esp.WiFi-Operation-Mode| 0 | WiFi operation mode (1- station, 2- softap, 3- station + softap) | 3- station + softap |
|esp.WiFi-Sleep-Type | 0 | WiFi sleep type (0- none, 1- light, 2- modem)| 2- modem |
|esp.WIFI-Station| 0 | Station mode parameters (object) |
|esp.WiFi-SSID| 1 | SSID |  |
|esp.WiFi-Password| 1 | Password |  |
|esp.WiFi-Auto-Connect| 1 | Station mode auto connect (0- disabled, 1- enabled)| 1- enabled |
|esp.WIFI-Soft-AP| 0 | Soft AP mode parameters (object) |
|esp.WiFi-SSID| 1 | SSID | ESPTSH_${MAC48[3:6]}> |
|esp.WiFi-Password| 1 | Password | ${MAC48} |
|esp.WiFi-Auth-Mode| 1 | Soft AP authentication mode (0- open, 1- wep, 2- wpa psk, 3- wpa2 psk, 4- wpa/wpa2 psk) | 4- wpa/wpa2 psk |

`${MAC48}` - MAC address of station interface

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

#### UDPCTL

UDP control configuration parameters:

|Parameter|Level|Description|Default|
|---------|-----|-----------|-------|
|common.IP-Port| 0 | Listening UDP port | 3901 |
|uctl.Secret| 0 | Authentication secret | ${MAC48} |

`${MAC48}` - MAC address of station interface

Example:
```
  {
    "common.IP-Port": 3900,
    "uctl.Secret": "mysecret"
  }
```


#### NTP

NTP client configuration parameters:

|Parameter|Level|Description|Default|
|---------|-----|-----------|-------|
|common.Time-Zone| 0 | Local Timezone (1/4 hours) | +0:00 |
|ntp.Poll-Interval| 0 |  Poll interval (minutes) | 20 |
|ntp.Peer| 0 |NTP Server peers (object list 0-2 items) | 0.pool.ntp.org, 1.pool.ntp.org |
|common.Host-Name| 1 | NTP Server host |  |

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

## Memos

### Indent
```
$ find ./ -name '*.h' -exec indent -l120 -brs -br -i4 -ci4 -di16 -sc {} -o {} \;
$ find ./ -name '*.c' -exec indent -l120 -brs -br -i4 -ci4 -di16 -sc {} -o {} \;
```
