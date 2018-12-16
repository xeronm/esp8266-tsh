esp8266-tsh
===========
Light-weight Shell for ESP8266 with support of simple and configurable control logic script. 
Developed and tested with my ESP-12E (4Mb) board.

Main Goals:
- OTA firmware update
- light-weight udp control/management protocol with python client [esp8266-tshcli]
- cron-like task scheduler
- simple scripting language for making control logic with trace/debug feature
- almost fixed heap memory consumption

License: GPLv3

*Some of internal solutions may looks weird, but all of it made just for fun and it works well.*

*Feel free to use, improve, report bugs, criticize and etc.*

Contributors
- Denis Muratov <xeronm@gmail.com>

## 1. References

 - [esp-open-sdk]: Free and open (as much as possible) integrated SDK for ESP8266/ESP8285 chips
 - [docker-esp8266]: Toolchain for esp8266 based on crosstool-NG 1.22.x
 - [esp8266-tshcli]: UDP client utilities for Light-weight Shell ESP8266

[esp-open-sdk]: https://github.com/pfalcon/esp-open-sdk.git
[docker-esp8266]: https://github.com/xeronm/docker-esp8266
[esp8266-tshcli]: https://github.com/xeronm/esp8266-tshcli
[DYI_01]: https://raw.githubusercontent.com/xeronm/esp8266-tsh/master/example/DYI_01.jpg
[DYI_02]: https://raw.githubusercontent.com/xeronm/esp8266-tsh/master/example/DYI_02.jpg
[DYI_03]: https://raw.githubusercontent.com/xeronm/esp8266-tsh/master/example/DYI_03.jpg
[Dashboard]: https://raw.githubusercontent.com/xeronm/esp8266-tsh/master/example/dashboard_screenshot.png

## 2. Definitions and Abbrevations

## 3. Buld Firmware from scratch

### 3.1. Prepare build environment

```
$ sudo docker pull dtec/esp8266:1.22-p
$ git clone https://github.com/xeronm/esp8266-tsh.git
$ cd esp8266-tsh
```

### 3.2. Configure project

Configurables are:
- APP, SPI_MODE, SDK_IMAGE_TOOL in `Makefile`
- Global project defines in `./include/core/config.h`

### 3.3. Buld Firmware

```
$ sudo docker run --name esp8266 -it --rm -v $PWD:/src/project dtec/esp8266:1.22-p
/src/project# make build
```


## 4. Usage

### 4.1. System Documentation

#### 4.1.1. Safe Mode

If exception occurs, on system initialization will checked `reason` and `exccause` and will turn system in Safe Mode to 60 seconds.
Only few services need for logging and control operations starts immediately. All  other services starts after 60 seconds timeout. 
Unconfirmed firmware updates will be rolled back to the previous version of firmware.


### 4.2. Service catalog

| Id | Name      | Description                  | Safe Mode* |
| ---| ----------| -----------------------------|-----------|
|  0 | multicast | multicast messaging point    |           |
|  1 | service   | service management           | Yes       |
|  2 | syslog    | system message logging       | Yes       |
|  3 | espadmin  | esp8266 system management    | Yes (no configuration) |
|  4 | udpctl    | UDP system management        | Yes       |
|  5 | lwsh      | Light-weight shell           |           |
|  6 | ntp       | Network Time Protocol client |           |
|  7 | gpioctl   | GPIO control management      |           |
|  8 | sched     | Cron-like scheduler          |           |
| 21 | dev.dht   | DHT11/AM2302 sensor          |           |


### 4.3. Service documentation

#### 4.3.1. Service management (service)

##### 4.3.1.1. Message types

