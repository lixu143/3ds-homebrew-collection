#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules
PREFIX ?= $(DEVKITARM)/bin/arm-none-eabi-
MAKEROM_BIN ?= makerom

#---------------------------------------------------------------------------------
TARGET  := HTTPShare_Module
BUILD   := build
SOURCES := source ../common
INCLUDES := source ../common
APP_TITLE       := HTTPShare
APP_PRODUCT_CODE:= CTR-H-HTTP
APP_UNIQUE_ID   := 0x5A17

APP_TITLE       := $(shell echo "$(APP_TITLE)" | cut -c1-128)
APP_PRODUCT_CODE:= $(shell echo $(APP_PRODUCT_CODE) | cut -c1-16)
APP_UNIQUE_ID   := $(shell echo $(APP_UNIQUE_ID) | cut -c1-8)

#---------------------------------------------------------------------------------
ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard

# A sysmodule heap is tiny: cap the socket buffer at 320 KiB.
CFLAGS := -Wall -Wextra -Wno-unused-parameter -O2 -mword-relocations \
          -fomit-frame-pointer -ffunction-sections -fdata-sections \
          -DSOC_BUFFERSIZE=0x50000 $(ARCH)

CFLAGS += $(INCLUDE) -D__3DS__

CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11
ASFLAGS  := -g $(ARCH)
LDFLAGS  = -specs=3dsx.specs $(ARCH) -Wl,--gc-sections

LIBS := -lctru -lm

LIBDIRS := $(CTRULIB) $(DEVKITPRO)/libctru $(DEVKITPRO)/portlibs/armv6k

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------
export OUTPUT  := $(CURDIR)/$(TARGET)
export TOPDIR  := $(CURDIR)
export VPATH   := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES  := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
SFILES  := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
export OFILES  := $(CFILES:.c=.o) $(SFILES:.s=.o)

export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean all

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -rf $(BUILD) $(TARGET).elf $(TARGET).cia $(TARGET)-stripped.elf

#---------------------------------------------------------------------------------
else

DEPENDS := $(OFILES:.o=.d)

.PHONY: all
all: $(OUTPUT).cia

$(OUTPUT).elf: $(OFILES)
	@echo " linking $@"
	@$(CC) $(LDFLAGS) -o $@ $(OFILES) $(LIBPATHS) $(LIBS)

$(OUTPUT)-stripped.elf: $(OUTPUT).elf
	@cp $(OUTPUT).elf $(OUTPUT)-stripped.elf
	@$(PREFIX)strip $(OUTPUT)-stripped.elf

# Build the sysmodule CIA with NO encryption and Category:Base from the RSF.
$(OUTPUT).cia: $(OUTPUT)-stripped.elf
	@MAKEROM=$(MAKEROM_BIN); \
	 if ! command -v $$MAKEROM >/dev/null 2>&1 && [ ! -x "$$MAKEROM" ]; then \
	   echo "  (CIA skipped: makerom not available in this toolchain)"; exit 0; \
	 fi; \
	 $$MAKEROM -f cia -o $(OUTPUT).cia \
		-rsf $(TOPDIR)/httpshare.rsf -target t -exefslogo \
		-elf $(OUTPUT)-stripped.elf \
		-DAPP_TITLE="$(APP_TITLE)" \
		-DAPP_PRODUCT_CODE="$(APP_PRODUCT_CODE)" \
		-DAPP_UNIQUE_ID="$(APP_UNIQUE_ID)"
	@echo "built ... $(notdir $@)"

-include $(DEPENDS)

endif
