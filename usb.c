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
#include <stdarg.h>
#include <unistd.h>
#include <syslog.h>

#include <libusb-1.0/libusb.h>
#include "x-viredero.h"


#define USB_MANUFACTURER "Leonid Movshovich"
#define USB_MODEL "x-viredero"
#define USB_DESCRIPTION "viredero is a virtual reality desktop view"
#define USB_VERSION "2.1"
#define USB_URI "http://play.google.com/"
#define USB_SERIAL_NUM "130"
#define USB_XFER_TIMEO_MSEC 1000
#define USB_ACCESSORY_VID 0x18D1
#define USB_ACCESSORY_PID_MASK 0xFFF0
#define USB_ACCESSORY_PID 0x2D00

#define BLK_OUT_ENDPOINT 0x02
#define BLK_IN_ENDPOINT 0x81

static int xfer_or_die(libusb_device_handle* hndl, int wIdx, char* str) {
    int res = libusb_control_transfer(hndl, 0x40, 52, 0, wIdx
                                      , str, strlen(str), 0);
    if (res < 0) {
        slog(LOG_DEBUG, "USB: xfer failed: %s", libusb_strerror(res));
        libusb_close(hndl);
        return 0;
    }
    return 1;
}

static int try_setup_accessory(libusb_device* dev) {
    libusb_device_handle* hndl;
    unsigned char buf[2];
    int res;
    res = libusb_open(dev, &hndl);
    if (res < 0) {
        slog(LOG_DEBUG, "USB: failed to open: %s", libusb_strerror(res));
        return 0;
    }
    res = libusb_claim_interface(hndl, 0);
    if (res < 0) {
        slog(LOG_DEBUG, "USB: failed to claim interface: %s", libusb_strerror(res));
        libusb_close(hndl);
        return 0;
    }

    res = libusb_control_transfer(
        hndl, 0xC0 //bmRequestType
        , 51 //bRequest
        , 0, 0 //wValue, wIndex
        , buf //data
        , 2 //wLength
        , 0); //timeout
    if (res < 0) {
        slog(LOG_DEBUG, "USB: xfer failed: %s", libusb_strerror(res));
        libusb_close(hndl);
        return 0;
    }
    slog(LOG_DEBUG, "USB Device version code: %d", buf[1] << 8 | buf[0]);
    
    if (! (xfer_or_die(hndl, 0, USB_MANUFACTURER)
           && xfer_or_die(hndl, 1, USB_MODEL)
           && xfer_or_die(hndl, 2, USB_DESCRIPTION)
           && xfer_or_die(hndl, 3, USB_VERSION)
           && xfer_or_die(hndl, 4, USB_URI)
           && xfer_or_die(hndl, 5, USB_SERIAL_NUM)))
    {
        return 0;
    }
    libusb_control_transfer(hndl, 0x40, 53, 0, 0, NULL, 0, 0);
    libusb_release_interface(hndl, 0);
    return 1;
}

static int usb_write(struct context* ctx, char* data, int size) {
    int sent = 0;
    while (size > 0) { 
        int response = libusb_bulk_transfer(ctx->w.uctx.hndl, BLK_OUT_ENDPOINT, data
                                            , size, &sent, USB_XFER_TIMEO_MSEC);
        if (response != 0) {
            slog(LOG_ERR, "USB transfer failed: %s", libusb_strerror(response));
            return 0;
        }
        size -= sent;
        data += sent;
    }
    return 1;
}

static int i = 0;
static int usb_img_writer(struct context* ctx, int x, int y, int width, int height
                      , char* data) {
    char* header = fill_imagecmd_header(data, width, height, x, y);
    int size = (width * height * 3) + IMAGECMD_HEAD_LEN;
//    slog(LOG_DEBUG, "%d %d %d %d %d %d", i, width, height, x, y, size - IMAGECMD_HEAD_LEN);
    i += 1;
    return usb_write(ctx, header, size);
}