|MsgType|Command|Multicast|Description|
|-------|-------|---------|-----------|
|1|INFO||Query information (state, services, etc.) |
|2|CONTROL||Service control (start/stop, enable/disable, etc.) |
|3|CONFIG_GET|| Query service configuration from Flash-DB (current, new)|
|4|CONFIG_SET|| Set new service configuration (reboot will restore current configuration)|
|5|CONFIG_SAVE|| Apply new service configuration as current configuration|
|32|SYSTEM_START|Y| System startup signal|
|33|SYSTEM_STOP|Y| System shutdown signal|
|34|NETWORK|Y| Network changed signal (station got IP) |
|35|NETWORK_LOSS|Y| Network loss signal |
|36|ADJTIME|Y| Adjust NTP datetime signal |
|37|MCAST_SIG1|Y| User signal |
|38|MCAST_SIG2|Y| User signal |
|39|MCAST_SIG3|Y| User signal |
|40|MCAST_SIG4|Y| User signal |


#### 4.3.2. System logging (syslog)

##### 4.3.2.2. Configuration parameters

|Parameter|Level|Description|Default|
|---------|-----|-----------|-------|
|syslog.Log-Severity| 0 | Logging severity (1- critical, 2- error, 3- warning, 4- information, 5- debug) | 4- information |

Example:
```
  { "syslog.Log-Severity": 4 }
```

#### 4.3.3. esp8266 system management (espadmin)

##### 4.3.3.1. Configuration parameters

|Parameter|Level|Description|Default|
|---------|-----|-----------|-------|
|common.Host-Name| 0 | station DHCP hostname | `ESP_${MAC48[3:6]}` |
|common.System-Description| 0 | Sysatem description | |
|esp.Wireless| 0 | Wireless configuration (object) | |
|esp.WiFi-Operation-Mode| 1 | WiFi operation mode (1- station, 2- softap, 3- station + softap) | 3- station + softap |
|esp.WiFi-Sleep-Type | 1 | WiFi sleep type (0- none, 1- light, 2- modem)| 2- modem |
|esp.WIFI-Station| 1 | Station mode parameters (object) |
|esp.WiFi-SSID| 2 | SSID |  |
|esp.WiFi-Password| 2 | Password |  |
|esp.WiFi-Auto-Connect| 2 | Station mode auto connect (0- disabled, 1- enabled)| 1- enabled |
|esp.WIFI-Soft-AP| 1 | Soft AP mode parameters (object) |
|esp.WiFi-SSID| 2 | SSID | `${HOST_NAME}` |
|esp.WiFi-Password| 2 | Password | `${MAC48}` |
|esp.WiFi-Auth-Mode| 2 | Soft AP authentication mode (0- open, 1- wep, 2- wpa psk, 3- wpa2 psk, 4- wpa/wpa2 psk) | 4- wpa/wpa2 psk |

- `${MAC48}` - MAC address of station interface
- `${HOST_NAME}` - WiFi station DHCP hostname

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

##### 4.3.3.2. Message types

|MsgType|Command|Description|
|-------|-------|-----------|
|1|INFO|Query system information (System, Firmware, Memory-DB, Flash-DB, Wireless)|
|10|RESTART|restart system|
|11|FDB_TRUNC|truncate Flash-DB|
|12|FW_OTA_INIT| Initialize OTA firmware upgrade|
|13|FW_OTA_UPLOAD| Upload firmware bin data |
|14|FW_OTA_DONE| Commit firmware upgrade, will reboot with new firmware|
|15|FW_OTA_ABORT| Abort firmware upgrade|
|16|FW_OTA_VERIFY_DONE| Final commit firmware upgrade, if not successed by 60 sec after restart, will rollback to previous firmware |
|17|FW_VERIFY| Verify firmware digest|

#### 4.3.4. UDP system management (udpctl)

##### 4.3.4.1. Configuration parameters

|Parameter|Level|Description|Default|
|---------|-----|-----------|-------|
|common.IP-Port| 0 | Listening UDP port | 3901 |
|uctl.Secret| 0 | Authentication secret | `${MAC48}` |

`${MAC48}` - MAC address of station interface

Example:
```
  {
    "common.IP-Port": 3901,
    "uctl.Secret": "mysecret"
  }
```

##### 4.3.4.2. Message types

