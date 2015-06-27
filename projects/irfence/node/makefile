# Platform name  cc2420DK, firefly, micaZ
#PLATFORM = firefly2_2
PLATFORM = firefly3
#PLATFORM = firefly2_3


# Target file name (without extension).
TARGET = main

# Set the Port that you programmer is connected to 
PORT ?= 0 # Default FireFly port 
PROGRAMMING_PORT ?= /dev/ttyUSB$(PORT) # Default FireFly port 
# PROGRAMMING_PORT = /dev/ttyUSB0 # Default micaZ port 

# Set this such that the nano-RK directory is the base path
ROOT_DIR = ../../..

I2C_DIR = i2c

# Set platform specific defines 
# The following will be defined based on the PLATFORM variable:
# PROG_TYPE  (e.g. avrdude, or uisp)
# MCU (e.g. atmega32, atmega128, atmega1281) 
# RADIO (e.g. cc2420)
include $(ROOT_DIR)/include/platform.mk

CFG_HEADER = cfg.h
MODULES=$(shell sed -n -e "s/^\#define ENABLE_\(\w\+\)\s\+1/\1/p" $(CFG_HEADER))
enabled = $(filter $(1),$(MODULES))

SRC = $(TARGET).c

# Add extra source files. 
# For example:
SRC += $(ROOT_DIR)/src/net/bmac/$(RADIO)/bmac.c
SRC += $(ROOT_DIR)/src/drivers/platform/$(PLATFORM_TYPE)/source/adc_driver.c 
SRC += output.c
SRC += enum.c
SRC += options.c
SRC += time.c
SRC += parse.c
SRC += config.c
SRC += queue.c
SRC += nodelist.c
SRC += random.c

ifneq ($(call enabled,COMMAND),)
SRC += command.c
endif
ifneq ($(call enabled,LED),)
SRC += led.c
endif
ifneq ($(call enabled,RCMD),)
SRC += rcmd.c
endif
ifneq ($(call enabled,COMMAND),)
SRC += periodic.c
endif
ifneq ($(call enabled,BLINKER),)
SRC += blinker.c
endif
ifneq ($(call enabled,CONSOLE),)
SRC += console.c
endif
ifneq ($(call enabled,RXTX),)
SRC += packets.c
SRC += rxtx.c
endif
ifneq ($(call enabled,ROUTER),)
SRC += router.c
endif
ifneq ($(call enabled,RFTOP),)
SRC += rftop.c
SRC += dijkstra.c
endif
ifneq ($(call enabled,RPC),)
SRC += rpc.c
endif
ifneq ($(call enabled,MPING),)
SRC += mping.c
endif
ifneq ($(call enabled,RPING),)
SRC += rping.c
endif
ifneq ($(call enabled,TWI),)
SRC += $(I2C_DIR)/TWI_Master.c
SRC += twi.c
endif
ifneq ($(call enabled,IR),)
SRC += ir.c
endif
ifneq ($(call enabled,IRTOP),)
SRC += irtop.c
endif
ifneq ($(call enabled,LOCALIZATION),)
SRC += localization.c
endif
ifneq ($(call enabled,POSITION),)
SRC += position.c
endif
ifneq ($(call enabled,COMPASS),)
SRC += compass.c
endif
ifneq ($(call enabled,BEAM),)
SRC += beam.c
endif
ifneq ($(call enabled,FENCE),)
SRC += fence.c
endif
ifneq ($(call enabled,AUTOFENCE),)
SRC += autofence.c
endif

# Add extra includes files. 
# For example:
EXTRAINCDIRS =
EXTRAINCDIRS += $(ROOT_DIR)/src/net/bmac
ifneq ($(call enabled,TWI),)
EXTRAINCDIRS += $(I2C_DIR)
endif

#  This is where the final compile and download happens
include $(ROOT_DIR)/include/platform/$(PLATFORM)/common.mk