static int usb_pntr_writer(struct context* ctx, int x, int y
                           , int width, int height, char* pointer) {
    int size = 10;
    char buf[10];
    char* data;
    if (pointer != NULL) {
        data = pointer - 9;
        data[0] = 1;
        ((int*)(data + 1))[0] = htonl(width);
        ((int*)(data + 1))[1] = htonl(height);
        data -= 9;
        size = 18 + width * height * 4;
    } else {
        data = buf;
        data[10] = 0; // no pointer data
    }
    data[0] = (char)Pointer;
    ((int*)(data + 1))[0] = htonl(x);
    ((int*)(data + 1))[1] = htonl(y);
    return usb_write(ctx, data, size);
}

static int usb_receive_init(struct context* ctx, char* buf, int size) {
    char* p = buf; 
    int received = 0;
    int response = LIBUSB_ERROR_TIMEOUT;
    while (size > 0 && (LIBUSB_ERROR_TIMEOUT == response || 0 == response)) {
        int t = 0;
        response = libusb_bulk_transfer(ctx->w.uctx.hndl, BLK_IN_ENDPOINT, p
                                        , size, &t
                                        , USB_XFER_TIMEO_MSEC * 30);
        p += t;
        size -= t;
    }
    if (response != 0) {
        slog(LOG_ERR, "USB: didn't get Init cmd: %s", libusb_strerror(response));
        return 0;
    }
    return 1;
}


void init_usb(struct context* ctx, uint16_t vid, uint16_t pid) {
    struct usb_context* uctx = &ctx->w.uctx;
    libusb_device** devs;
    libusb_device* tgt = NULL;
    int cnt;
    uint8_t port, bus;
    int setup_required = 1;
    libusb_init(NULL);
    libusb_set_debug(NULL, 3);
    cnt = libusb_get_device_list(NULL, &devs);
    if (cnt < 0) {
        slog(LOG_ERR, "USB: listing devices failed: %s", libusb_strerror(cnt));
        exit(1);
    }
    while (cnt > 0 && NULL == tgt) {
        struct libusb_device_descriptor desc;
        cnt -= 1;
        if (libusb_get_device_descriptor(devs[cnt], &desc) != 0) {
            slog(LOG_WARNING, "USB: failed to get device descriptor");
        } else if (desc.idVendor == USB_ACCESSORY_VID
                   && (desc.idProduct & USB_ACCESSORY_PID_MASK) == USB_ACCESSORY_PID) {
            tgt = devs[cnt];
            setup_required = 0;
        } else if (0 == vid || (desc.idVendor == vid && desc.idProduct == pid)) {
            if (try_setup_accessory(devs[cnt])) {
                tgt = devs[cnt];
            }
        }
    }
    if (NULL == tgt) {
        slog(LOG_ERR, "USB: failed to setup accessory mode on any device");
        exit(1);
    }
    if (setup_required) {
        port = libusb_get_port_number(tgt);
        bus = libusb_get_bus_number(tgt);
        slog(LOG_NOTICE, "USB: Switched to accessory mode on device %d.%d", port, bus);
        libusb_free_device_list(devs, 1);
        sleep(5);
        cnt = libusb_get_device_list(NULL, &devs);
        if (cnt < 0) {
            slog(LOG_ERR, "USB: listing devices failed: %s", libusb_strerror(cnt));
            exit(1);
        }
        cnt -= 1;
        while (cnt >= 0 && (libusb_get_bus_number(devs[cnt]) != bus
                            || libusb_get_port_number(devs[cnt]) != port)) {
            cnt -= 1;
        }
        if (cnt < 0) {
            slog(LOG_ERR, "USB: failed to setup accessory mode on device @%d.%d", bus, port);
            exit(1);
        }
    }
    slog(LOG_NOTICE, "USB: accessory mode setup success");
    int res = libusb_open(devs[cnt], &uctx->hndl);
    libusb_free_device_list(devs, 1);
    
    if (res < 0) {
        slog(LOG_ERR, "USB: failed to open: %s", libusb_strerror(res));
        return;
    }
    res = libusb_claim_interface(uctx->hndl, 0);
    if (res < 0) {
        slog(LOG_ERR, "USB: failed to claim interface: %s", libusb_strerror(res));
        libusb_close(uctx->hndl);
        return;
    }
    ctx->image_write = usb_img_writer;
    ctx->pointer_write = usb_pntr_writer;
    ctx->receive_init = usb_receive_init;
    ctx->send_reply = usb_write;
}

