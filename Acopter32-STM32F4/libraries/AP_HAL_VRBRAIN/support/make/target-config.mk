# Board-specific configuration values.  Flash and SRAM sizes in bytes.

ifeq ($(BOARD), laserlab_MP32V1F1)
   MCU            := STM32F103VE
   PRODUCT_ID     := 0003
   ERROR_LED_PORT := GPIOC
   ERROR_LED_PIN  := 15
   DENSITY        := STM32_HIGH_DENSITY
   FLASH_SIZE     := 524288
   SRAM_SIZE      := 65536
endif

ifeq ($(BOARD), laserlab_MP32V3F1)
   MCU            := STM32F103VE
   PRODUCT_ID     := 0003
   ERROR_LED_PORT := GPIOC
   ERROR_LED_PIN  := 15
   DENSITY        := STM32_HIGH_DENSITY
   FLASH_SIZE     := 524288
   SRAM_SIZE      := 65536
endif

ifeq ($(BOARD), laserlab_MP32V1F4)
   MCU            := STM32F407VG
   PRODUCT_ID     := 0003
   ERROR_LED_PORT := GPIOC
   ERROR_LED_PIN  := 15
   DENSITY        := STM32_HIGH_DENSITY
   FLASH_SIZE     := 1048576
   SRAM_SIZE      := 131072
endif

# Memory target-specific configuration values

ifeq ($(MEMORY_TARGET), ram)
   LDSCRIPT := $(BOARD)/ram.ld
   VECT_BASE_ADDR := VECT_TAB_RAM
endif
ifeq ($(MEMORY_TARGET), flash)
   LDSCRIPT := $(BOARD)/flash.ld
   VECT_BASE_ADDR := VECT_TAB_FLASH
endif
ifeq ($(MEMORY_TARGET), jtag)
   LDSCRIPT := $(BOARD)/jtag.ld
   VECT_BASE_ADDR := VECT_TAB_BASE
endif

IMAGE_DIR        = $(SUPPORT_PATH)/templates
PRODUCT_BUNDLE   = $(BUILD_PATH)/laserlab_MP32V1F4.vrx
PRODUCT_BIN      = $(BUILD_PATH)/laserlab_MP32V1F4.bin

export MKFW      = $(SUPPORT_PATH)/scripts/px_mkfw.py

#
# Built product rules
#

$(PRODUCT_BUNDLE): $(PRODUCT_BIN)
	$(SILENT_VRB) $@
	$(V1) python $(MKFW) --prototype $(IMAGE_DIR)/$(BOARD).prototype \
		--image $< > $@

#
# Rules and tools for uploading firmware to various PX4 boards.
#

export UPLOADER	= $(SUPPORT_PATH)/scripts/px_uploader.py

SYSTYPE		:= $(shell uname -s)

#
# Serial port defaults.
#
# XXX The uploader should be smarter than this.
#
ifeq ($(SYSTYPE),Darwin)
SERIAL_PORTS		?= "/dev/tty.usbmodemPX*,/dev/tty.usbmodem*"
endif
ifeq ($(SYSTYPE),Linux)
SERIAL_PORTS		?= "/dev/serial/by-id/usb-3D_Robotics*"
endif
ifeq ($(SERIAL_PORTS),)
SERIAL_PORTS		 = "COM32,COM31,COM30,COM29,COM28,COM27,COM26,COM25,COM24,COM23,COM22,COM21,COM20,COM19,COM18,COM17,COM16,COM15,COM14,COM13,COM12,COM11,COM10,COM9,COM8,COM7,COM6,COM5,COM4,COM3,COM2,COM1,COM0"
endif

.PHONY: upload

upload:	all $(PRODUCT_BUNDLE) $(UPLOADER)
	$(V1) python -u $(UPLOADER) --port $(SERIAL_PORTS) $(PRODUCT_BUNDLE)
	
		