.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment (devkitPro pacman + 3ds-dev).")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

TARGET   := HTTPShare3DS
BUILD    := build
SOURCES  := source ../common
INCLUDES := source ../common
ROMFS    := romfs

ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS   := -g -Wall -O2 $(ARCH) -ffunction-sections -fno-strict-aliasing
CFLAGS   += $(INCLUDE) -D__3DS__
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions
ASFLAGS  := $(ARCH)
LDFLAGS  := $(ARCH) -specs=3dsx.specs -g -Wl,--gc-sections
LIBS     := -lcitro2d -lcitro3d -lctru -lm
LIBDIRS  := $(CTRULIB) $(PORTLIBS)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)
export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR  := $(CURDIR)/$(BUILD)
export INCLUDE  := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir))

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
export OFILES := $(CFILES:.c=.o) $(CPPFILES:.cpp=.o)

.PHONY: all clean
all: $(BUILD)
$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
clean:
	@rm -fr $(BUILD) $(TARGET).3dsx $(TARGET).elf $(TARGET).smdh

else

DEPENDS := $(OFILES:.o=.d)
$(OUTPUT).elf: $(OFILES)
	@echo " linking $@"
	@$(CC) $(LDFLAGS) -o $@ $(OFILES) $(LIBPATHS) $(LIBS)

$(OUTPUT).3dsx: $(OUTPUT).elf
	@echo " building $@"
	@3dsx $< $@

-include $(DEPENDS)
endif
