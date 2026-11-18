#---------------------------------------------------------------------------------
# IPTV-3DS
# Public free IPTV client for Nintendo 3DS / New 3DS (no PC relay).
#---------------------------------------------------------------------------------
.SUFFIXES:
ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

TARGET		:=	iptv-3ds
BUILD		:=	build
SOURCES		:=	source
DATA		:=	data
INCLUDES	:=	source

# FFmpeg 3DS port (ThirdTube / Video player for 3DS branch) with MVD hwdec.
# Point these at your local build.  See README.md.
FFMPEG_DIR	?=	$(TOPDIR)/library/FFmpeg
# Uncomment to try MVD hardware decode (needs the patched FFmpeg build):
# CFLAGS		+=	-DIPTV_FFMPEG_MVD

ICON		:=	$(TOPDIR)/icon.png

VERSION		:=	1
UID			:=	0x5A18

#---------------------------------------------------------------------------------
ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mfpu=vfpv2 -mword-relocations

CFLAGS	:=	-g -O2 -Wall -Wextra -ffunction-sections \
			$(ARCH) $(INCLUDES) \
			-I$(FFMPEG_DIR)/include \
			-D_3DS -DARM11 -D__ARM__ $(CFLAGS)

CXXFLAGS:=	$(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11

LDFLAGS	:=	$(ARCH) -Wl,--gc-sections,--as-needed

LIBS	:=	-lcurl -lmbedtls -lmbedcrypto -lmbedx509 \
			-lavformat -lavcodec -lavutil -lswscale -lswresample \
			-lz -lm \
			-lcitro2d -lcitro3d -lctru -lm

#---------------------------------------------------------------------------------
.PHONY: clean all

all: $(TARGET).3dsx

CFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
OFILES	:=	$(addprefix $(BUILD)/,$(CFILES:.c=.o))
DEPS	:=	$(OFILES:.o=.d)

VPATH	:=	$(SOURCES)

$(BUILD)/%.o: %.c Makefile | $(BUILD)
	@echo "compiling $(notdir $<)"
	$(CC) -MMD -MP -c $(CFLAGS) -o $@ $<

$(BUILD):
	@mkdir -p $@

$(TARGET).elf: $(OFILES)
	@echo "linking $@"
	$(CC) $(LDFLAGS) -o $@ $^ -L$(FFMPEG_DIR)/lib $(LIBS)

# icon.png is optional: provide one for a nice Home Menu icon, otherwise
# the .3dsx still builds (it just won't have a custom SMDH).
$(TARGET).3dsx: $(TARGET).elf
	@if [ -f $(ICON) ]; then \
		smdhtool --create "IPTV-3DS" "Public free channels on 3DS" \
			"homebrew" $(ICON) $(TARGET).smdh; \
	fi
	@mkdir -p romfs
	@cp -r $(DATA)/* romfs/ 2>/dev/null || true
	3dsxtool $< $@ --romfs=romfs `test -f $(TARGET).smdh && echo --smdh=$(TARGET).smdh`

clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).3dsx $(TARGET).smdh romfs