|MsgType|Command|Description|
|-------|-------|-----------|
|1|INFO|Query information (state, peers, etc.) |

##### 4.3.4.3. Protocol

###### Message Flow
0. Auth Request
```
	Auth0 := hmac(Random)
	Digest0 := hmac( Header0, 0, Auth0, Body0 )
	Message0 := (Header0, Digest0, Auth0, Body0)
```
1. Auth Answer
```
	Auth1 := hmac(Random)
	Digest1 = hmac( Header1, Digest0, Auth1, Body1 )
	Message1 = (Header1, Digest1, Auth1, Body1)
```
2. Control Request
```
	Digest2 = hmac( Header2, Digest1, Body2 )
	Message2 = (Header2, Digest2, Body2)
```
3. Control Answer
```
	Digest3 = hmac( Header3, Digest2, Body3 )
	Message3 = (Header3, Digest3, Body3)
```

###### Message Header
```
	 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|          Service-Id           |             Length            |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|R S E x x x x x|    Cmd Code   |          Identifier           |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                    Message Digest (256 bits)                  |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                              ...                              |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                      Message Digest (end)                     |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                     Authenticator (256 bits)                  |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                              ...                              |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                       Authenticator (end)                     |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                              ...                              |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```
- **Service-Id** - message target service identifier.
- **Length** - message length (header + body)
- **Flags**:
    - R flag - request message
    - S flag - secured message (has message digest)
    - E flag - error answer
- **Command Code** - corresponds to service Message type
- **Identifier** - message sequence identifier (starts from 0 for every new authenticated connection)
- **Message Digest** - message digest for message originator and body validation
- **Authenticator** - party authenticator issued by originator of auth request/answer message

###### Message Body

Message body is a sequence of AVP
```
	 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|D D L|       AVP Length        |   NS-Id   |      AVP Code     |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                             Data                              |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

- **DataType** - (0- OCTETS, 1- OBJECT, 2- INTEGER, 3- CHAR)
- **List flag** - means a grouping AVP that contains a sequence of AVP with same type
- **AVP Length** - length of AVP (hader + data)
- **Namespace-Id** - Namespace identifier, 0 means usage of parent Namespace
- **AVP Code** - AVP code, must unique identify AVP within Namespace
- **Data** - attribute value data (4-bytes aligned)


#### 4.3.5. Light-weight shell (lwsh)

##### 4.3.5.1. Configuration parameters

This service hasn't any configurable parameters

##### 4.3.5.2. Message types

|MsgType|Command|Description|
|-------|-------|-----------|
|1|INFO|Query current loaded script information |
|10|ADD| Add new script|
|11|REMOVE| Remove existing script|
|12|RUN| Run existing script (will load from source if needed)|
|13|DUMP| Dump byte-code of loaded existing script|
|14|SOURCE| Get source of existing script from Flash-DB|
|15|LOAD| Load existing script from source (Flash-DB)|
|16|LIST| List all stored scripts from Flash-DB|

#### 4.3.6. Network Time Protocol client (ntp)

##### 4.3.6.1. Configuration parameters

|Parameter|Level|Description|Default|
|---------|-----|-----------|-------|
|common.Time-Zone| 0 | Local Timezone (1/4 hours) | +0:00 |
|ntp.Poll-Interval| 0 |  Poll interval (minutes) | 15 |
|ntp.Peer| 0 |NTP Server peers (object list 0-2 items) | 0.pool.ntp.org, 1.pool.ntp.org |
|common.Host-Name| 1 | NTP Server host |  |

Example:
```
  { 
    "common.Time-Zone": "+3:00", 
    "ntp.Poll-Interval": 15, 
    "ntp.Peer": [
      { "common.Host-Name": "0.pool.ntp.org" }, 
      { "common.Host-Name": "1.pool.ntp.org" }
    ]
  }
