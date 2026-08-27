# Makefile for rtw88-macos
#
# Prerequisites:
#   Xcode Command Line Tools
#   macOS SDK (comes with Xcode)
#   IOKit headers (in SDK)
#
# Usage:
#   make              — build kext bundle + rtw88ctl  →  build/out/
#   make kext         — build kext only
#   make ctl          — build rtw88ctl only
#   make install      — copy build/out/rtw88.kext to /Library/Extensions
#   make load         — kextutil (unsigned, requires SIP kext loading enabled)
#   make unload       — kextunload
#   make clean        — remove build/

MAKEFLAGS += -j$(shell sysctl -n hw.logicalcpu 2>/dev/null || echo 4)

# ------------------------------------------------------------------ #
# Paths                                                               #
# ------------------------------------------------------------------ #

PROJ_ROOT    := $(shell pwd)
LINUX_SRC    := $(PROJ_ROOT)/../rtw88-stable/drivers/net/wireless/realtek/rtw88
COMPAT_DIR   := $(PROJ_ROOT)/src/compat
KEXT_SRC     := $(PROJ_ROOT)/src/kext
FIRMWARE_DIR := $(PROJ_ROOT)/firmware
CTL_DIR      := $(PROJ_ROOT)/ctl

FLAVOR       ?= ethernet

ifneq ($(filter airport airport-legacy,$(FLAVOR)),)
ifeq ($(FLAVOR),airport-legacy)
BUILD_DIR    := $(PROJ_ROOT)/build-airport-legacy
else
BUILD_DIR    := $(PROJ_ROOT)/build-airport
endif
else
BUILD_DIR    := $(PROJ_ROOT)/build
endif
OUT_DIR      := $(BUILD_DIR)/out

# Source bundle: Info.plist + Resources skeleton (tracked in git)
ifneq ($(filter airport airport-legacy,$(FLAVOR)),)
ifeq ($(FLAVOR),airport-legacy)
KEXT_SKEL    := $(PROJ_ROOT)/AirportRtw88Legacy.kext
OUT_KEXT     := $(OUT_DIR)/AirportRtw88Legacy.kext
OUT_KEXT_BIN := $(OUT_KEXT)/Contents/MacOS/AirportRtw88Legacy
else
KEXT_SKEL    := $(PROJ_ROOT)/AirportRtw88.kext
OUT_KEXT     := $(OUT_DIR)/AirportRtw88.kext
OUT_KEXT_BIN := $(OUT_KEXT)/Contents/MacOS/AirportRtw88
endif
else
KEXT_SKEL    := $(PROJ_ROOT)/rtw88.kext
OUT_KEXT     := $(OUT_DIR)/rtw88.kext
OUT_KEXT_BIN := $(OUT_KEXT)/Contents/MacOS/rtw88
endif

# Output bundle: fully assembled kext ready for OpenCore / kextutil
OUT_CTL      := $(OUT_DIR)/rtw88ctl

# ------------------------------------------------------------------ #
# Toolchain                                                           #
# ------------------------------------------------------------------ #

SDK          := $(shell xcrun --show-sdk-path)
MKSDK        := $(PROJ_ROOT)/MacKernelSDK
ARCH         := -arch x86_64
MINOS        := -mmacosx-version-min=11.0

CC           := xcrun clang
CXX          := xcrun clang++
LD           := xcrun clang++

KEXT_FLAGS   := -fno-exceptions -fno-rtti \
                -fno-stack-protector -mkernel \
                $(ARCH) $(MINOS) \
                -isysroot $(SDK) \
                -I$(MKSDK)/Headers \
                -I$(SDK)/System/Library/Frameworks/Kernel.framework/Headers

ifeq ($(FLAVOR),airport)
KEXT_FLAGS += -DRTW88_AIRPORT=1 -DIO80211FAMILY_V2=1 \
              -D__IO80211_TARGET=__MAC_14_4
endif
ifeq ($(FLAVOR),airport-legacy)
KEXT_FLAGS += -DRTW88_AIRPORT=1 -DRTW88_AIRPORT_LEGACY=1 \
              -D__IO80211_TARGET=__MAC_13_0 -D__PRIVATE_SPI__=1
endif

# Compat include path overrides ALL linux/ and net/ headers
# so the Linux driver C files find our shims instead.
COMPAT_FLAGS := \
    -I$(COMPAT_DIR) \
    -I$(COMPAT_DIR)/linux \
    -I$(PROJ_ROOT)/src/kext

