#---------------------------------------------------------------------------------
# PS5-Stream-3DS
# Stream PS5 gameplay (PBS Payload MJPEG) to New 3DS, with controller input back.
#---------------------------------------------------------------------------------
.SUFFIXES:
ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

TARGET   := ps5-stream-3ds
BUILD    := build
SOURCES  := source
DATA     := data
INCLUDES := source

ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mfpu=vfpv2 -mword-relocations

CFLAGS := -g -O2 -Wall -Wextra -ffunction-sections \
          $(ARCH) $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
          -D_3DS -DARM11 -D__ARM__

CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11
LDFLAGS  := $(ARCH) -Wl,--gc-sections,--as-needed

# libjpeg is a standard devkitPro 3DS portlib: pacman -S 3ds-libjpeg
LIBS := -ljpeg -lcitro2d -lcitro3d -lctru -lm

ICON := $(TOPDIR)/icon.png

.PHONY: clean all
all: $(TARGET).3dsx

CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
OFILES := $(addprefix $(BUILD)/,$(CFILES:.c=.o))
DEPS   := $(OFILES:.o=.d)

VPATH := $(SOURCES)

$(BUILD)/%.o: %.c Makefile | $(BUILD)
	@echo "compiling $(notdir $<)"
	$(CC) -MMD -MP -c $(CFLAGS) -o $@ $<

$(BUILD):
	@mkdir -p $@

$(TARGET).elf: $(OFILES)
	@echo "linking $@"
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

$(TARGET).3dsx: $(TARGET).elf
	@if [ -f $(ICON) ]; then \
		smdhtool --create "PS5 Stream 3DS" "Stream and control PS5 on 3DS" \
			"homebrew" $(ICON) $(TARGET).smdh; \
	fi
	@mkdir -p romfs
	@cp -r $(DATA)/* romfs/ 2>/dev/null || true
	3dsxtool $< $@ `test -f $(TARGET).smdh && echo --smdh=$(TARGET).smdh`

clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).3dsx $(TARGET).smdh romfs

-include $(DEPS)
