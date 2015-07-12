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

#include <sys/shm.h>

#include <X11/Xlibint.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xfixes.h>

#include "x-viredero.h"

#define PROG "x-viredero"
#define SLEEP_TIME_MSEC 50
#define DISP_NAME_MAXLEN 64
#define DATA_BUFFER_HEAD 32
#define INIT_CMD_LEN 4
#define MAX_VIREDERO_PROT_VERSION 1
#define CURSOR_BUFFER_SIZE (4 * 32 * 32 + 18)

static int max_log_level = 8;

void slog(int prio, char* format, ...) {
    if (prio > max_log_level) {
        return;
    }
    va_list ap;
    va_start(ap, format);
    vsyslog(prio, format, ap);
    va_end(ap);
}

static void usage() {
    printf("USAGE: %s <opts>\n", PROG);
}

char* fill_imagecmd_header(char* data, int w, int h, int x, int y) {
    char* header = data - IMAGECMD_HEAD_LEN;
    header[0] = (char)Image;
    ((int*)(header + 1))[0] = htonl(w);
    ((int*)(header + 1))[1] = htonl(h);
    ((int*)(header + 1))[2] = htonl(x);
    ((int*)(header + 1))[3] = htonl(y);
    return header;
}

static void zpixmap2rgb(char* data, unsigned long len) {
    //pixmap is in agbr format
    uint32_t* src = (uint32_t*)data;
    uint32_t* end = src + len;
    while (src < end) {
        char green = (char)((src[0] >> 16) & 0xFF);
        char blue = (char)((src[0] >> 8) & 0xFF);
        char red = (char)(src[0] & 0xFF);
        data[0] = red;
        data[1] = green;
        data[2] = blue;
        data += 3;
        src += 1;
    }
}

static void cursor2rgba(unsigned long* cur_data, char* rgba_data, unsigned long len) {
// cursor is in argb format
    int cur_idx = len/4;
    while (len > 0) {
        len -= 4;
        cur_idx -= 1;
        rgba_data[len + 3] = (cur_data[cur_idx] >> 24) & 0xFF;
        rgba_data[len + 0] = (cur_data[cur_idx] >> 16) & 0xFF;
        rgba_data[len + 1] = (cur_data[cur_idx] >>  8) & 0xFF;
        rgba_data[len + 2] = (cur_data[cur_idx]) & 0xFF;
    }
}

int dummy_pointer_writer(struct context* ctx, int x, int y
                         , int width, int height, char* pointer) {
    return 1;
}

static int output_damage(struct context* ctx, int x, int y, int width, int height) {
//    slog(LOG_DEBUG, "outputing damage: %d %d %d %d\n", x, y, width, height);
    ctx->shmimage->width = width;
    ctx->shmimage->height = height;
    if (!XShmGetImage(ctx->display, ctx->root
                      , ctx->shmimage, x, y, AllPlanes)) {
        slog(LOG_ERR, "unabled to get the image\n");
        return 0;
    }
    zpixmap2rgb(ctx->shmimage->data, width * height);
    ctx->image_write(ctx, x, y, width, height, ctx->shmimage->data);
    return 1;
}

static int output_pointer(struct context* ctx) {
    XFixesCursorImage* cursor = XFixesGetCursorImage(ctx->display);
    if (cursor->cursor_serial != ctx->cursor_serial) {
        char* data = ctx->cursor_buffer + 18;
        cursor2rgba(cursor->pixels, data, cursor->width * cursor->height * 4);
        ctx->pointer_write(ctx, cursor->x, cursor->y
                           , cursor->width, cursor->height, data);
        ctx->cursor_serial = cursor->cursor_serial;
    } else if (cursor->x != ctx->cursor_x || cursor->y != ctx->cursor_y) {
        ctx->pointer_write(ctx, cursor->x, cursor->y, 0, 0, NULL);
    }                    
//    slog(LOG_DEBUG, "pointer is @%dx%d", cursor->x, cursor->y);
    return 1;
}

static int setup_display(const char * display_name, struct context* ctx) {
    Display* display = XOpenDisplay(display_name);
    int t;
    if (!display) {
        slog(LOG_ERR, "unable to open display '%s'\n"
             , display_name);
        usage();
        return 0;
    }

    Window root = DefaultRootWindow(display);

    int damage_error;
    if (!XDamageQueryExtension (display, &ctx->damage, &damage_error)) {
        slog(LOG_ERR, "backend does not have Xdamage extension\n");
        return 0;
    }
    if (!XShmQueryExtension(display)) {
        slog(LOG_ERR, "backend does not have XShm extension\n");
        return 0;
    }
    if (!XFixesQueryExtension(display, &t, &t)) {
        slog(LOG_ERR, "backend does not have XFixes extension\n");
        return 0;
    }
    XDamageCreate(display, root, XDamageReportRawRectangles);
    XWindowAttributes attrib;
    XGetWindowAttributes(display, root, &attrib);
    if (0 == attrib.width || 0 == attrib.height)
    {
        slog(LOG_ERR, "bad root with size %dx%d\n"
             , attrib.width, attrib.height);
        return 0;
    }
    int scr = XDefaultScreen(display);
    ctx->shmimage = XShmCreateImage(
        display, DefaultVisual(display, scr), DefaultDepth(display, scr)
        , ZPixmap, NULL, &ctx->shminfo, attrib.width, attrib.height);

    ctx->shminfo.shmid = shmget(
        IPC_PRIVATE
        , DATA_BUFFER_HEAD + ctx->shmimage->bytes_per_line * ctx->shmimage->height
        , IPC_CREAT | 0777);
    if (ctx->shminfo.shmid == -1) {
        slog(LOG_ERR, "Cannot get shared memory!");
        return 0; 
    }
 
    ctx->shminfo.shmaddr = shmat(ctx->shminfo.shmid, 0, 0);
    ctx->shminfo.readOnly = False;
    ctx->shmimage->data = ctx->shminfo.shmaddr + DATA_BUFFER_HEAD;
    if (!XShmAttach(display, &ctx->shminfo)) {
        slog(LOG_ERR, "Failed to attach shared memory!");
        return 0;
    }

    ctx->cursor_buffer = (char*)malloc(CURSOR_BUFFER_SIZE);
    ctx->cursor_serial = -1;
    ctx->cursor_x = 0;
    ctx->cursor_y = 0;
    
    ctx->display = display;
    ctx->root = root;
    return 1;
}

