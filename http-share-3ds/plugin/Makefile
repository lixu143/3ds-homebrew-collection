.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment (devkitPro pacman + 3ds-dev).")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

# libctrpf comes from the ThePixellizerOSS pacman repo (see README).
CTRPFLIB ?= $(DEVKITPRO)/libctrpf

TARGET  := HTTPShare
PLGINFO := HTTPShare.plgInfo

BUILD    := build
SOURCES  := source ../common
INCLUDES := Includes ../common

ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS   := $(ARCH) -Os -mword-relocations -fomit-frame-pointer \
            -ffunction-sections -fno-strict-aliasing
CFLAGS   += $(INCLUDE) -D__3DS__
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11
ASFLAGS  := $(ARCH)
LDFLAGS  := -T $(TOPDIR)/3gx.ld $(ARCH) -Os -Wl,--gc-sections,--strip-discarded,--strip-debug

LIBS    := -lctrpf -lctru -lm
LIBDIRS := $(CTRPFLIB) $(CTRULIB) $(PORTLIBS)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT  := $(CURDIR)/$(TARGET)
export TOPDIR  := $(CURDIR)
export VPATH   := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

export LD      := $(CXX)
export OFILES  := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)

.PHONY: all clean
all: $(BUILD)
$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
clean:
	@rm -fr $(BUILD) $(OUTPUT).3gx $(OUTPUT).elf

else

DEPENDS := $(OFILES:.o=.d)

$(OUTPUT).3gx: $(OUTPUT).elf

$(OUTPUT).elf: $(OFILES)
	@echo " linking $@"
	@$(LD) $(LDFLAGS) -o $@ $(OFILES) $(LIBPATHS) $(LIBS)

%.3gx: %.elf
	@echo " creating $(notdir $@)"
	@3gxtool -s $(word 1, $^) $(TOPDIR)/$(PLGINFO) $@

-include $(DEPENDS)
endif
