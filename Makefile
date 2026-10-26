.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO is not set. Install devkitPro and devkitA64.)
endif

include $(DEVKITPRO)/libnx/switch_rules

TARGET      := SwitchWebFiles
BUILD       := build
SOURCES     := source
INCLUDES    :=

ARCH        := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft

CFLAGS      := -g -Wall -Wextra -O2 -ffunction-sections \
               -fdata-sections $(ARCH) $(INCLUDE) -D__SWITCH__
CFLAGS      += $(INCLUDES)

CXXFLAGS    := $(CFLAGS) -fno-rtti -fno-exceptions
ASFLAGS     := $(ARCH)

LDFLAGS     := -specs=$(DEVKITPRO)/libnx/switch.specs \
               -Wl,--gc-sections

LIBS        := -lnx

LIBDIRS     := $(PORTLIBS) $(LIBNX)

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)

export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))

CFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
OFILES      := $(CFILES:.c=.o)

.PHONY: all clean

all: $(BUILD)

$(BUILD):
	@mkdir -p $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@rm -rf $(BUILD) $(TARGET).nro $(TARGET).elf

else

DEPENDS     := $(OFILES:.o=.d)

all: $(OUTPUT).nro

$(OUTPUT).nro: $(OUTPUT).elf

$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif
