#
# Copyright 2013-2014 Luke Dashjr
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 3 of the License, or (at your option)
# any later version.  See COPYING for more details.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=bfgminer
PKG_TITLE:=BFGMiner
PKG_VERSION:=5.3.0
PKG_RELEASE:=1

PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).txz
PKG_SOURCE_URL:=http://luke.dashjr.org/programs/bitcoin/files/$(PKG_NAME)/$(PKG_VERSION)/

PKG_INSTALL:=1

include $(INCLUDE_DIR)/package.mk

define Download/uthash
	FILE:=v1.9.8
	URL:=https://codeload.github.com/troydhanson/uthash/tar.gz/
endef
$(eval $(call Download,uthash))

define Package/$(PKG_NAME)/Default
	MAINTAINER:="Luke Dashjr" <luke_openwrt@dashjr.org>
	SECTION:=utils
	CATEGORY:=Utilities
	URL:=http://luke.dashjr.org/programs/$(PKG_NAME)
	DEPENDS:=+$(PKG_NAME)
endef

define Package/$(PKG_NAME)
$(call Package/$(PKG_NAME)/Default)
	TITLE:=$(PKG_TITLE) (Bitcoin miner)
	DEPENDS:=+libc +libcurl +libpthread +jansson +PACKAGE_$(PKG_NAME)_libevent:libevent2 +PACKAGE_$(PKG_NAME)_curses:libncurses +PACKAGE_$(PKG_NAME)_libmicrohttpd:libmicrohttpd +PACKAGE_$(PKG_NAME)_libusb:libusb-1.0
endef

define Package/$(PKG_NAME)/description
Modular Bitcoin ASIC/FPGA/GPU/CPU miner in C
endef

define Package/$(PKG_NAME)/config
config PACKAGE_$(PKG_NAME)_curses
	bool "Build with curses TUI support"
	depends on PACKAGE_$(PKG_NAME)
	default y
config PACKAGE_$(PKG_NAME)_libevent
	bool "Build with stratum server support"
	depends on PACKAGE_$(PKG_NAME)
	default y
config PACKAGE_$(PKG_NAME)_libmicrohttpd
	bool "Build with getwork server support (Block Erupter blades)"
	depends on PACKAGE_$(PKG_NAME)
	default y
config PACKAGE_$(PKG_NAME)_libusb
	bool "Build with libusb support (X6500 & ZTEX)"
	depends on PACKAGE_$(PKG_NAME)
	default y
config PACKAGE_$(PKG_NAME)_keccak
	bool "Build with Keccak algorithm support"
	depends on PACKAGE_$(PKG_NAME)
	default n
config PACKAGE_$(PKG_NAME)_scrypt
	bool "Build with scrypt algorithm support"
	depends on PACKAGE_$(PKG_NAME)
	default y
endef

ifndef CONFIG_PACKAGE_$(PKG_NAME)_curses
CONFIGURE_ARGS += --without-curses
else
CONFIGURE_ARGS += --with-curses=ncurses
endif

ifndef CONFIG_PACKAGE_$(PKG_NAME)_libevent
CONFIGURE_ARGS += --without-libevent
else
CONFIGURE_ARGS += --with-libevent
endif

ifndef CONFIG_PACKAGE_$(PKG_NAME)_libmicrohttpd
CONFIGURE_ARGS += --without-libmicrohttpd
else
CONFIGURE_ARGS += --with-libmicrohttpd
endif

ifndef CONFIG_PACKAGE_$(PKG_NAME)_libusb
CONFIGURE_ARGS += --without-libusb
endif

ifdef CONFIG_PACKAGE_$(PKG_NAME)_keccak
CONFIGURE_ARGS += --enable-keccak
endif

ifdef CONFIG_PACKAGE_$(PKG_NAME)_scrypt
CONFIGURE_ARGS += --enable-scrypt
endif

TARGET_CFLAGS += -std=gnu99
TARGET_CFLAGS += -Iuthash-1.9.8/src

CONFIGURE_ARGS += --without-libudev
CONFIGURE_ARGS += --without-sensors
CONFIGURE_ARGS += --without-system-libbase58

define Build/Prepare
	$(call Build/Prepare/Default)
	gzip -dc $(DL_DIR)/v1.9.8 | $(HOST_TAR) -C $(PKG_BUILD_DIR) $(TAR_OPTIONS)
endef

define Build/Configure
	# Need to remake configure etc to pick up on cross-compiler libtool
	( cd $(PKG_BUILD_DIR); NOSUBMODULES=1 ./autogen.sh; )
	
	$(call Build/Configure/Default)
endef

define Package/$(PKG_NAME)/install
	$(INSTALL_DIR) $(1)/usr/bin $(1)/usr/lib
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/bin/$(PKG_NAME) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/bin/$(PKG_NAME)-rpc $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/lib/libblkmaker*.so* $(1)/usr/lib
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/lib/libbase58*.so* $(1)/usr/lib
endef

ALL_$(PKG_NAME)_PACKAGES += $(PKG_NAME)


$(foreach bitstream,$(ALL_$(PKG_NAME)_PACKAGES),$(eval $(call BuildPackage,$(bitstream))))
