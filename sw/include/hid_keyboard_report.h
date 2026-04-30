#ifndef HID_KEYBOARD_REPORT_H
#define HID_KEYBOARD_REPORT_H

/*
 * USB HID boot keyboard report。
 *
 * boot protocol 报告固定包含 modifiers、reserved 和最多 6 个普通键 keycode。
 * keycode 用 uint8_t 是因为 HID usage ID 是 8 bit。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  /* modifiers 存 Ctrl/Shift/Alt 等修饰键位；普通按键放在 keycode[6]。 */
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