```

##### 4.3.6.2. Message types

|MsgType|Command|Description|
|-------|-------|-----------|
|1|INFO|Query information (state, peers, etc.) |
|10|SETDATE| Query NTP peers and try to set local datetime|

#### 4.3.7. GPIO control management (gpioctl)

##### 4.3.7.1. Configuration parameters

This service hasn't any configurable parameters

##### 4.3.7.2. Message types

|MsgType|Command|Description|
|-------|-------|-----------|
|1|INFO|Query GPIO perepherial information |
|10|OUTPUT_SET| Set output parameters for GPIO PIN (value, delay, function)|

#### 4.3.8. Cron-like scheduler (sched)

##### 4.3.8.1. Configuration parameters

This service hasn't any configurable parameters

##### 4.3.8.2. Message types

|MsgType|Command|Description|
|-------|-------|-----------|
|1|INFO|Query scheduled task information |
|10|ADD| Add new task|
|11|REMOVE| Remove existing task|
|12|RUN| Run existing task|
|13|SOURCE| Get source of existing task from Flash-DB|
|14|LIST| List all stored tasks from Flash-DB|

#### 4.3.9. DHT11/AM2302 sensor (dev.dht)

##### 4.3.9.1. Configuration parameters

|Parameter|Level|Description|Default|
|---------|-----|-----------|-------|
|common.Perepherial-GPIO-Id| 0 | GPIO PIN id | 4 |
|dht.Sensor-Type| 0 | Sensor type (0- DHT11, 1- AM2302) | 0- DHT11 |
|dht.Stat-Timeout| 0 | Query data timeout for statistics (last, average) (seconds) | 20 |
|dht.Hist-Interval| 0 | - | 4 |
|dht.EMA-Alpha-Percent| 0 | Estimated moving average alpha | 90 |
|dht.Threshold-High| 0 | Increasing high threshold notification (object) | |
|dht.Humidity| 1 | Humidity PCT (1/100)  | |
|dht.Temperature| 1 | Temperature Celseus (1/100)  | |
|common.Milticast-Signal| 1 | Notification multicast signal_id (32-63)|
|dht.Threshold-Low| 0 | Decreasing low threshold notification (object)  | |
|dht.Humidity| 1 | Humidity PCT (1/100)  | |
|dht.Temperature| 1 | Temperature Celseus (1/100)  | |
|common.Milticast-Signal| 1 | Notification multicast signal_id (32-63)|

##### 4.3.9.2. Message types

|MsgType|Command|Description|
|-------|-------|-----------|
|1|INFO|Query service information (last data response, average statistics, etc.)|
|10|QUERY| Query data from sensor |


## 5. Examples

### 5.1. FAN Control Unit

#### 5.1.1. Scope

Bathroom FAN Control Unit with DHT11 humidity sensor and 1P solid state relay G3MB-202P.

Conrol Logic Goals
- Force turn on FAN when system startup and every daylight time hour. Turn off after 12 minutes timeout.
- Turn on/off FAN when humidity threshold crossed high/low. Regardless of humidity turn off after 30 minutes.
- Cool-down turn on/off humidity event by 5 minutes after last on/off event.


#### 5.1.2. Flash Initial Firmware Image

###### 1. Prerequisties
Connect ESP12E to host by serial cable.
Install required utilities
```
$ sudo pip install esptool miniterm
$ git clone https://github.com/xeronm/esp8266-tshcli.git
$ cd esp8266-tshcli
```

###### 2. Use esptool for query module `MAC` (in our example was `5ccf7f85e196`)
```
$  sudo esptool.py -p /dev/ttyUSB0 -b 115200 read_mac
esptool.py v2.5.0
Serial port /dev/ttyUSB0
Connecting....
Detecting chip type... ESP8266
Chip is ESP8266EX
Features: WiFi
MAC: 5c:cf:7f:85:e1:96
Uploading stub...
Running stub...
Stub running...
MAC: 5c:cf:7f:85:e1:96
Hard resetting via RTS pin...
```

###### 3. Flash Things-Shell firmware
```
$ cd ./bin
$ sudo esptool.py -p /dev/ttyUSB0 -b 115200 write_flash --flash_freq 80m --flash_mode dio --flash_size 32m --verify \
    0x00000 boot_v1.7.bin \
    0x01000 tsh-0.1.0-dev.spi4.app1.bin \
    0x7e000 blank.bin \
    0x3fc000 esp_init_data_default.bin \
    0x3fe000 blank.bin
