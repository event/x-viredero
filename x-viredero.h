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
#ifndef __X_VIREDERO_H__
#define __X_VIREDERO_H__

#include <X11/Xlibint.h>
#include <X11/extensions/XShm.h>
#if WITH_USB
#include <libusb-1.0/libusb.h>
#endif

enum CommandType {
    Init,
    InitReply,
    Image,
    Pointer,
    SceneChange,
    ReCenter,
};

enum CommandResultCode {
    ResultSuccess,
    ErrorBadMessage,
    ErrorVersion,
    ErrorScreenFormatNotSupported,
    ErrorPointerFormatNotSupported,
};

enum ScreenFormat { // bit masks
    SF_RGB = 0x1,
    SF_PNG = 0x2,
};

enum PointerFormat { //bit masks
    PF_RGBA = 0x1,
    PF_PNG = 0x2,
};

#define IMAGECMD_HEAD_LEN 17

#define DEFAULT_PORT 1242

struct __attribute__ ((__packed__)) bm_head {
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
};

struct __attribute__ ((__packed__)) bm_info_head {
    uint32_t biSize;
    uint32_t biWidth;
    uint32_t biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    uint32_t biXPelsPerMeter;
    uint32_t biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
};

struct bmp_context {
    int num;
    char* path;
    char* fname;
    struct bm_head head;
    struct bm_info_head ihead; 
};

struct sock_context {
    int listen_sock;
    int sock;
};

struct usb_context {
    libusb_device_handle* hndl;
};

struct context {
    Display* display;
    Window root;
    XShmSegmentInfo shminfo;
    XImage* shmimage;
    int damage;
    int fin;
    char* cursor_buffer;
    long cursor_serial;
    short cursor_x;
    short cursor_y;
    union writer_cfg{
        struct sock_context sctx;
        struct bmp_context bctx;
        struct usb_context uctx;
    } w;
    int (*receive_init)(struct context*, char*, int);
    int (*send_reply)(struct context*, char*, int);
    int (*image_write)(struct context*, int, int, int, int, char*, int);
    int (*pointer_write)(struct context*, int, int, char*, int, int);
    int (*scene_change)(struct context*, int, int, int, int, char*, int);
    int (*recenter)(struct context*, int, int, int, int, char*, int);
};


void slog(int, char*, ...);
char* fill_imagecmd_header(char*, int, int, int, int);
#if WITH_USB
void init_usb(struct context*, uint16_t, uint16_t);
#endif
int dummy_pointer_writer(struct context*, int, int, char*, int, int);
void init_bmp(struct context*, char*);
void init_socket(struct context*, uint16_t);

#endif //__X_VIREDERO_H__
