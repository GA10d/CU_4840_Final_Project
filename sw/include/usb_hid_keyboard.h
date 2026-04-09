#ifndef USB_HID_KEYBOARD_H
#define USB_HID_KEYBOARD_H

#include <stddef.h>
#include <stdint.h>

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
  uint8_t modifiers;
  uint8_t reserved;
  uint8_t keycode[6];
} usb_hid_keyboard_report_t;

typedef struct {
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

int usb_hid_keyboard_report_contains(const usb_hid_keyboard_report_t *report,
                                     uint8_t keycode);

#ifdef __cplusplus
}
#endif

#endif
