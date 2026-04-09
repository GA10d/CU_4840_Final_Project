CC ?= gcc
PKG_CONFIG ?= pkg-config

TARGET := input_demo
SRCS := main_input_demo.c input/usb_hid_keyboard.c input/fighter_input.c
OBJS := $(SRCS:.c=.o)

CFLAGS += -std=c11 -Wall -Wextra -Werror -Iinclude
CFLAGS += $(shell $(PKG_CONFIG) --cflags libusb-1.0 2>/dev/null)

LIBUSB_LIBS := $(shell $(PKG_CONFIG) --libs libusb-1.0 2>/dev/null)
ifeq ($(strip $(LIBUSB_LIBS)),)
LDLIBS += -lusb-1.0
else
LDLIBS += $(LIBUSB_LIBS)
endif

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)

clean:
	rm -f $(TARGET) $(OBJS)
