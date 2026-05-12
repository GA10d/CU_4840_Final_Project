CC ?= gcc
PKG_CONFIG ?= pkg-config
BUILD_SIGNATURE_FILE := .build_signature
DEPFLAGS := -MMD -MP

HAVE_LIBUSB := $(shell command -v $(PKG_CONFIG) >/dev/null 2>&1 && \
	$(PKG_CONFIG) --exists libusb-1.0 && echo 1 || echo 0)

CPPFLAGS += -Iinclude
CPPFLAGS += -DFIGHTER_ENABLE_LIBUSB=$(HAVE_LIBUSB)
CFLAGS += -std=c11 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L
CFLAGS += -pthread
LDFLAGS += -pthread

COMMON_SRCS := \
	audio/fighter_audio.c \
	game/fighter_game.c \
	input/fighter_gamepad.c \
	input/fighter_input.c \
	input/hid_keyboard_report.c \
	fighter_animation.c \
	render_if/fighter_renderer.c
COMMON_OBJS := $(COMMON_SRCS:.c=.o)

PHASE1_TARGET := phase1_demo
PHASE1_SRCS := main_phase1_demo.c $(COMMON_SRCS)
PHASE1_OBJS := $(PHASE1_SRCS:.c=.o)

TEST_TARGET := phase1_test
TEST_SRCS := tests/test_phase1.c $(COMMON_SRCS)
TEST_OBJS := $(TEST_SRCS:.c=.o)

AUDIO_DEMO_TARGET := audio_demo
AUDIO_DEMO_SRCS := main_audio_demo.c audio/fighter_audio.c
AUDIO_DEMO_OBJS := $(AUDIO_DEMO_SRCS:.c=.o)

AUDIO_PROBE_TARGET := audio_probe
AUDIO_PROBE_SRCS := main_audio_probe.c
AUDIO_PROBE_OBJS := $(AUDIO_PROBE_SRCS:.c=.o)

VGA_PROBE_TARGET := vga_probe
VGA_PROBE_SRCS := main_vga_probe.c
VGA_PROBE_OBJS := $(VGA_PROBE_SRCS:.c=.o)

GAMEPAD_PROBE_TARGET := gamepad_probe
GAMEPAD_PROBE_SRCS := main_gamepad_probe.c
GAMEPAD_PROBE_OBJS := $(GAMEPAD_PROBE_SRCS:.c=.o)

PHASE1_USB_OBJS :=
USB_LDLIBS :=

LIBUSB_LIBS := $(shell command -v $(PKG_CONFIG) >/dev/null 2>&1 && \
	$(PKG_CONFIG) --libs libusb-1.0 2>/dev/null)
ifeq ($(HAVE_LIBUSB),1)
CPPFLAGS += $(shell $(PKG_CONFIG) --cflags libusb-1.0 2>/dev/null)
ifeq ($(strip $(LIBUSB_LIBS)),)
USB_LDLIBS := -lusb-1.0
else
USB_LDLIBS := $(LIBUSB_LIBS)
endif
PHASE1_USB_OBJS := input/usb_hid_keyboard.o
endif

.PHONY: all clean rebuild FORCE

DEPFILES := $(sort $(PHASE1_OBJS:.o=.d) $(TEST_OBJS:.o=.d) \
	$(AUDIO_DEMO_OBJS:.o=.d) $(AUDIO_PROBE_OBJS:.o=.d) \
	$(VGA_PROBE_OBJS:.o=.d) $(GAMEPAD_PROBE_OBJS:.o=.d) \
	$(PHASE1_USB_OBJS:.o=.d))

all: $(PHASE1_TARGET) $(TEST_TARGET) $(AUDIO_DEMO_TARGET) \
	$(AUDIO_PROBE_TARGET) $(VGA_PROBE_TARGET)

FORCE:

$(BUILD_SIGNATURE_FILE): Makefile FORCE
	@printf '%s\n' \
		'CC=$(CC)' \
		'CPPFLAGS=$(CPPFLAGS)' \
		'CFLAGS=$(CFLAGS)' \
		'LDFLAGS=$(LDFLAGS)' \
		'LDLIBS=$(LDLIBS)' \
		'USB_LDLIBS=$(USB_LDLIBS)' > $@.tmp
	@if ! test -f $@ || ! cmp -s $@.tmp $@; then \
		mv $@.tmp $@; \
	else \
		rm -f $@.tmp; \
	fi

%.o: %.c Makefile $(BUILD_SIGNATURE_FILE)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(PHASE1_TARGET): LDLIBS += $(USB_LDLIBS)
$(PHASE1_TARGET): $(COMMON_OBJS) main_phase1_demo.o $(PHASE1_USB_OBJS) \
	$(BUILD_SIGNATURE_FILE)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ \
		$(filter %.o,$^) $(LDLIBS)

$(TEST_TARGET): $(TEST_OBJS) $(BUILD_SIGNATURE_FILE)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ \
		$(filter %.o,$^) $(LDLIBS)

$(AUDIO_DEMO_TARGET): $(AUDIO_DEMO_OBJS) $(BUILD_SIGNATURE_FILE)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ \
		$(filter %.o,$^) $(LDLIBS)

$(AUDIO_PROBE_TARGET): $(AUDIO_PROBE_OBJS) $(BUILD_SIGNATURE_FILE)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ \
		$(filter %.o,$^) $(LDLIBS)

$(VGA_PROBE_TARGET): $(VGA_PROBE_OBJS) $(BUILD_SIGNATURE_FILE)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ \
		$(filter %.o,$^) $(LDLIBS)

$(GAMEPAD_PROBE_TARGET): $(GAMEPAD_PROBE_OBJS) $(BUILD_SIGNATURE_FILE)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ \
		$(filter %.o,$^) $(LDLIBS)

-include $(DEPFILES)

clean:
	$(RM) $(PHASE1_TARGET) $(TEST_TARGET) $(AUDIO_DEMO_TARGET) \
		$(AUDIO_PROBE_TARGET) $(VGA_PROBE_TARGET) \
		$(GAMEPAD_PROBE_TARGET) $(PHASE1_OBJS) \
		$(TEST_OBJS) $(AUDIO_DEMO_OBJS) $(AUDIO_PROBE_OBJS) \
		$(VGA_PROBE_OBJS) $(GAMEPAD_PROBE_OBJS) \
		$(PHASE1_USB_OBJS) \
		$(DEPFILES) $(BUILD_SIGNATURE_FILE)

rebuild: clean
	$(MAKE) all
