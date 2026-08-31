.DEFAULT_GOAL := all

PROJECT = blinky
BINARY = main
OPENCM3_DIR = ../libopencm3
DEVICE = stm32f103cbt6
CFILES = main.c
OBJS = $(CFILES:.c=.o)

LDFLAGS += -nostartfiles -lnosys

all: $(BINARY).elf $(BINARY).bin $(BINARY).hex

include $(OPENCM3_DIR)/mk/genlink-config.mk
include $(OPENCM3_DIR)/mk/genlink-rules.mk
include $(OPENCM3_DIR)/mk/gcc-config.mk
include $(OPENCM3_DIR)/mk/gcc-rules.mk