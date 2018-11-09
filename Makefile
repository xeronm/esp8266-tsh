## Customizable Section: adapt those variables to suit your program.
##==========================================================================

# The executable file name.
# If not specified, current directory name or `a.out' will be used.
APP   = tsh

# The directories in which header files reside.
INCLUDES = ./include ./include/arch ./include/arch/xtensa ./include/arch/xtensa/driver

# The directories in which source files reside.
# If not specified, only the current directory will be serached.
SRCDIRS   = ./arch/xtensa ./arch/xtensa/driver ./crypto ./misc ./proto ./core ./system ./service ./service/device

# DEFINES
#    ENABLE_AT -
#    DISABLE_SERVICES -
#    DISABLE_CORE -
#    DISABLE_SYSTEM -
#    LOGGING_DEBUG -
#    LOGGIGN_DEBUG_MODE -
DEFINES = ICACHE_FLASH

# SPI Size and map
#    2=1024KB( 512KB+ 512KB)
#    3=2048KB( 512KB+ 512KB)
#    4=4096KB( 512KB+ 512KB)
#    5=2048KB(1024KB+1024KB)
#    6=4096KB(1024KB+1024KB)
SPI_MODE = 4

# Use SDK Image Tool
SDK_IMAGE_TOOL = 0

# ESP SDK Path
SDK_DIR = /src/esp-open-sdk/sdk
SDK_FLASH_BIN = blank.bin boot_v1.7.bin esp_init_data_default.bin
SDK_LIBS = c gcc hal pp phy net80211 lwip wpa main at smartconfig upgrade json airkiss ssl wps

XTENSA_DIR = /src/esp-open-sdk/xtensa-lx106-elf/xtensa-lx106-elf

INCLUDES_EXTRA = 

CFLAGS = $(addprefix -I,$(INCLUDES)) \
	-I$(SDK_DIR)/include \
	$(addprefix -I,$(INCLUDES_EXTRA)) \
	-mlongcalls -Wall \
	-mtext-section-literals \
	$(addprefix -D,$(DEFINES))
# The pre-processor options used by the cpp (man cpp for more).
CPPFLAGS  = -Wall

LDLIBS = -nostdlib -u call_user_start \
	-Wl,--start-group $(addprefix -l,$(SDK_LIBS)) -Wl,--end-group
LDFLAGS = -L$(SDK_DIR)/lib


## Implicit Section: change the following only when necessary.
##==========================================================================
BUILD_DIR = .build/
BINDIR = bin/

# The source file types (headers excluded).
# .c indicates C source files, and others C++ ones.
SRCEXTS = .c .cc .cpp .c++ .cxx .cp

# The header file types.
HDREXTS = .h .hh .hpp .h++ .hxx .hp

# C/C++ Compilers
CC = xtensa-lx106-elf-gcc
CXX = xtensa-lx106-elf-g++
OBJCOPY = xtensa-lx106-elf-objcopy

LD_APP_SUFFIX = 1
ifeq ($(SPI_MODE),2)
  IMAGE_SIZE = 512
  ESPTOOL_PARAMS = --flash_freq 80m --flash_mode dio --flash_size 8m
  LD_SCRIPT_PREFIX = eagle.app.v6.new.1024
  FLASH_ADD_ADDR = 0x7e000 blank.bin 0x3fc000 esp_init_data_default.bin 0x3fe000 blank.bin
endif
ifeq ($(SPI_MODE),3)
  IMAGE_SIZE = 512
  ESPTOOL_PARAMS = --flash_freq 80m --flash_mode dio --flash_size 16m
  LD_SCRIPT_PREFIX = eagle.app.v6.new.1024
  FLASH_ADD_ADDR = 0x7e000 blank.bin 0x3fc000 esp_init_data_default.bin 0x3fe000 blank.bin
endif
ifeq ($(SPI_MODE),4)
  IMAGE_SIZE = 512
  ESPTOOL_PARAMS = --flash_freq 80m --flash_mode dio --flash_size 32m
  LD_SCRIPT_PREFIX = eagle.app.v6.new.1024
  FLASH_ADD_ADDR = 0x7e000 blank.bin 0x3fc000 esp_init_data_default.bin 0x3fe000 blank.bin
endif
ifeq ($(SPI_MODE),5)
  LD_APP_SUFFIX = 0
  ESPTOOL_PARAMS = --flash_freq 80m --flash_mode dio --flash_size 16m
  IMAGE_SIZE = 1024
  LD_SCRIPT_PREFIX = eagle.app.v6.new.2048
  FLASH_ADD_ADDR = 0xfe000 blank.bin 0x3fc000 esp_init_data_default.bin 0x3fe000 blank.bin
endif
ifeq ($(SPI_MODE),6)
  LD_APP_SUFFIX = 0
  ESPTOOL_PARAMS = --flash_freq 80m --flash_mode dio --flash_size 32m
  IMAGE_SIZE = 1024
  LD_SCRIPT_PREFIX = eagle.app.v6.new.2048
  FLASH_ADD_ADDR = 0xfe000 blank.bin 0x3fc000 esp_init_data_default.bin 0x3fe000 blank.bin