```

###### 4. Connect to hidden WiFi AP `ESPTSH_85e196` (last 6 digit of MAC) with password `5ccf7f85e196` (MAC)
###### 5. Check that firmware and `udpctl` service works. Query system information
```
$ ./tcli.py -H 192.168.4.1 -s 5ccf7f85e196 system info
{
    "common.Event-Timestamp": "2018.12.04 20:16:55",
    "esp:common.Service-Message": {
        "common.Application-Product": "esp8266 Things Shell (c) 2018 dtec.pro",
        "common.Application-Version": "0.1.0-dev(657)",
        "esp.Firmware": {
            "esp.FW-Address": "0x081000",
            "esp.FW-Size-Map": 4,
            "esp.FW-Bin-Size": 331024,
            "esp.FW-Bin-Date": "2018.12.04 20:05:11",
            "esp.FW-User-Data-Address": "0x0fd000",
            "esp.FW-User-Data-Size": 3133440,
            "esp.FW-Release-Date": "2018.12.04 20:05:09",
            "esp.FW-Digest": "e99c2d1a4dbba34ec41809ee21da906aab0d735b1cca2acdbcd5b63509896133",
            "esp.FW-Init-Digest": "4646303030303030303030303030303030303030303030303030303030304646"
        },
        "esp.System": {
            "esp.System-SDK-Version": "2.2.0-dev(9422289)",
            "esp.System-Chip-ID": 8774038,
            "esp.System-Flash-ID": 1458400,
            "esp.System-Uptime": 239,
            "esp.Heap-Free-Size": 11832,
            "esp.System-Reset-Reason": 6,
            "esp.System-CPU-Frequence": 80,
            "esp.System-Boot-Loader-Version": 7
        }
    },
    "common.Result-Code": 1
}
```

#### 5.1.3. Configure System

###### 1. Setup Wi-Fi station mode and system description
```
$ ./tcli.py -H 192.168.5.86 -s 5ccf7f85e196 service config set -m '
{   
  "service.Service": [
      { 
          "service.Service-Id": 3, 
          "esp:common.Service-Configuration": {
              "common.System-Description": "Bathroom FAN#1",
              "esp.Wireless": {
                  "esp.WIFI-Station": {
                      "esp.WiFi-SSID": "MYROUTER",
                      "esp.WiFi-Password": "mypassword",
                      "esp.WiFi-Auto-Connect": 1
                  }
              }
          }
      } 
]}'
```

###### 2. Setup Time-Zone
```
$ ./tcli.py -H 192.168.5.86 -s 5ccf7f85e196 service config set -m '
{
  "service.Service": [
      { 
          "service.Service-Id": 6, 
          "ntp:common.Service-Configuration": {"common.Time-Zone": "+3:00"}
      } 
  ] 
}'
```

###### 3. Save configuration
```
$ ./tcli.py -H 192.168.5.86 -s 5ccf7f85e196 service config save -m '
{
  "service.Service": [
      { "service.Service-Id": 3 } ,
      { "service.Service-Id": 6 }
  ] 
}'
```


#### 5.1.4. Configure Control Logic and Schedule

Following terms were used:
- global variable `last_ev` - last state change event (0- reset state, 1-force power on, 2- humidity high threshold, 3- humidity low threshold, 4- power off timeout)
- global variable `last_dt` - last state change event date
- gpio pin `0` for FAN solid state relay; low-level on pin relates to open state on relay
- humidity turn on threshold: >= 36%
- humidity turn off threshold: < 36%
- used estimated moving average results from DHT sensor

###### 1. Make light-weight shell script with control logic rules. Solution is not optimal, may improved by using dht service thresholds and multicast signal handling
```
  ## last_dt; ## last_ev; # sdt := sysctime(); 
  (last_ev <= 0) ?? { gpio_set(0, 0); last_ev := 1; last_dt := sdt; print(last_ev) }; 	// set initial state, force power on

  # temp = 0; # hmd = 0; # res := ! dht_get(1, hmd, temp); 
  ((last_ev != 2) && res && (hmd >= 3600) && (last_dt + 300 < sdt)) ?? { gpio_set(0, 0); last_ev := 2; last_dt := sdt; print(last_ev) }; 	// humidity high threshold
  ((last_ev = 2) && res && (hmd < 3600) && (last_dt + 300 < sdt)) ?? { gpio_set(0, 1); last_ev := 3; last_dt := sdt; print(last_ev) };	// humidity low threshold
  ((last_ev = 1) && (last_dt + 720 < sdt) || (last_ev = 2) && res && (hmd < 4500) && (last_dt + 1800 < sdt)) ?? { gpio_set(0, 1); last_ev := 4; last_dt := sdt; print(last_ev) };	// power off timeout