# C flags for Linux driver files
DRIVER_CFLAGS := \
    $(KEXT_FLAGS) \
    $(COMPAT_FLAGS) \
    -include $(COMPAT_DIR)/rtw88_compat.h \
    -I$(LINUX_SRC) \
    -DRTW88_MACOS=1 \
    -D__KERNEL__ \
    -DCONFIG_RTW88_8822BE=1 \
    -DCONFIG_RTW88_8822CE=1 \
    -DCONFIG_RTW88_8821CE=1 \
    -DCONFIG_RTW88_8812AE=1 \
    -DCONFIG_RTW88_8814AE=1 \
    -DCONFIG_RTW88_8821AU=1 \
    -DCONFIG_RTW88_8822BU=1 \
    -DCONFIG_RTW88_8822CU=1 \
    -DCONFIG_RTW88_8812AU=1 \
    -Wno-implicit-function-declaration \
    -Wno-int-conversion \
    -Wno-incompatible-pointer-types \
    -Wno-unused-variable \
    -Wno-unused-function

# C++ flags for kext wrapper files (-fapple-kext only for C++)
KEXT_CXXFLAGS := \
    $(KEXT_FLAGS) \
    -fapple-kext \
    $(COMPAT_FLAGS) \
    -std=c++17 \
    -DKERNEL \
    -Wno-deprecated-declarations \
    -Wno-nullability-completeness

ifneq ($(filter airport airport-legacy,$(FLAVOR)),)
KEXT_CXXFLAGS += -I$(PROJ_ROOT)/include
endif

# ------------------------------------------------------------------ #
# Source files                                                         #
# ------------------------------------------------------------------ #

# Linux driver core C files (compiled with compat headers)
DRIVER_SRCS := \
    $(LINUX_SRC)/main.c \
    $(LINUX_SRC)/mac.c \
    $(LINUX_SRC)/phy.c \
    $(LINUX_SRC)/fw.c \
    $(LINUX_SRC)/tx.c \
    $(LINUX_SRC)/rx.c \
    $(LINUX_SRC)/sec.c \
    $(LINUX_SRC)/efuse.c \
    $(LINUX_SRC)/coex.c \
    $(LINUX_SRC)/ps.c \
    $(LINUX_SRC)/regd.c \
    $(LINUX_SRC)/bf.c \
    $(LINUX_SRC)/sar.c \
    $(LINUX_SRC)/util.c \
    $(LINUX_SRC)/pci.c \
    $(LINUX_SRC)/usb.c \
    $(LINUX_SRC)/sdio.c \
    $(LINUX_SRC)/mac80211.c

# Chip-specific C files
CHIP_SRCS := \
    $(LINUX_SRC)/rtw8822b.c \
    $(LINUX_SRC)/rtw8822b_table.c \
    $(LINUX_SRC)/rtw8822be.c \
    $(LINUX_SRC)/rtw8822bu.c \
    $(LINUX_SRC)/rtw8822c.c \
    $(LINUX_SRC)/rtw8822c_table.c \
    $(LINUX_SRC)/rtw8822ce.c \
    $(LINUX_SRC)/rtw8822cu.c \
    $(LINUX_SRC)/rtw8821c.c \
    $(LINUX_SRC)/rtw8821c_table.c \
    $(LINUX_SRC)/rtw8821ce.c \
    $(LINUX_SRC)/rtw8821cu.c \
    $(LINUX_SRC)/rtw8812a.c \
    $(LINUX_SRC)/rtw8812a_table.c \
    $(LINUX_SRC)/rtw8812au.c \
    $(LINUX_SRC)/rtw8814a.c \
    $(LINUX_SRC)/rtw8814a_table.c \
    $(LINUX_SRC)/rtw8814ae.c \
    $(LINUX_SRC)/rtw8814au.c \
    $(LINUX_SRC)/rtw8821a.c \
    $(LINUX_SRC)/rtw8821a_table.c \
    $(LINUX_SRC)/rtw8821au.c \
    $(LINUX_SRC)/rtw88xxa.c

# Compat C implementation
COMPAT_SRCS := \
    $(COMPAT_DIR)/rtw88_compat.c

# Firmware loader — compiled with system headers only (no Linux compat headers)
# fw_blobs.c is auto-generated from firmware/*.bin before compilation
FIRMWARE_SRCS := \
    $(COMPAT_DIR)/rtw88_firmware.c \
    $(COMPAT_DIR)/fw_blobs.c

FW_BLOBS_C := $(COMPAT_DIR)/fw_blobs.c

# kmod_info.c — defines _kmod_info (required by kmutil/kextutil)
KMOD_SRCS := \
    $(KEXT_SRC)/kmod_info.c

# IOKit C++ wrapper
# RTW88USBDevice excluded: IOUSBHostFamily not in OSBundleLibraries
KEXT_SRCS := \
    $(KEXT_SRC)/RTW88Kext.cpp \
    $(KEXT_SRC)/RTW88PCIDevice.cpp \
    $(KEXT_SRC)/RTW88IEEE80211.cpp \
    $(KEXT_SRC)/RTW88UserClient.cpp

