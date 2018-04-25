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

#include <stdbool.h>
#include <X11/Xlibint.h>
#include <X11/extensions/XShm.h>
#include <cairo/cairo.h>
#include <webp/encode.h>
#if WITH_USB
#include <libusb-1.0/libusb.h>
#endif

#define IMAGECMD_HEAD_LEN 21
#define POINTERCMD_HEAD_LEN 18
#define DEFAULT_PORT 1242

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
    ErrorInitFailed,
};

enum ScreenFormat { // bit masks
    SF_RGB = 0x1,
    SF_PNG = 0x2,
};

enum PointerFormat { //bit masks
    PF_RGBA = 0x1,
    PF_PNG = 0x2,
};

struct ppm_context {
    int num;
    char* path;
    char* fname;
};

struct sock_context {
    int listen_sock;
    int sock;
};

#if WITH_USB
struct usb_context {
    libusb_device_handle* hndl;
};
#endif

struct bmp_image_pump_context {
    XShmSegmentInfo shminfo;
    XImage* shmimage;
};

struct webp_image_pump_context {
    struct bmp_image_pump_context bmp;
    WebPConfig config;
    WebPPicture picture;
};

struct context {
    Display* display;
    Window root;
    int damage_evt_base;
    int cursor_evt_base;
    int fin;
    short cursor_x;
    short cursor_y;
    union writer_cfg {
        struct sock_context sctx;
        struct ppm_context pctx;
#if WITH_USB
        struct usb_context uctx;
#endif
    } w;
    union pump_cfg {
        struct bmp_image_pump_context bmp;
        struct webp_image_pump_context webp;
    } p;
    bool (*init_conn)(struct context*, char*, int);
    bool (*check_reinit)(struct context*, char*, int);
    bool (*send_reply)(struct context*, char*, int);
    bool (*write_image)(struct context*, int, int, int, int, char*, int);
    bool (*write_pointer)(struct context*, int, int, int, int, char*);
    bool (*change_scene)(struct context*);
    bool (*recenter)(struct context*, int, int);
    int (*get_image)(struct context*, char*, int, int, int, int);
};


void slog(int, char*, ...);
char* fill_imagecmd_header(char*, int, int, int, int, int);
unsigned long now();
#if WITH_USB
void init_usb(struct context*, int bus, int port);
#endif
bool dummy_pointer_writer(struct context*, int, int, int, int, char*);
void init_ppm(struct context*, char*);
void init_socket(struct context*, uint16_t);

#endif //__X_VIREDERO_H__