```

###### 2. Add peristent named statement `fan_control` for common control logic
```
$ ./tcli.py -H 192.168.5.86 -s 5ccf7f85e196 lsh add -m '
{
  "lsh.Statement-Name": "fan_control",
  "lsh.Persistent-Flag": 1,
  "lsh.Statement-Text": "## last_dt; ## last_ev; # sdt := sysctime();\n(last_ev <= 0) ?? { gpio_set(0, 0); last_ev := 1; last_dt := sdt; print(last_ev) };\n\n# temp = 0; # hmd = 0; # res := !dht_get(1, hmd, temp);\n((last_ev != 2) && res && (hmd >= 3600) && (last_dt + 300 < sdt)) ?? { gpio_set(0, 0); last_ev := 2; last_dt := sdt; print(last_ev) };\n((last_ev = 2) && res && (hmd < 3600) && (last_dt + 300 < sdt)) ?? { gpio_set(0, 1); last_ev := 3; last_dt := sdt; print(last_ev) };\n((last_ev = 1) && (last_dt + 720 < sdt) || (last_ev = 2) && res && (hmd < 4500) && (last_dt + 1800 < sdt)) ?? { gpio_set(0, 1); last_ev := 4; last_dt := sdt; print(last_ev) };"
}'
```

###### 3. Add peristent named statement `fan_force_on` for force turn on by schedule, startup multicast signal, or manual run
```
$ ./tcli.py -H 192.168.5.86 -s 5ccf7f85e196 lsh add -m '
{
  "lsh.Statement-Name": "fan_force_on",
  "lsh.Persistent-Flag": 1,
  "lsh.Statement-Text": "## last_ev; ((last_ev != 1) && (last_ev != 2)) ?? { last_ev := 1; ## last_dt := sysctime(); gpio_set(0, 0); print(last_ev) }"
}'
```

###### 4. Perform simple tests
```
  # Force turn on when no initial state
$ ./tcli.py -H 192.168.5.86 -s 5ccf7f85e196 lsh run -m '{ "lsh.Statement-Name": "fan_control" }'
{
    "common.Event-Timestamp": "2018.12.07 08:37:26",
    "lsh:common.Service-Message": {
        "lsh.Exit-Code": 1,
        "lsh.Exit-Address": "0x0088"
    },
    "common.Result-Code": 1
}
  # Second force turn on before off timeout occurs
$ ./tcli.py -H 192.168.5.86 -s 5ccf7f85e196 lsh run -m '{ "lsh.Statement-Name": "fan_control" }'
{
    "common.Event-Timestamp": "2018.12.07 08:37:29",
    "lsh:common.Service-Message": {
        "lsh.Exit-Code": 0,
        "lsh.Exit-Address": "0x030c"
    },
    "common.Result-Code": 1
}
  # wait 10 minutes, must turn off after 10 minutes timeout
