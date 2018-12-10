esp8266-tsh
===============
Light-weight Shell for ESP8266

# Scope

# References

# Definitions and Abbrevations

# Buld Firmware from scratch

## Prepare build environment

```
$ sudo docker pull dtec/esp8266:1.22-p
$ git clone https://github.com/xeronm/esp8266-tsh.git
$ cd esp8266-tsh
```

## Configure project

Configurables are:
- APP, SPI_MODE, SDK_IMAGE_TOOL in `Makefile`
- Global project defines in `./include/core/config.h`

## Buld Firmware

```
$ sudo docker run --name esp8266 -it --rm -v $PWD:/src/project dtec/esp8266:1.22-p
/src/project# make build
```


# Usage

## Service catalog

| Id | Name      | Description                  |
| ---| ----------| -----------------------------|
|  0 | multicast | multicast messaging point    |
|  1 | service   | service management           |
|  2 | syslog    | system message logging       |
|  3 | espadmin  | esp8266 system management    |
|  4 | udpctl    | UDP system management        |
|  5 | lwsh      | Light-weight shell           |
|  6 | ntp       | Network Time Protocol client |
|  7 | gpioctl   | GPIO control management      |
|  8 | sched     | Cron-like scheduler          |

## Service documentation

### System logging (syslog)

#### Configuration parameters

|Parameter|Level|Description|Default|
|---------|-----|-----------|-------|
|syslog.Log-Severity| 0 | Logging severity (1- critical, 2- error, 3- warning, 4- information, 5- debug) | 4- information |

Example:
```
  { "syslog.Log-Severity": 4 }
```

### esp8266 system management (espadmin)

#### Configuration parameters

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

### UDP system management (udpctl)

#### Configuration parameters

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


### Network Time Protocol client (ntp)

#### Configuration parameters

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

# Examples

## FAN Control Unit

### Scope

Bathroom FAN control unit with DHT11 sensor and 1P solid state relay.

Conrol Logic Goals
- Force turn on FAN when system startup and every day-time hour. Turn off after 10 minutes timeout.
- Turn on/off FAN when humidity reached high/low threshold. Regardless of humidity turn off after 20 minutes.
- Cool-down turn on/off humidity event by 5 minutes after last on/off event.

### Flash Initial Firmware Image

1. Connect ESP12E to host by serial cable
2. Use esptool for query module `MAC` (in our example was `5ccf7f85e196`)
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

3. Flash Things-Shell firmware
```
$ cd ./bin
$ sudo esptool.py -p /dev/ttyUSB0 -b 115200 write_flash --flash_freq 80m --flash_mode dio --flash_size 32m --verify \
    0x00000 boot_v1.7.bin \
    0x01000 tsh-0.1.0-dev.spi4.app1.bin \
    0x7e000 blank.bin \
    0x3fc000 esp_init_data_default.bin \
    0x3fe000 blank.bin
...
```

4. Connect to hidden WiFi AP `ESPTSH_85e196` (last 6 digit of MAC) with password `5ccf7f85e196` (MAC)
5. Check that firmware and `udpctl` service works. Query system information
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

### Configure System

1. Setup Wi-Fi station mode and system description
```
$ ./tcli.py -H 192.168.5.86 -s 5ccf7f85e196 service config set -m '{   
  "service.Service": [
      { 
          "service.Service-Id": 3, 
          "esp:common.Service-Configuration": {
              "common.System-Description": "Bathroom FAN#1",
              "esp.Wireless": {
                  "esp.WIFI-Station": {
                      "esp.WiFi-SSID": "DMHOME",
                      "esp.WiFi-Password": "d7bfeb9a3b2111e893fbc8f7330dd8c8",
                      "esp.WiFi-Auto-Connect": 1
                  }
              }
          }
      } 
]}'
```

2. Setup Time-Zone
```
$ ./tcli.py -H 192.168.5.86 -s 5ccf7f85e196 service config set -m '{   
  "service.Service": [
      { 
          "service.Service-Id": 6, 
          "ntp:common.Service-Configuration": {"common.Time-Zone": "+3:00"}
      } 
  ] 
}'
```

3. Save configuration
```
$ ./tcli.py -H 192.168.5.86 -s 5ccf7f85e196 service config save -m '{
  "service.Service": [
      { "service.Service-Id": 3 } ,
      { "service.Service-Id": 6 }
  ] 
}'
```


### Configure Script Logic and Schedule

Following terms were used:
- global variable `last_ev` - last state change event (0- reset state, 1-force power on, 2- humidity high threshold, 3- humidity low threshold, 4- power off timeout);
- global variable `last_dt` - last state change event date;
- gpio pin `0` for FAN solid state relay G3MB-202P; low-level on pin relates to open state on relay;
- humidity turn on threshold: 50%;
- humidity turn off threshold: 40%;
- we are using estimated moving average results from DHT sensor.

1. Make light-shell script with control rule logic. Solution is not optimal, may improved by using dht service thresholds and multicast signal handling.
```

## last_dt; ## last_ev; # sdt := sysctime(); 
(last_ev <= 0) ?? { gpio_set(0, 0); last_ev := 1; last_dt := sdt; print(last_ev) }; 	// set initial state, force power on

# temp = 0; # hmd = 0; # res := ! dht_get(1, hmd, temp); 
((last_ev != 2) && res && (hmd >= 3600) && (last_dt + 300 < sdt)) ?? { gpio_set(0, 0); last_ev := 2; last_dt := sdt; print(last_ev) }; 	// humidity high threshold
((last_ev = 2) && res && (hmd < 3600) && (last_dt + 300 < sdt)) ?? { gpio_set(0, 1); last_ev := 3; last_dt := sdt; print(last_ev) };	// humidity low threshold
((last_ev = 1) && (last_dt + 720 < sdt) || (last_ev = 2) && (last_dt + 1800 < sdt)) ?? { gpio_set(0, 1); last_ev := 4; last_dt := sdt; print(last_ev) };	// power off timeout

```

