#ifndef HID_KEYBOARD_REPORT_H
#define HID_KEYBOARD_REPORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint8_t modifiers;
  uint8_t reserved;
  uint8_t keycode[6];
} usb_hid_keyboard_report_t;

void usb_hid_keyboard_report_clear(usb_hid_keyboard_report_t *report);
int usb_hid_keyboard_report_add_key(usb_hid_keyboard_report_t *report,
                                    uint8_t keycode);
int usb_hid_keyboard_report_contains(const usb_hid_keyboard_report_t *report,
                                     uint8_t keycode);
int usb_hid_keyboard_report_is_empty(const usb_hid_keyboard_report_t *report);

#ifdef __cplusplus
}
#endif

#endif
