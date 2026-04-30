#include "hid_keyboard_report.h"

/*
 * USB HID boot keyboard report 工具函数。
 *
 * 标准 boot keyboard report 最多同时携带 6 个普通按键 keycode；
 * 因此下面所有查找/添加循环都固定遍历 keycode[6]。
 */

#include <string.h>

/* 清空 HID 键盘报告，使其表示“没有按键按下”。 */
void usb_hid_keyboard_report_clear(usb_hid_keyboard_report_t *report) {
  if (!report) {
    return;
  }

  memset(report, 0, sizeof(*report));
}

/* 向报告中加入一个 keycode；若已存在则视为成功。 */
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

/* 检查报告中是否包含某个 keycode。 */
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

/* 判断报告是否没有任何普通按键按下。 */
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