static void daemonize() {
    if (daemon(0, 0)) {
        slog(LOG_ERR, "Failed to daemonize: %m");
        exit(1);
    }
}

static void send_error_reply(struct context* ctx, enum CommandResultCode error) {
    char buf[2];
    buf[0] = InitReply;
    buf[1] = error;
    ctx->send_reply(ctx, buf, 2);
}

static int handshake(struct context* ctx) {
    char buf[INIT_CMD_LEN];
    char version;
    
    if (! ctx->receive_init(ctx, buf, INIT_CMD_LEN)) {
        return 0;
    }

    if (buf[0] != 0) {
        send_error_reply(ctx, ErrorBadMessage);
        return 0;
    }

    if (version > MAX_VIREDERO_PROT_VERSION) {
        send_error_reply(ctx, ErrorVersion);
        return 0;
    }
    
    if ((buf[1] & SF_RGB) == 0) {
        send_error_reply(ctx, ErrorScreenFormatNotSupported);
        return 0;
    }

    if ((buf[2] & PF_RGBA) == 0) {
        send_error_reply(ctx, ErrorPointerFormatNotSupported);
        return 0;
    }
    buf[0] = InitReply;
    buf[1] = ResultSuccess;
    buf[2] = SF_RGB;
    buf[3] = PF_RGBA;
    ctx->send_reply(ctx, buf, 4);
}

static struct context context;
int main(int argc, char* argv[]) {
    char* disp_name = ":0";
    char* path;
    uint16_t port = DEFAULT_PORT;
    XEvent event;
    int c;
    int debug = 0;
    int ssock;
    int len;
    long int _port;
    int i;
    openlog(PROG, LOG_PERROR | LOG_CONS | LOG_PID, LOG_DAEMON);
    while ((c = getopt (argc, argv, "hdD:l:p:u::")) != -1) {
        switch (c)
        {
        case 'd':
            debug = 1;
            break;
        case 'D':
            len = strlen(optarg);
            if (len > DISP_NAME_MAXLEN) {
                fprintf(stderr, "Display name %s is longer then %d"
                        ". We can't handle it. Good bye.\n"
                        , optarg, DISP_NAME_MAXLEN);
                exit(1);
            }
            disp_name = malloc(len + 1);
            strncpy(disp_name, optarg, len);
            break;
        case 'l':
            _port = strtol(optarg, NULL, 10);
            if (_port < 1 || port > 65535) {
                fprintf(stderr, "Port %s is not in range."
                        " Will use default port %d\n", optarg, DEFAULT_PORT);
                _port = DEFAULT_PORT;
            }
            init_socket(&context, (uint16_t)_port);
            break;
        case 'p':
            len = strlen(optarg);
            if (len > DISP_NAME_MAXLEN) {
                fprintf(stderr, "Display name %s is longer then %d"
                        ". We can't handle it. Good bye.\n"
                        , optarg, DISP_NAME_MAXLEN);
                exit(1);
            }
            path = malloc(len + 1);
            strncpy(path, optarg, len);
            init_ppm(&context, path);
            break;
#if WITH_USB
        case 'u':
            if (optarg != NULL) {
                char* ppid = strchr(optarg, ':');
                if (NULL == ppid) {
                    usage();
                    exit(1);
                }
                ppid += + 1;
                uint16_t vid, pid;
                vid = strtol(optarg, NULL, 16);
                pid = strtol(ppid, NULL, 16);
                init_usb(&context, vid, pid);
            } else {
                init_usb(&context, 0, 0);
            }
            break;
#endif /*WITH_USB*/
        default:
            usage();
            exit(1);
            break;
        }
    }
    
    if (!debug) {
        daemonize();
    }
    slog(LOG_DEBUG, "%s up and running", PROG);
    if (context.receive_init) {
        if (!handshake(&context)) {
            slog(LOG_ERR, "handshake failed. Aborting...");
            exit(1);
        }
    }
    slog(LOG_NOTICE, "handshake success");
    setup_display(disp_name, &context);
    slog(LOG_NOTICE, "display setup success");
    while (! context.fin) {
        while (XPending(context.display) > 0) {
            XNextEvent(context.display, &event);
            if (context.damage + XDamageNotify == event.type) {
                XDamageNotifyEvent *de = (XDamageNotifyEvent *) &event;
                if (de->drawable == context.root) {
                    output_damage(
                        &context, de->area.x, de->area.y
                        , de->area.width, de->area.height);
                }
                output_pointer(&context);
            }
        }
        usleep(SLEEP_TIME_MSEC);
    }
}
  