ifeq ($(FLAVOR),airport)
KEXT_SRCS += \
    $(KEXT_SRC)/RTW88Airport.cpp \
    $(KEXT_SRC)/RTW88AirportInterface.cpp \
    $(KEXT_SRC)/RTW88AirportSkywalkInterface.cpp
endif
ifeq ($(FLAVOR),airport-legacy)
KEXT_SRCS += \
    $(KEXT_SRC)/RTW88AirportLegacy.cpp \
    $(KEXT_SRC)/RTW88AirportLegacyInterface.cpp \
    $(KEXT_SRC)/RTW88TahoeNetworkABI.cpp
endif

# ------------------------------------------------------------------ #
# Object files                                                         #
# ------------------------------------------------------------------ #

DRIVER_OBJS   := $(patsubst $(LINUX_SRC)/%.c,  $(BUILD_DIR)/driver/%.o, $(DRIVER_SRCS))
CHIP_OBJS     := $(patsubst $(LINUX_SRC)/%.c,  $(BUILD_DIR)/driver/%.o, $(CHIP_SRCS))
COMPAT_OBJS   := $(patsubst $(COMPAT_DIR)/%.c, $(BUILD_DIR)/compat/%.o, $(COMPAT_SRCS))
FIRMWARE_OBJS := $(patsubst $(COMPAT_DIR)/%.c, $(BUILD_DIR)/compat/%.o, $(FIRMWARE_SRCS))
KMOD_OBJS   := $(patsubst $(KEXT_SRC)/%.c,   $(BUILD_DIR)/kext/%.o,   $(KMOD_SRCS))
KEXT_OBJS   := $(patsubst $(KEXT_SRC)/%.cpp, $(BUILD_DIR)/kext/%.o,   $(KEXT_SRCS))

ALL_OBJS    := $(DRIVER_OBJS) $(CHIP_OBJS) $(COMPAT_OBJS) $(FIRMWARE_OBJS) $(KMOD_OBJS) $(KEXT_OBJS)

# ------------------------------------------------------------------ #
# Linker flags                                                         #
# ------------------------------------------------------------------ #

KEXT_LDFLAGS := \
    $(ARCH) \
    -static \
    -nostdlib \
    -Xlinker -kext \
    $(MKSDK)/Library/x86_64/libkmod.a \
    $(MKSDK)/Library/universal/libkmodc++.a \
    -F$(SDK)/System/Library/Frameworks

# ------------------------------------------------------------------ #
# Targets                                                             #
# ------------------------------------------------------------------ #

.PHONY: all kext airport airport-legacy ctl install load unload clean clean-airport clean-airport-legacy

all: kext ctl

kext: $(OUT_KEXT_BIN)

# Experimental native IO80211/CoreWLAN target. It is assembled separately and
# never overwrites the proven Ethernet-mode artifact.
airport:
	$(MAKE) FLAVOR=airport kext

# Native AirPort interface using the Ventura IO80211 ABI restored on Tahoe by
# IO80211FamilyLegacy. Kept separate from both Ethernet and current Skywalk.
airport-legacy:
	$(MAKE) FLAVOR=airport-legacy kext

# Link, then assemble the full kext bundle in build/out/
$(OUT_KEXT_BIN): $(ALL_OBJS) | $(OUT_KEXT)/Contents/MacOS
	@echo "  LD   $(notdir $@)"
	$(LD) $(KEXT_LDFLAGS) -o $@ $(ALL_OBJS)
	@echo "  SYNC $(OUT_KEXT)"
	rsync -a --exclude='MacOS' $(KEXT_SKEL)/ $(OUT_KEXT)/
	@echo "  NOTE  run 'sudo chown -R root:wheel $(OUT_KEXT)' for kextutil"
	@echo "  KEXT $$(dwarfdump --uuid $(OUT_KEXT_BIN) 2>/dev/null)"
	@echo "  OK   $(patsubst $(PROJ_ROOT)/%,%,$(OUT_KEXT))"

# Compile Linux driver C files with compat headers
$(BUILD_DIR)/driver/%.o: $(LINUX_SRC)/%.c | $(BUILD_DIR)/driver
	@echo "  CC   $(notdir $<)"
	$(CC) $(DRIVER_CFLAGS) -c $< -o $@

# Compile compat C files (with Linux compat headers)
$(BUILD_DIR)/compat/%.o: $(COMPAT_DIR)/%.c | $(BUILD_DIR)/compat
	@echo "  CC   $(notdir $<)"
	$(CC) $(DRIVER_CFLAGS) -c $< -o $@

