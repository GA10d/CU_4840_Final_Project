#include "hid_keyboard_report.h"

#include <string.h>

void usb_hid_keyboard_report_clear(usb_hid_keyboard_report_t *report) {
  if (!report) {
    return;
  }

  memset(report, 0, sizeof(*report));
}

int usb_hid_keyboard_report_add_key(usb_hid_keyboard_report_t *report,
                                    uint8_t keycode) {
  int i;

  if (!report || keycode == 0) {
    return -1;
  }

  for (i = 0; i < 6; ++i) {
    if (report->keycode[i] == keycode) {
      return 0;
    }
  }

  for (i = 0; i < 6; ++i) {
    if (report->keycode[i] == 0) {
      report->keycode[i] = keycode;
      return 0;
    }
  }

  return -1;
}

int usb_hid_keyboard_report_contains(const usb_hid_keyboard_report_t *report,
                                     uint8_t keycode) {
  int i;

  if (!report || keycode == 0) {
    return 0;
  }

  for (i = 0; i < 6; ++i) {
    if (report->keycode[i] == keycode) {
      return 1;
    }
  }

  return 0;
}

int usb_hid_keyboard_report_is_empty(const usb_hid_keyboard_report_t *report) {
  int i;

  if (!report) {
    return 1;
  }

  if (report->modifiers != 0) {
    return 0;
  }

  for (i = 0; i < 6; ++i) {
    if (report->keycode[i] != 0) {
      return 0;
    }
  }

  return 1;
}
