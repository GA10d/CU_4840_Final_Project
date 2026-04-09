#include "usb_hid_keyboard.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define USB_CLASS_HID 0x03
#define USB_SUBCLASS_BOOT 0x01
#define USB_PROTOCOL_KEYBOARD 0x01

static void usb_hid_keyboard_device_close(usb_hid_keyboard_device_t *device) {
  if (!device || !device->connected) {
    return;
  }

  if (device->handle) {
    libusb_release_interface(device->handle, device->interface_number);
    libusb_close(device->handle);
  }

  memset(device, 0, sizeof(*device));
}

static int usb_hid_keyboard_find_endpoint(const struct libusb_interface_descriptor *desc,
                                          uint8_t *endpoint_address) {
  int i;

  for (i = 0; i < desc->bNumEndpoints; ++i) {
    const struct libusb_endpoint_descriptor *endpoint = &desc->endpoint[i];
    const uint8_t endpoint_type = endpoint->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK;
    const int is_interrupt_in = endpoint_type == LIBUSB_TRANSFER_TYPE_INTERRUPT &&
                                (endpoint->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) ==
                                    LIBUSB_ENDPOINT_IN;

    if (is_interrupt_in) {
      *endpoint_address = endpoint->bEndpointAddress;
      return 0;
    }
  }

  return -1;
}

static int usb_hid_keyboard_probe_device(libusb_device *usb_device,
                                         usb_hid_keyboard_device_t *device) {
  struct libusb_device_descriptor device_desc;
  struct libusb_config_descriptor *config = NULL;
  libusb_device_handle *handle = NULL;
  uint8_t endpoint_address = 0;
  int interface_number = -1;
  int rc;
  int interface_index;

  memset(device, 0, sizeof(*device));

  rc = libusb_get_device_descriptor(usb_device, &device_desc);
  if (rc != 0) {
    return rc;
  }

  rc = libusb_get_active_config_descriptor(usb_device, &config);
  if (rc != 0) {
    return rc;
  }

  for (interface_index = 0; interface_index < config->bNumInterfaces; ++interface_index) {
    const struct libusb_interface *interface = &config->interface[interface_index];

    if (interface->num_altsetting < 1) {
      continue;
    }

    const struct libusb_interface_descriptor *desc = &interface->altsetting[0];
    if (desc->bInterfaceClass != USB_CLASS_HID ||
        desc->bInterfaceSubClass != USB_SUBCLASS_BOOT ||
        desc->bInterfaceProtocol != USB_PROTOCOL_KEYBOARD) {
      continue;
    }

    if (usb_hid_keyboard_find_endpoint(desc, &endpoint_address) != 0) {
      continue;
    }

    interface_number = desc->bInterfaceNumber;
    break;
  }

  if (interface_number < 0) {
    libusb_free_config_descriptor(config);
    return -1;
  }

  rc = libusb_open(usb_device, &handle);
  if (rc != 0) {
    libusb_free_config_descriptor(config);
    return rc;
  }

  libusb_set_auto_detach_kernel_driver(handle, 1);

  rc = libusb_claim_interface(handle, interface_number);
  if (rc != 0) {
    libusb_close(handle);
    libusb_free_config_descriptor(config);
    return rc;
  }

  device->handle = handle;
  device->endpoint_address = endpoint_address;
  device->interface_number = interface_number;
  device->bus_number = libusb_get_bus_number(usb_device);
  device->device_address = libusb_get_device_address(usb_device);
  device->connected = 1;

  if (device_desc.iProduct != 0) {
    int len = libusb_get_string_descriptor_ascii(handle, device_desc.iProduct,
                                                 (unsigned char *)device->product_name,
                                                 (int)sizeof(device->product_name) - 1);
    if (len > 0) {
      device->product_name[len] = '\0';
    }
  }

  if (device->product_name[0] == '\0') {
    snprintf(device->product_name, sizeof(device->product_name),
             "bus%d-dev%d", device->bus_number, device->device_address);
  }

  libusb_free_config_descriptor(config);
  return 0;
}

int usb_hid_keyboard_manager_init(usb_hid_keyboard_manager_t *manager,
                                  size_t max_devices) {
  libusb_device **device_list = NULL;
  ssize_t device_count;
  ssize_t i;
  size_t limit;
  int rc;

  if (!manager) {
    return LIBUSB_ERROR_INVALID_PARAM;
  }

  memset(manager, 0, sizeof(*manager));

  rc = libusb_init(&manager->context);
  if (rc != 0) {
    return rc;
  }

  device_count = libusb_get_device_list(manager->context, &device_list);
  if (device_count < 0) {
    libusb_exit(manager->context);
    memset(manager, 0, sizeof(*manager));
    return (int)device_count;
  }

  limit = max_devices;
  if (limit > USB_HID_KEYBOARD_MAX_DEVICES) {
    limit = USB_HID_KEYBOARD_MAX_DEVICES;
  }

  for (i = 0; i < device_count && manager->device_count < limit; ++i) {
    usb_hid_keyboard_device_t candidate;
    if (usb_hid_keyboard_probe_device(device_list[i], &candidate) == 0) {
      manager->devices[manager->device_count++] = candidate;
    }
  }

  libusb_free_device_list(device_list, 1);

  if (manager->device_count == 0) {
    usb_hid_keyboard_manager_close(manager);
    return -1;
  }

  return 0;
}

void usb_hid_keyboard_manager_close(usb_hid_keyboard_manager_t *manager) {
  size_t i;

  if (!manager) {
    return;
  }

  for (i = 0; i < manager->device_count; ++i) {
    usb_hid_keyboard_device_close(&manager->devices[i]);
  }

  if (manager->context) {
    libusb_exit(manager->context);
  }

  memset(manager, 0, sizeof(*manager));
}

int usb_hid_keyboard_manager_poll(usb_hid_keyboard_manager_t *manager,
                                  usb_hid_keyboard_report_t *reports,
                                  size_t report_capacity,
                                  int timeout_ms) {
  size_t i;

  if (!manager || !reports) {
    return LIBUSB_ERROR_INVALID_PARAM;
  }

  if (report_capacity < manager->device_count) {
    return LIBUSB_ERROR_INVALID_PARAM;
  }

  for (i = 0; i < manager->device_count; ++i) {
    usb_hid_keyboard_device_t *device = &manager->devices[i];
    int transferred = 0;
    int rc;

    if (!device->connected) {
      memset(&reports[i], 0, sizeof(reports[i]));
      continue;
    }

    rc = libusb_interrupt_transfer(device->handle, device->endpoint_address,
                                   (unsigned char *)&device->last_report,
                                   (int)sizeof(device->last_report), &transferred,
                                   timeout_ms);

    if (rc == LIBUSB_ERROR_TIMEOUT) {
      reports[i] = device->last_report;
      continue;
    }

    if (rc == LIBUSB_ERROR_NO_DEVICE) {
      fprintf(stderr, "keyboard disconnected: %s\n", device->product_name);
      usb_hid_keyboard_device_close(device);
      memset(&reports[i], 0, sizeof(reports[i]));
      continue;
    }

    if (rc != 0) {
      fprintf(stderr, "keyboard poll error on %s: %s\n", device->product_name,
              libusb_error_name(rc));
      reports[i] = device->last_report;
      continue;
    }

    if (transferred != (int)sizeof(device->last_report)) {
      reports[i] = device->last_report;
      continue;
    }

    reports[i] = device->last_report;
  }

  return (int)manager->device_count;
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