# Generate fw_blobs.c from firmware/*.bin (zlib-compressed embedded blobs)
$(FW_BLOBS_C): $(wildcard $(FIRMWARE_DIR)/*.bin) scripts/gen_fw_blobs.py
	@echo "  GEN  fw_blobs.c"
	python3 $(PROJ_ROOT)/scripts/gen_fw_blobs.py $(FIRMWARE_DIR) $@

# Compile firmware loader + blobs (system headers only — no compat header conflicts)
FW_CFLAGS := $(KEXT_FLAGS) \
    -I$(MKSDK)/Headers \
    -I$(SDK)/System/Library/Frameworks/Kernel.framework/Headers \
    -I$(COMPAT_DIR) \
    -Wno-unused-function

$(BUILD_DIR)/compat/rtw88_firmware.o: $(COMPAT_DIR)/rtw88_firmware.c | $(BUILD_DIR)/compat
	@echo "  CC   rtw88_firmware.c"
	$(CC) $(FW_CFLAGS) -c $< -o $@

$(BUILD_DIR)/compat/fw_blobs.o: $(COMPAT_DIR)/fw_blobs.c | $(BUILD_DIR)/compat
	@echo "  CC   fw_blobs.c"
	$(CC) $(FW_CFLAGS) -c $< -o $@

# Compile kmod_info.c (plain C, no compat headers)
$(BUILD_DIR)/kext/kmod_info.o: $(KEXT_SRC)/kmod_info.c | $(BUILD_DIR)/kext
	@echo "  CC   kmod_info.c"
	$(CC) $(KEXT_FLAGS) -c $< -o $@

# Compile kext C++ wrapper files
$(BUILD_DIR)/kext/%.o: $(KEXT_SRC)/%.cpp | $(BUILD_DIR)/kext
	@echo "  CXX  $(notdir $<)"
	$(CXX) $(KEXT_CXXFLAGS) -c $< -o $@

# ctl binary
ctl: $(OUT_CTL)

$(OUT_CTL): $(CTL_DIR)/main.c | $(OUT_DIR)
	@echo "  CC   rtw88ctl"
	$(CC) $(ARCH) $(MINOS) \
	    -isysroot $(SDK) \
	    -framework IOKit \
	    -framework CoreFoundation \
	    -o $@ $<
	@echo "  OK   build/out/rtw88ctl"

# ------------------------------------------------------------------ #
# Directory creation                                                  #
# ------------------------------------------------------------------ #

$(BUILD_DIR)/driver:
	mkdir -p $@

$(BUILD_DIR)/compat:
	mkdir -p $@

$(BUILD_DIR)/kext:
	mkdir -p $@

$(OUT_DIR):
	mkdir -p $@

$(OUT_KEXT)/Contents/MacOS:
	mkdir -p $@

# ------------------------------------------------------------------ #
# Install / load                                                      #
# ------------------------------------------------------------------ #

install: kext ctl
	@echo "Installing rtw88.kext to /Library/Extensions..."
	sudo cp -R $(OUT_KEXT) /Library/Extensions/
	sudo chown -R root:wheel /Library/Extensions/rtw88.kext
	sudo chmod -R 755 /Library/Extensions/rtw88.kext
	@echo "Installing rtw88ctl to /usr/local/bin..."
	sudo install -m 755 $(OUT_CTL) /usr/local/bin/rtw88ctl
	@echo "Done. Run 'make load' or reboot to activate."

load:
	@echo "Loading rtw88.kext (requires SIP kext loading enabled)..."
	sudo kextutil -v $(OUT_KEXT)

unload:
	@echo "Unloading rtw88.kext..."
	sudo kextunload -b com.rtw88.driver

clean:
	rm -rf $(BUILD_DIR)

clean-airport:
	rm -rf $(PROJ_ROOT)/build-airport

clean-airport-legacy:
	rm -rf $(PROJ_ROOT)/build-airport-legacy

# ------------------------------------------------------------------ #
# OpenCore injection helper                                           #
# ------------------------------------------------------------------ #
# To use with OpenCore:
#   1. Copy build/out/rtw88.kext to OC/Kexts/
#   2. Add to config.plist -> Kernel -> Add:
#      Arch: Any
#      BundlePath: rtw88.kext
#      Comment: Realtek rtw88 WiFi
#      Enabled: YES
#      ExecutablePath: Contents/MacOS/rtw88
#      MaxKernel: (empty)
#      MinKernel: 20.0.0   (macOS 11+)
#      PlistPath: Contents/Info.plist
#   3. Rebuild OC cache: sudo kextcache -i /
#
# BaseSystem / Recovery notes:
#   The kext loads via OpenCore injection before the OS, so it works
#   in BaseSystem automatically.  rtw88ctl is a standalone binary;
#   copy it to the USB installer's /usr/local/bin or run from a path.