endif

# Used shell command
RM     = rm -f
MKDIR  = mkdir -p
CP     = cp -v
MV     = mv -v
PYTHON = python
BININFO = $(PYTHON) ./scripts/bininfo.py
DIGEST = $(PYTHON) ./scripts/digest.py

## Stable Section: usually no need to be changed. But you can add more.
##==========================================================================
SHELL   = /bin/sh
EMPTY   =
SPACE   = $(EMPTY) $(EMPTY)
ifeq ($(APP),)
  CUR_PATH_NAMES = $(subst /,$(SPACE),$(subst $(SPACE),_,$(CURDIR)))
  APP = $(word $(words $(CUR_PATH_NAMES)),$(CUR_PATH_NAMES))
  ifeq ($(APP),)
    APP = a.out
  endif
endif
ifeq ($(SRCDIRS),)
  SRCDIRS = .
endif
SOURCES = $(foreach d,$(SRCDIRS),$(wildcard $(addprefix $(d)/*,$(SRCEXTS))))
HEADERS = $(foreach d,$(SRCDIRS),$(wildcard $(addprefix $(d)/*,$(HDREXTS))))
SRC_CXX = $(filter-out %.c,$(SOURCES))
OBJS    = $(addprefix $(BUILD_DIR),$(addsuffix .o, $(basename $(SOURCES))))
DEPS    = $(OBJS:.o=.d)

APP_SUFFIX = $(shell awk '/define\s*APP_VERSION_MAJOR/ {mj=$$3} \
    /define\s*APP_VERSION_MINOR/ {mn=$$3} \
    /define\s*APP_VERSION_PATCH/ {vp=$$3} \
    /define\s*APP_VERSION_SUFFIX/ {match($$3, "\"?([^\"]*)",r); vs=r[1]} \
    END {printf("%s.%s.%s%s", mj,mn,vp,vs)}' ./include/core/config.h )
#
ifneq ($(APP_SUFFIX),)
  APPS_TMP    = $(addprefix $(APP)-$(APP_SUFFIX).spi$(SPI_MODE).,app1 app2)
else
  APPS_TMP    = $(addprefix $(APP).spi$(SPI_MODE).,app1 app2)
endif

APPS  = $(addprefix $(BUILD_DIR),$(APPS_TMP))
APPS_INFO = $(addsuffix .info.json,$(APPS))
IMAGES  = $(addsuffix  .bin,$(addprefix $(BINDIR),$(APPS_TMP)))
SDK_IMAGES = $(addprefix  $(BINDIR),$(SDK_FLASH_BIN))
IMAGEINFO = $(BINDIR)$(APP)-$(APP_SUFFIX).spi$(SPI_MODE).info.json

## Define some useful variables.
DEP_OPT = $(shell if `$(CC) --version | grep "GCC" >/dev/null`; then \
                  echo "-MM -MP"; else echo "-M"; fi )

DEPEND      = $(CC)  $(DEP_OPT)  $(CFLAGS) $(CPPFLAGS)
DEPEND.d    = $(subst -g ,,$(DEPEND))
COMPILE.c   = $(CC)  $(CFLAGS)   -c
COMPILE.cxx = $(CXX) $(CXXFLAGS) $(CPPFLAGS) -c
LINK.c      = $(CC)  $(CFLAGS)   $(LDFLAGS)
LINK.cxx    = $(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS)

.PHONY: all objs clean cleanall show buildpath project image release reldate

# Delete the default suffixes
.SUFFIXES:

release: reldate clean all

reldate:
	@echo "*** Make Release ***" && \
          release_date=$$(date '+%s') && sed -i -e "s/\(^#\\s*define\\s*APP_VERSION_RELEASE_DATE\\s*\)\([0-9]*\)/\\1$$release_date/" ./include/core/config.h && \
	  echo "Release date: $$release_date"


all: buildpath project image


buildpath:
	$(MKDIR) $(BUILD_DIR) $(BINDIR)
	$(MKDIR) $(addprefix $(BUILD_DIR),$(SRCDIRS))

# Rules for generating object files (.o).
#----------------------------------------
objs:$(OBJS)

$(BUILD_DIR)%.o:%.c
	$(COMPILE.c) $< -o $@

$(BUILD_DIR)%.o:%.cc
	$(COMPILE.cxx) $< -o $@

$(BUILD_DIR)%.o:%.cpp
	$(COMPILE.cxx) $< -o $@

$(BUILD_DIR)%.o:%.c++
	$(COMPILE.cxx) $< -o $@

$(BUILD_DIR)%.o:%.cp
	$(COMPILE.cxx) $< -o $@

$(BUILD_DIR)%.o:%.cxx
	$(COMPILE.cxx) $< -o $@

# Rules for generating the image.
#-------------------------------------
image:$(IMAGES) $(IMAGEINFO) $(SDK_IMAGES)
	@echo '*******************************************************'
	@echo -e flash 512k:\\n\\t sudo esptool.py -p /dev/ttyUSB0 -b 115200 write_flash $(ESPTOOL_PARAMS) --verify \
  0x00000 boot_v1.7.bin \
  0x01000 $(APP)-$(APP_SUFFIX).spi$(SPI_MODE).app1.bin $(FLASH_ADD_ADDR) \\n
	@echo -e bootloader messages:\\n\\t sudo miniterm.py /dev/ttyUSB0 74880 \\n
	@echo -e AT commands:\\n\\t sudo miniterm.py /dev/ttyUSB0 115200 \\n
	@echo '*******************************************************'

$(SDK_IMAGES): $(BINDIR)%:$(SDK_DIR)/bin/%
	@$(CP) $^ $@

$(IMAGEINFO): $(APPS_INFO)
	$(BININFO) $^ $@

$(IMAGES): $(BINDIR)%.bin:$(BUILD_DIR)%
ifeq ($(SDK_IMAGE_TOOL),0)
	@esptool.py elf2image --version=2 $(ESPTOOL_PARAMS) -o $@ $^
	$(DIGEST) $^ $@
else
	$(eval APPID := $(subst .app,$(SPACE),$^))
	@echo gen_appbin.py: $(CURDIR)/$^ 2 0 0 $(SPI_MODE) $(word $(words $(APPID)),$(APPID))
	@cd $(BUILD_DIR) && \
          $(OBJCOPY) --only-section .irom0.text -O binary $(CURDIR)/$^ eagle.app.v6.irom0text.bin && \
          $(OBJCOPY) --only-section .text -O binary $(CURDIR)/$^ eagle.app.v6.text.bin && \
          $(OBJCOPY) --only-section .data -O binary $(CURDIR)/$^ eagle.app.v6.data.bin && \
          $(OBJCOPY) --only-section .rodata -O binary $(CURDIR)/$^ eagle.app.v6.rodata.bin && \
          export COMPILE=gcc && \
          $(PYTHON) $(SDK_DIR)/tools/gen_appbin.py $(CURDIR)/$^ 2 0 0 $(SPI_MODE) $(word $(words $(APPID)),$(APPID)) && \
          $(MV) eagle.app.flash.bin $(CURDIR)/$@
endif

# Rules for generating the executable.
#-------------------------------------
project: $(APPS)
	@echo Incrementing build number && \
	  build=$$(awk '/^#\s*define\s*BUILD_NUMBER/ {match($$0, "BUILD_NUMBER\\s*([0-9]*)", r); print r[1]+1}' ./include/core/config.h) && \
	  [ "$$build" != "" ] && sed -i -e "s/\(^#\\s*define\\s*BUILD_NUMBER\\s*\)\([0-9]*\)/\\1$$build/" ./include/core/config.h && \
	  echo "Next build: $$build"

$(APPS): %:$(OBJS)
ifeq ($(LD_APP_SUFFIX),1)
	$(eval LD_SCRIPT := $(addsuffix .ld, $(addprefix $(LD_SCRIPT_PREFIX), $(suffix $@))) )
else
	$(eval LD_SCRIPT := $(addsuffix .ld,$(LD_SCRIPT_PREFIX)))
endif
ifeq ($(SRC_CXX),)              # C program
	$(LINK.c)   $(OBJS) $(LDLIBS) -T$(SDK_DIR)/ld/$(LD_SCRIPT) -o $@
else                            # C++ program
	$(LINK.cxx) $(OBJS) $(LDLIBS) -T$(SDK_DIR)/ld/$(LD_SCRIPT) -o $@
endif

clean:
	$(RM) $(OBJS) $(APPS)

cleanall: clean
	$(RM) -r $(BUILD_DIR) $(BINDIR)

# Show variables (for debug use only.)
show:
ifeq ($(LD_APP_SUFFIX),1)
	$(eval LD_SCRIPTS := $(addsuffix .ld, $(addprefix $(LD_SCRIPT_PREFIX), $(suffix $(APPS)))) )
else
	$(eval LD_SCRIPTS := $(addsuffix .ld,$(LD_SCRIPT_PREFIX)))
endif
	@echo '*******************************************************'
	@echo 'SDK_DIR     :' $(SDK_DIR)
	@echo 'APP         :' $(APP)
	@echo 'APP_SUFFIX  :' $(APP_SUFFIX)
	@echo 'IMAGE_SIZE  :' $(IMAGE_SIZE)
	@echo 'APPS        :' $(APPS)
	@echo 'IMAGES      :' $(IMAGES)
	@echo 'IMAGEINFO   :' $(IMAGEINFO)
	@echo 'LD_SCRIPTS  :' $(LD_SCRIPTS)
	@echo 'SRCDIRS     :' $(SRCDIRS)
	@echo 'HEADERS     :' $(HEADERS)
	@echo 'SOURCES     :' $(SOURCES)
	@echo 'SRC_CXX     :' $(SRC_CXX)
	@echo 'OBJS        :' $(OBJS)
	@echo 'DEPS        :' $(DEPS)
	@echo 'DEPEND      :' $(DEPEND)
	@echo 'COMPILE.c   :' $(COMPILE.c)
	@echo 'COMPILE.cxx :' $(COMPILE.cxx)
	@echo 'link.c      :' $(LINK.c)
	@echo 'link.cxx    :' $(LINK.cxx)
	@echo '*******************************************************'
