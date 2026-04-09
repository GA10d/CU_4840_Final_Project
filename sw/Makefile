CC ?= gcc
PKG_CONFIG ?= pkg-config

HAVE_LIBUSB := $(shell command -v $(PKG_CONFIG) >/dev/null 2>&1 && \
	$(PKG_CONFIG) --exists libusb-1.0 && echo 1 || echo 0)

CFLAGS += -std=c11 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L -Iinclude
CFLAGS += -DFIGHTER_ENABLE_LIBUSB=$(HAVE_LIBUSB)

COMMON_SRCS := \
	audio/fighter_audio.c \
	game/fighter_game.c \
	input/fighter_input.c \
	input/hid_keyboard_report.c \
	render_if/fighter_mmio.c \
	render_if/fighter_renderer.c
COMMON_OBJS := $(COMMON_SRCS:.c=.o)

PHASE1_TARGET := phase1_demo
PHASE1_SRCS := main_phase1_demo.c $(COMMON_SRCS)
PHASE1_OBJS := $(PHASE1_SRCS:.c=.o)

TEST_TARGET := phase1_test
TEST_SRCS := tests/test_phase1.c $(COMMON_SRCS)
TEST_OBJS := $(TEST_SRCS:.c=.o)

INPUT_DEMO_TARGET := input_demo
INPUT_DEMO_SRCS := main_input_demo.c input/usb_hid_keyboard.c input/fighter_input.c \
	input/hid_keyboard_report.c
INPUT_DEMO_OBJS := $(INPUT_DEMO_SRCS:.c=.o)

LIBUSB_LIBS := $(shell command -v $(PKG_CONFIG) >/dev/null 2>&1 && \
	$(PKG_CONFIG) --libs libusb-1.0 2>/dev/null)
ifeq ($(HAVE_LIBUSB),1)
CFLAGS += $(shell $(PKG_CONFIG) --cflags libusb-1.0 2>/dev/null)
ifeq ($(strip $(LIBUSB_LIBS)),)
USB_LDLIBS := -lusb-1.0
else
USB_LDLIBS := $(LIBUSB_LIBS)
endif
PHASE1_USB_OBJS := input/usb_hid_keyboard.o
USB_TARGETS := $(INPUT_DEMO_TARGET)
endif

.PHONY: all clean

all: $(PHASE1_TARGET) $(TEST_TARGET) $(USB_TARGETS)

$(PHASE1_TARGET): $(COMMON_OBJS) main_phase1_demo.o $(PHASE1_USB_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(USB_LDLIBS)

$(TEST_TARGET): $(TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(INPUT_DEMO_TARGET): $(INPUT_DEMO_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(USB_LDLIBS)

clean:
	rm -f $(PHASE1_TARGET) $(TEST_TARGET) $(INPUT_DEMO_TARGET) \
		$(PHASE1_OBJS) $(TEST_OBJS) $(INPUT_DEMO_OBJS)