2. Add peristent named statement `fan_control`
```
$ ./tcli.py -H 192.168.5.86 -s 5ccf7f85e196 lsh add -m '{
  "lsh.Statement-Name": "fan_control",
  "lsh.Persistent-Flag": 1,
  "lsh.Statement-Text": "## last_dt; ## last_ev; # sdt := sysctime();\n(last_ev <= 0) ?? { gpio_set(0, 0); last_ev := 1; last_dt := sdt; print(last_ev) };\n\n# temp = 0; # hmd = 0; # res := !dht_get(1, hmd, temp);\n((last_ev != 2) && res && (hmd >= 3600) && (last_dt + 300 < sdt)) ?? { gpio_set(0, 0); last_ev := 2; last_dt := sdt; print(last_ev) };\n((last_ev = 2) && res && (hmd < 3600) && (last_dt + 300 < sdt)) ?? { gpio_set(0, 1); last_ev := 3; last_dt := sdt; print(last_ev) };\n((last_ev = 1) && (last_dt + 720 < sdt) || (last_ev = 2) && (last_dt + 1800 < sdt)) ?? { gpio_set(0, 1); last_ev := 4; last_dt := sdt; print(last_ev) };"
}'
```

3. Add peristent named statement `fan_force_on`
```
$ ./tcli.py -H 192.168.5.86 -s 5ccf7f85e196 lsh add -m '{
  "lsh.Statement-Name": "fan_force_on",
  "lsh.Persistent-Flag": 1,
  "lsh.Statement-Text": "## last_ev; ((last_ev != 1) && (last_ev != 2)) ?? { last_ev := 1; ## last_dt = sysctime(); gpio_set(0, 0); print(last_ev) }"
}'
```

4. Perform simple tests. 
4.1. Force turn on when no initial state
```
$ ./tcli.py -H 192.168.5.86 -s 5ccf7f85e196 lsh run -m '{ "lsh.Statement-Name": "fan_control" }'
{
    "common.Event-Timestamp": "2018.12.07 08:37:26",
    "lsh:common.Service-Message": {
        "lsh.Exit-Code": 1,
        "lsh.Exit-Address": "0x0088"
    },
    "common.Result-Code": 1
}

$ ./tcli.py -H 192.168.5.86 -s 5ccf7f85e196 lsh run -m '{ "lsh.Statement-Name": "fan_control" }'
{
    "common.Event-Timestamp": "2018.12.07 08:37:29",
    "lsh:common.Service-Message": {
        "lsh.Exit-Code": 0,
        "lsh.Exit-Address": "0x030c"
    },
    "common.Result-Code": 1
}
```

4.2. Turn off after 10 minutes timeout
```$ ./tcli.py -H 192.168.5.86 -s 5ccf7f85e196 lsh run -m '{ "lsh.Statement-Name": "fan_control" }'
{
    "common.Event-Timestamp": "2018.12.07 08:57:12",
    "lsh:common.Service-Message": {
        "lsh.Exit-Code": 1,
        "lsh.Exit-Address": "0x0308"
    },
    "common.Result-Code": 1
}

$ ./tcli.py -H 192.168.5.86 -s 5ccf7f85e196 lsh run -m '{ "lsh.Statement-Name": "fan_control" }'
{
    "common.Event-Timestamp": "2018.12.07 08:57:14",
    "lsh:common.Service-Message": {
        "lsh.Exit-Code": 0,
        "lsh.Exit-Address": "0x030c"
    },
    "common.Result-Code": 1
}
```

4.3. Force turn on
```$ ./tcli.py -H 192.168.5.86 -s 5ccf7f85e196 lsh run -m '{ "lsh.Statement-Name": "fan_force_on" }'
{
    "common.Event-Timestamp": "2018.12.07 08:59:32",
    "lsh:common.Service-Message": {
        "lsh.Exit-Code": 0,
        "lsh.Exit-Address": "0x005c"
    },
    "common.Result-Code": 1
}
```

4.4. Output results (uart port)
```
[1200.868] [warn ][ntp] adjust time to: 2018.12.07 08:35:35+3:00 offset:-1.7
[1674.718] [info ][lwsh] load "fan_control"
[1674.719] [info ][lwsh] fan_control out: 1
[2400.828] [warn ][ntp] adjust time to: 2018.12.07 08:55:34+3:00 offset:0.982
[2499.240] [info ][lwsh] fan_control out: 4
[2639.283] [info ][lwsh] load "fan_force_on"
[2639.283] [info ][lwsh] fan_force_on out: 1
```

5. Add schedule
- `fan_force_on` at system startup and every 30th minutes of 09 - 21 day hours
- `fan_control` at 15th seconds of every minute
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



# Appendix: Memos

## Indent
```
$ find ./ -name '*.h' -exec indent -l120 -brs -br -i4 -ci4 -di16 -sc {} -o {} \;
$ find ./ -name '*.c' -exec indent -l120 -brs -br -i4 -ci4 -di16 -sc {} -o {} \;
```
