#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static const char *event_type_name(unsigned short type) {
  switch (type) {
    case EV_SYN:
      return "EV_SYN";
    case EV_KEY:
      return "EV_KEY";
    case EV_REL:
      return "EV_REL";
    case EV_ABS:
      return "EV_ABS";
    default:
      return "EV_OTHER";
  }
}

static const char *key_code_name(unsigned short code) {
  switch (code) {
    case BTN_A:
      return "BTN_A";
    case BTN_B:
      return "BTN_B";
    case BTN_X:
      return "BTN_X";
    case BTN_Y:
      return "BTN_Y";
    case BTN_TL:
      return "BTN_TL";
    case BTN_TR:
      return "BTN_TR";
    case BTN_TL2:
      return "BTN_TL2";
    case BTN_TR2:
      return "BTN_TR2";
    case BTN_SELECT:
      return "BTN_SELECT";
    case BTN_START:
      return "BTN_START";
    case BTN_MODE:
      return "BTN_MODE";
    case BTN_THUMBL:
      return "BTN_THUMBL";
    case BTN_THUMBR:
      return "BTN_THUMBR";
    case BTN_TRIGGER:
      return "BTN_TRIGGER";
    case BTN_THUMB:
      return "BTN_THUMB";
    case BTN_THUMB2:
      return "BTN_THUMB2";
    case BTN_TOP:
      return "BTN_TOP";
    case BTN_TOP2:
      return "BTN_TOP2";
    case BTN_PINKIE:
      return "BTN_PINKIE";
    case BTN_BASE:
      return "BTN_BASE";
    case BTN_BASE2:
      return "BTN_BASE2";
    case BTN_BASE3:
      return "BTN_BASE3";
    case BTN_BASE4:
      return "BTN_BASE4";
    case BTN_BASE5:
      return "BTN_BASE5";
    case BTN_BASE6:
      return "BTN_BASE6";
    case BTN_DPAD_UP:
      return "BTN_DPAD_UP";
    case BTN_DPAD_DOWN:
      return "BTN_DPAD_DOWN";
    case BTN_DPAD_LEFT:
      return "BTN_DPAD_LEFT";
    case BTN_DPAD_RIGHT:
      return "BTN_DPAD_RIGHT";
    default:
      return "KEY_OTHER";
  }
}

static const char *abs_code_name(unsigned short code) {
  switch (code) {
    case ABS_X:
      return "ABS_X";
    case ABS_Y:
      return "ABS_Y";
    case ABS_Z:
      return "ABS_Z";
    case ABS_RX:
      return "ABS_RX";
    case ABS_RY:
      return "ABS_RY";
    case ABS_RZ:
      return "ABS_RZ";
    case ABS_HAT0X:
      return "ABS_HAT0X";
    case ABS_HAT0Y:
      return "ABS_HAT0Y";
    case ABS_HAT1X:
      return "ABS_HAT1X";
    case ABS_HAT1Y:
      return "ABS_HAT1Y";
    default:
      return "ABS_OTHER";
  }
}

static const char *event_code_name(unsigned short type, unsigned short code) {
  if (type == EV_KEY) {
    return key_code_name(code);
  }
  if (type == EV_ABS) {
    return abs_code_name(code);
  }
  return "-";
}

static void print_usage(const char *argv0) {
  printf("usage: %s [event-device]\n", argv0);
  printf("default: /dev/input/by-id/usb-081f_USB_gamepad-event-joystick\n");
}

int main(int argc, char **argv) {
  const char *device_path = "/dev/input/by-id/usb-081f_USB_gamepad-event-joystick";
  char device_name[256];
  int fd;

  if (argc > 2) {
    print_usage(argv[0]);
    return 1;
  }
  if (argc == 2) {
    if (strcmp(argv[1], "--help") == 0) {
      print_usage(argv[0]);
      return 0;
    }
    device_path = argv[1];
  }

  fd = open(device_path, O_RDONLY);
  if (fd < 0) {
    fprintf(stderr, "open %s failed: %s\n", device_path, strerror(errno));
    return 1;
  }

  memset(device_name, 0, sizeof(device_name));
  if (ioctl(fd, EVIOCGNAME(sizeof(device_name) - 1), device_name) < 0) {
    snprintf(device_name, sizeof(device_name), "unknown");
  }

  setvbuf(stdout, NULL, _IOLBF, 0);
  printf("device: %s\n", device_path);
  printf("name  : %s\n", device_name);
  printf("press buttons / d-pad / sticks; Ctrl-C to stop\n");
  printf("type,code,value,name\n");

  while (1) {
    struct input_event event;
    ssize_t bytes_read = read(fd, &event, sizeof(event));

    if (bytes_read < 0) {
      if (errno == EINTR) {
        continue;
      }
      fprintf(stderr, "read failed: %s\n", strerror(errno));
      close(fd);
      return 1;
    }
    if (bytes_read != (ssize_t)sizeof(event)) {
      fprintf(stderr, "short read: %zd bytes\n", bytes_read);
      close(fd);
      return 1;
    }

    if (event.type == EV_SYN) {
      continue;
    }

    printf("%s,%u,%d,%s\n",
           event_type_name(event.type),
           event.code,
           event.value,
           event_code_name(event.type, event.code));
  }
}