$ ./tcli.py -H 192.168.5.86 -s 5ccf7f85e196 lsh run -m '{ "lsh.Statement-Name": "fan_control" }'
{
    "common.Event-Timestamp": "2018.12.07 08:57:12",
    "lsh:common.Service-Message": {
        "lsh.Exit-Code": 1,
        "lsh.Exit-Address": "0x0308"
    },
    "common.Result-Code": 1
}
  # second exceution will no effect
$ ./tcli.py -H 192.168.5.86 -s 5ccf7f85e196 lsh run -m '{ "lsh.Statement-Name": "fan_control" }'
{
    "common.Event-Timestamp": "2018.12.07 08:57:14",
    "lsh:common.Service-Message": {
        "lsh.Exit-Code": 0,
        "lsh.Exit-Address": "0x030c"
    },
    "common.Result-Code": 1
}
  # force turn on
$ ./tcli.py -H 192.168.5.86 -s 5ccf7f85e196 lsh run -m '{ "lsh.Statement-Name": "fan_force_on" }'
{
    "common.Event-Timestamp": "2018.12.07 08:59:32",
    "lsh:common.Service-Message": {
        "lsh.Exit-Code": 0,
        "lsh.Exit-Address": "0x005c"
    },
    "common.Result-Code": 1
}
```

###### 5. Output results on uart port
```
[1200.868] [warn ][ntp] adjust time to: 2018.12.07 08:35:35+3:00 offset:-1.7
[1674.718] [info ][lwsh] load "fan_control"
[1674.719] [info ][lwsh] fan_control out: 1
[2400.828] [warn ][ntp] adjust time to: 2018.12.07 08:55:34+3:00 offset:0.982
[2499.240] [info ][lwsh] fan_control out: 4
[2639.283] [info ][lwsh] load "fan_force_on"
[2639.283] [info ][lwsh] fan_force_on out: 1
```

###### 6. Add schedules. `fan_force_on` - at system startup and every 30th minutes of 09 - 21 day hours. `fan_control` - at 15th seconds of every minute
```
$ ./tcli.py -H 192.168.5.86 -s 5ccf7f85e196 sched add -m '{
  "sched.Entry-Name": "fan_force_on",
  "sched.Persistent-Flag": 1,
  "sched.Schedule-String": "@0 0 30 9-21 * *",
  "sched.Statement-Name": "fan_force_on",
  "sched.Statement-Args": {}
}'
$ ./tcli.py -H 192.168.5.86 -s 5ccf7f85e196 sched add -m '{
  "sched.Entry-Name": "fan_control",
  "sched.Persistent-Flag": 1,
  "sched.Schedule-String": "1 * * * *",
  "sched.Statement-Name": "fan_control",
  "sched.Statement-Args": {}
}'
```

#### 5.1.5. Screenshots

![DYI Assembling-1][DYI_01]
![DYI Assembling-2][DYI_02]
![DYI Assembling-3][DYI_03]
![Chronograph Dashboard][Dashboard]

## 6. Appendix A: Known Bugs

1. #3: Sometimes NTP server returns local time wich is Posix epoch start time
2. #4: Read previous block state of Flash-DB on reboot after spi_flash_erase_sector & spi_flash_write (not reproduced)

## 7. Appendix B: Roadmap Improvements

1. #1: udpctl: send notification multicast signal messages to target host
2. #2: udpctl: disable softap by timeout after system start
3. #3: system: make safemode services startup after exception
4. #6: lsh: add arguments support

## 8. Appendix C: Memos

### 8.1. Indent
```
$ find ./ -name '*.h' -exec indent -l120 -brs -br -i4 -ci4 -di16 --no-tab -sc {} -o {} \;
$ find ./ -name '*.c' -exec indent -l120 -brs -br -i4 -ci4 -di16 --no-tab -sc {} -o {} \;
```
