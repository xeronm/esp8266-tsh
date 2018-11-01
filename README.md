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


## Memos

### Indent
```
$ find ./ -name '*.h' -exec indent -l120 -brs -br -i4 -ci4 -di16 -sc {} -o {} \;
$ find ./ -name '*.c' -exec indent -l120 -brs -br -i4 -ci4 -di16 -sc {} -o {} \;
```
