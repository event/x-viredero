/*
 * X11 state change collector for viredero
 * Copyright (c) 2015 Leonid Movshovich <event.riga@gmail.com>
 *
 *
 * viredero is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * viredero is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with viredero; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <unistd.h>
#include <syslog.h>

#include <arpa/inet.h>

#include <libusb-1.0/libusb.h>
#include "x-viredero.h"


#define USB_MANUFACTURER "Leonid Movshovich"
#define USB_MODEL "x-viredero"
#define USB_DESCRIPTION "viredero is a virtual reality desktop view"
#define USB_VERSION "2.1"
#define USB_URI "http://play.google.com/"
#define USB_SERIAL_NUM "130"
#define USB_XFER_TIMEO_MSEC 1000
#define USB_CHECK_TIMEO_MSEC 10
#define USB_ACCESSORY_VID 0x18D1
#define USB_ACCESSORY_PID_MASK 0xFFF0
#define USB_ACCESSORY_PID 0x2D00

#define BLK_OUT_ENDPOINT 0x02
#define BLK_IN_ENDPOINT 0x81

#define INIT_HOOK_MSG_LEN 1024

static libusb_hotplug_callback_handle callback_handle;

static bool xfer_or_die(libusb_device_handle* hndl, int wIdx, char* str) {
    int res = libusb_control_transfer(hndl, 0x40, 52, 0, wIdx
                                      , str, strlen(str), 0);
    if (res < 0) {
        slog(LOG_DEBUG, "USB: xfer failed: %s", libusb_strerror(res));
        libusb_close(hndl);
        return false;
    }
    return true;
}

static bool init_accessory(struct context* ctx, libusb_device_handle* hndl) {
    unsigned char buf[2];
    int res = libusb_control_transfer(
        hndl, 0xC0 //bmRequestType
        , 51 //bRequest
        , 0, 0 //wValue, wIndex
        , buf //data
        , 2 //wLength
        , 0); //timeout
    while (res < 0) {
        slog(LOG_DEBUG, "USB: cfg xfer failed: %s", libusb_strerror(res));
        sleep(5);
        libusb_control_transfer(hndl, 0xC0, 51, 0, 0, buf, 2, 0);
    }
    if (0 == res) {
        slog(LOG_NOTICE, "USB: not an android host");
        return false;
    }
    slog(LOG_DEBUG, "USB Device version code: %d", buf[1] << 8 | buf[0]);
    
    if (! (xfer_or_die(hndl, 0, USB_MANUFACTURER)
           && xfer_or_die(hndl, 1, USB_MODEL)
           && xfer_or_die(hndl, 2, USB_DESCRIPTION)
           && xfer_or_die(hndl, 3, USB_VERSION)
           && xfer_or_die(hndl, 4, USB_URI)
           && xfer_or_die(hndl, 5, USB_SERIAL_NUM)))
    {
        return false;
    }
    libusb_control_transfer(hndl, 0x40, 53, 0, 0, NULL, 0, 0);
    return true;
}

static bool try_setup_accessory(struct context* ctx, libusb_device* dev) {
    libusb_device_handle* hndl;
    int usbres;
    bool res;
    usbres = libusb_open(dev, &hndl);
    if (usbres < 0) {
        slog(LOG_DEBUG, "USB: failed to open: %s", libusb_strerror(usbres));
        return false;
    }
    usbres = libusb_claim_interface(hndl, 0);
    if (usbres < 0) {
        slog(LOG_DEBUG, "USB: failed to claim interface: %s", libusb_strerror(usbres));
        libusb_close(hndl);
        return false;
    }
    res = init_accessory(ctx, hndl);
    libusb_release_interface(hndl, 0);
    libusb_close(hndl);
    return res;
}

static bool usb_write(struct context* ctx, char* data, int size) {
    int sent = 0;
    while (size > 0) {
        int response = libusb_bulk_transfer(ctx->w.uctx.hndl, BLK_OUT_ENDPOINT, data
                                            , size, &sent, USB_XFER_TIMEO_MSEC);
        if (response != 0) {
            slog(LOG_ERR, "USB transfer failed: %s", libusb_strerror(response));
            if (LIBUSB_ERROR_NO_DEVICE == response) {
                ctx->w.uctx.hndl = NULL;
                ctx->fin = 1;
            }
            return false;
        }
        size -= sent;
        data += sent;
    }
    return true;
}

static bool usb_img_writer(struct context* ctx, int x, int y, int width, int height
                           , char* data, int data_len) {
    char* header = fill_imagecmd_header(data, data_len, width, height, x, y);
    int size = data_len + IMAGECMD_HEAD_LEN;
    return usb_write(ctx, header, size);
}

static bool usb_pntr_writer(struct context* ctx, int x, int y
                           , int width, int height, char* pointer) {
    int size;
    char* data;
    if (width > 0) {
        data = pointer - 9;
        data[0] = 1;
        ((int*)(data + 1))[0] = htonl(width);
        ((int*)(data + 1))[1] = htonl(height);
        data -= 9;
        size = POINTERCMD_HEAD_LEN + width * height * 4;
    } else {
        data = pointer;
        data[9] = 0; // no pointer data
        size = 10;
    }
    data[0] = (char)Pointer;
    ((int*)(data + 1))[0] = htonl(x);
    ((int*)(data + 1))[1] = htonl(y);
    return usb_write(ctx, data, size);
}

static bool usb_init_conn(struct context* ctx, char* buf, int size) {
    int received = 0;
    int response = LIBUSB_ERROR_TIMEOUT;
    slog(LOG_DEBUG, "USB: init connection");
    if(NULL == ctx->w.uctx.hndl) {
        return false;
    }
    while (size > 0 && (LIBUSB_ERROR_TIMEOUT == response || 0 == response)) {
        int t = 0;
        response = libusb_bulk_transfer(ctx->w.uctx.hndl, BLK_IN_ENDPOINT, buf
                                        , size, &t
                                        , USB_XFER_TIMEO_MSEC);
        slog(LOG_DEBUG, "USB: init: receive %d/%d bytes", t, size);
        buf += t;
        size -= t;
    }
    if (response != 0) {
        slog(LOG_ERR, "USB: didn't get Init cmd: %s", libusb_strerror(response));
        if (LIBUSB_ERROR_NO_DEVICE == response) {
            ctx->w.uctx.hndl = NULL;
        }
        return false;
    }
    return true;
}

static bool usb_check_reinit(struct context* ctx, char* buf, int size) {
    int received = 0;
    int response;
    int t = 0;
    if (NULL == ctx->w.uctx.hndl) {
        return false;
    }
    slog(LOG_DEBUG, "USB: check if remote side trying to reinit");
    response = libusb_bulk_transfer(ctx->w.uctx.hndl, BLK_IN_ENDPOINT, buf
                             , size, &t
                             , USB_CHECK_TIMEO_MSEC);
    if (LIBUSB_ERROR_TIMEOUT == response) {
        return false;
    }

    slog(LOG_DEBUG, "USB: remote side reinit attempt detected");
    buf += t;
    size -= t;
    while (size > 0 && (LIBUSB_ERROR_TIMEOUT == response || 0 == response)) {
        response = libusb_bulk_transfer(ctx->w.uctx.hndl, BLK_IN_ENDPOINT, buf
                                        , size, &t
                                        , USB_XFER_TIMEO_MSEC);
        slog(LOG_DEBUG, "USB: reinit: receive %d/%d bytes", t, size);
        buf += t;
        size -= t;
    }
    if (response != 0) {
        slog(LOG_ERR, "USB: didn't get Init cmd: %s", libusb_strerror(response));
        if (LIBUSB_ERROR_NO_DEVICE == response) {
            ctx->w.uctx.hndl = NULL;
        }
        return false;
    }
    return true;
}


static libusb_device* get_by_bus_port(libusb_device*** devs, uint8_t bus, uint8_t port) {
    int cnt = libusb_get_device_list(NULL, devs);
    if (cnt < 0) {
        slog(LOG_ERR, "USB: listing devices failed: %s", libusb_strerror(cnt));
        return NULL;
    }
    cnt -= 1;
    while (cnt >= 0 && (libusb_get_bus_number((*devs)[cnt]) != bus
                        || libusb_get_port_number((*devs)[cnt]) != port)) {
        cnt -= 1;
    }
    if (cnt < 0) {
        return NULL;
    }
    return (*devs)[cnt];
} 

void init_usb(struct context* ctx, int bus, int port) {
    ctx->write_image = usb_img_writer;
    ctx->write_pointer = usb_pntr_writer;
    ctx->init_conn = usb_init_conn;
    ctx->check_reinit = usb_check_reinit;
    ctx->send_reply = usb_write;
    libusb_init(NULL);
    libusb_set_debug(NULL, LIBUSB_LOG_LEVEL_INFO);
    slog(LOG_NOTICE, "USB: trying %d.%d", bus, port);
    struct libusb_device** devs; //this is outside get_by_bus_port because it returns an item this list
    struct libusb_device* dev = get_by_bus_port(&devs, bus, port);
    if (NULL == dev) {
        slog(LOG_ERR, "USB: device @%d.%d does not exist", bus, port);
        return;
    }
    bool acc_res = try_setup_accessory(ctx, dev);
    libusb_free_device_list(devs, 1);
    if (!acc_res) {
        slog(LOG_DEBUG, "USB: Failed to setup accessory mode");
        return;
    }
    slog(LOG_NOTICE, "USB: Switched to accessory mode on device %d.%d", bus, port);
    sleep(5); // let the android disconnect and connect back
    dev = get_by_bus_port(&devs, bus, port);
    if (NULL == dev) {
        slog(LOG_ERR, "USB: failed to setup accessory mode on device @%d.%d", bus, port);
        return;
    }
    slog(LOG_NOTICE, "USB: accessory mode setup success");
    int res = libusb_open(dev, &ctx->w.uctx.hndl);
    libusb_free_device_list(devs, 1);
    if (res < 0) {
        slog(LOG_ERR, "USB: failed to open: %s", libusb_strerror(res));
        ctx->w.uctx.hndl = NULL;
    }
}

