#---------------------------------------------------------------------------------
# NFC Toolbox for New 3DS
# 目标: 3dsx (Homebrew Launcher) 与 cia (FBI 安装)
#---------------------------------------------------------------------------------
TARGET	:= nfc-toolbox-3ds
BUILD	:= build
SOURCE	:= source
INCLUDE	:= $(SOURCE)
ROMFS	:= romfs

SOURCES	:= $(wildcard $(SOURCE)/*.c)
OBJECTS	:= $(SOURCES:.c=.o)
DEPENDS	:= $(OBJECTS:.o=.d)

# 使用的库：citro2d 依赖 citro3d
LIBS	:= -lcitro2d -lcitro3d -lctru -lm

#---------------------------------------------------------------------------------
# devkitPro 标准 3DS 工具链配置
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment (install devkitPro pacman + 3ds-dev).")
endif

DEVKITPRO	?= $(DEVKITARM)/../..

PREFIX		:= $(DEVKITARM)/bin/arm-none-eabi-
CC			:= $(PREFIX)gcc
CXX			:= $(PREFIX)g++
AR			:= $(PREFIX)gcc-ar
OBJCOPY		:= $(PREFIX)objcopy

export PATH := $(DEVKITARM)/bin:$(PATH)

ARCH	:= -march=armv6k -mtune=mpcore -mfloat-abi=hard -mfpu=vfpv2 -mword-relocations
CFLAGS	:= -g -O2 -Wall -Wextra -MMD -MP -fno-omit-frame-pointer $(ARCH) \
		   -D__3DS__ -DARM11 -D_ARM11_ $(addprefix -I,$(INCLUDE)) \
		   -I$(DEVKITPRO)/libctru/include
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11
ASFLAGS	:= $(ARCH)
LDFLAGS	:= $(ARCH) -specs=3dsx.specs -g -Wl,--gc-sections

# 3dsx 打包工具 (name differs across devkitPro versions: 3dsxtool / 3dsx)
3DSX := $(if $(wildcard $(DEVKITPRO)/tools/bin/3dsxtool),$(DEVKITPRO)/tools/bin/3dsxtool,$(DEVKITPRO)/tools/bin/3dsx)
LIBDIRS	:= $(DEVKITPRO)/libctru/lib $(DEVKITPRO)/portlibs/3ds/lib

#---------------------------------------------------------------------------------
# 构建目标
#---------------------------------------------------------------------------------
.PHONY: all clean cia

all: $(BUILD)/$(TARGET).3dsx

$(BUILD):
	@mkdir -p $@

# 编译
$(SOURCE)/%.o: $(SOURCE)/%.c
	@echo " CC $<"
	@$(CC) -MMD -MP -MF $(SOURCE)/$*.d $(CFLAGS) -c $< -o $@

# 链接 .elf
$(BUILD)/$(TARGET).elf: $(OBJECTS) | $(BUILD)
	@echo " LINK $@"
	@$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(addprefix -L,$(LIBDIRS)) $(LIBS)

# 生成 .3dsx (3dsx 工具打包 elf + romfs)
$(BUILD)/$(TARGET).3dsx: $(BUILD)/$(TARGET).elf
	@echo " 3DSX $@"
	@$(3DSX) -o $@ $< --romfs=$(ROMFS)

# 生成 .cia (需安装 bannertool + makerom)
cia: $(BUILD)/$(TARGET).elf
	@echo " CIA (requires bannertool + makerom)"
	@$(OBJCOPY) -S -O binary $< $(BUILD)/$(TARGET).bin
	@makerom -f cia -o $(BUILD)/$(TARGET).cia \
		-elf $< -target t -exefslogo -rsf $(BUILD)/$(TARGET).rsf 2>/dev/null || \
		echo "  (CIA build skipped: provide .rsf/icon/banner or use FBI remote install of 3dsx)"

clean:
	@rm -rf $(BUILD) $(OBJECTS) $(DEPENDS)
	@echo " cleaned"

-include $(DEPENDS)
