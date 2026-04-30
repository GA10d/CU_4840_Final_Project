#ifndef USB_HID_KEYBOARD_H
#define USB_HID_KEYBOARD_H

/*
 * USB HID 键盘扫描接口。
 *
 * 使用 libusb 寻找 boot keyboard，并读取 interrupt IN endpoint 产生的
 * HID 键盘报告。最多管理两个键盘，对应双人输入。
 */

#include <stddef.h>
#include <stdint.h>

#include "hid_keyboard_report.h"

#if defined(__has_include)
#  if __has_include(<libusb-1.0/libusb.h>)
#    include <libusb-1.0/libusb.h>
#  elif __has_include(<libusb.h>)
#    include <libusb.h>
#  else
#    error "libusb headers not found"
#  endif
#else
#  include <libusb-1.0/libusb.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define USB_HID_KEYBOARD_MAX_DEVICES 2

typedef struct {
  /* 单个 USB 键盘设备的 libusb 句柄、端点和最近一次报告。 */
  libusb_device_handle *handle;
  uint8_t endpoint_address;
  int interface_number;
  int bus_number;
  int device_address;
  char product_name[128];
  usb_hid_keyboard_report_t last_report;
  int connected;
} usb_hid_keyboard_device_t;

typedef struct {
  /* 键盘管理器持有 libusb 上下文和已连接设备列表。 */
  libusb_context *context;
  usb_hid_keyboard_device_t devices[USB_HID_KEYBOARD_MAX_DEVICES];
  size_t device_count;
} usb_hid_keyboard_manager_t;

int usb_hid_keyboard_manager_init(usb_hid_keyboard_manager_t *manager,
                                  size_t max_devices);
void usb_hid_keyboard_manager_close(usb_hid_keyboard_manager_t *manager);

int usb_hid_keyboard_manager_poll(usb_hid_keyboard_manager_t *manager,
                                  usb_hid_keyboard_report_t *reports,
                                  size_t report_capacity,
                                  int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
