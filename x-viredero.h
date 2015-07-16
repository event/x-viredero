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
#define POINTERCMD_HEAD_LEN 18

#define DEFAULT_PORT 1242

struct ppm_context {
    int num;
    char* path;
    char* fname;
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
    int damage_evt_base;
    int cursor_evt_base;
    int fin;
    char* cursor_buffer;
    short cursor_x;
    short cursor_y;
    union writer_cfg{
        struct sock_context sctx;
        struct ppm_context pctx;
        struct usb_context uctx;
    } w;
    int (*receive_init)(struct context*, char*, int);
    int (*send_reply)(struct context*, char*, int);
    int (*image_write)(struct context*, int, int, int, int, char*);
    int (*pointer_write)(struct context*, int, int, int, int, char*);
    int (*scene_change)(struct context*);
    int (*recenter)(struct context*, int, int);
};


void slog(int, char*, ...);
char* fill_imagecmd_header(char*, int, int, int, int);
#if WITH_USB
void init_usb(struct context*, uint16_t, uint16_t);
#endif
int dummy_pointer_writer(struct context*, int, int, int, int, char*);
void init_ppm(struct context*, char*);
void init_socket(struct context*, uint16_t);

#endif //__X_VIREDERO_H__
