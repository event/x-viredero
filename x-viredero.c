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
#include <time.h>
#include <syslog.h>

#include <sys/shm.h>
#include <arpa/inet.h>

#include <X11/Xlibint.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/Xrandr.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>

#include <webp/encode.h>

#include "x-viredero.h"

#define PROG "x-viredero"
#define DISP_NAME_MAXLEN 64
#define DATA_BUFFER_HEAD 32
#define INIT_CMD_LEN 4
#define MAX_INIT_BUF_SIZE 12 // maximum size required for init_reply cmd
#define MAX_VIREDERO_PROT_VERSION 1
#define CURSOR_MAX_SIZE 64
#define CURSOR_BUFFER_SIZE (4 * CURSOR_MAX_SIZE * CURSOR_MAX_SIZE + POINTERCMD_HEAD_LEN)
#define POINTER_CHECK_INTERVAL_MSEC 50
#define FPS_LOG_INTERVAL_MSEC 30000
#define FAILURES_EXIT_PUMP 100
#define USE_PNG 1

static char* image_buffer;

struct png_wr_ctx {
    char* out;
    int offset;
};

static int log_level = LOG_NOTICE;
void slog(int prio, char* format, ...) {
    if (prio > log_level) {
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

unsigned long now() {
    struct timespec tp;
    clock_gettime(CLOCK_MONOTONIC, &tp);
    return tp.tv_sec * 1000 + tp.tv_nsec / 1000000;
}

char* fill_imagecmd_header(char* data, int data_len, int w, int h, int x, int y) {
    char* cmd = data - IMAGECMD_HEAD_LEN;
    int* header = (int*)(cmd + 1);
    *cmd = (char)Image;
    header[0] = htonl(w);
    header[1] = htonl(h);
    header[2] = htonl(x);
    header[3] = htonl(y);
    header[4] = htonl(data_len);
    return cmd;
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

bool dummy_pointer_writer(struct context* ctx, int x, int y
                          , int width, int height, char* pointer) {
    return true;
}

static bool output_damage(struct context* ctx, int x, int y, int width, int height) {
//    slog(LOG_DEBUG, "outputing damage: %d %d %d %d\n", x, y, width, height);
    bool res;
    char* buf = image_buffer + DATA_BUFFER_HEAD;
    int len = ctx->get_image(ctx, buf, x, y, width, height);
    res = len > 0 && ctx->write_image(ctx, x, y, width, height, buf, len);
    return res;
}

static bool output_pointer_image(struct context* ctx) {
    XFixesCursorImage* cursor = XFixesGetCursorImage(ctx->display);
    char* data = image_buffer + POINTERCMD_HEAD_LEN; // leave some head space for cmd header
    cursor2rgba(cursor->pixels, data, cursor->width * cursor->height * 4);
    return ctx->write_pointer(ctx, cursor->x, cursor->y
                       , cursor->width, cursor->height, data);
}

static bool output_pointer_coords(struct context* ctx, int x, int y) {
    return ctx->write_pointer(ctx, x, y, 0, 0, image_buffer);
}

static bool setup_display(const char * display_name, struct context* ctx) {
    Display* display = XOpenDisplay(display_name);
    int t;
    if (!display) {
        slog(LOG_ERR, "unable to open display '%s'\n"
             , display_name);
        usage();
        return false;
    }

    Window root = DefaultRootWindow(display);

    if (!XDamageQueryExtension (display, &ctx->damage_evt_base, &t)) {
        slog(LOG_ERR, "backend does not have Xdamage extension\n");
        return false;
    }
    if (!XShmQueryExtension(display)) {
        slog(LOG_ERR, "backend does not have XShm extension\n");
        return false;
    }
    if (!XFixesQueryExtension(display, &ctx->cursor_evt_base, &t)) {
        slog(LOG_ERR, "backend does not have XFixes extension\n");
        return false;
    }
    XFixesSelectCursorInput(display, root,
                            XFixesDisplayCursorNotifyMask);
    XDamageCreate(display, root, XDamageReportRawRectangles);
    ctx->cursor_x = 0;
    ctx->cursor_y = 0;
    
    ctx->display = display;
    ctx->root = root;
    return true;
}

static int get_image_bmp(struct context* ctx, char* out, int x, int y, int width, int height) {
    XImage* ximage = ctx->p.bmp.shmimage;
    ximage->width = width;
    ximage->height = height;
    if (!XShmGetImage(ctx->display, ctx->root
                      , ximage, x, y, AllPlanes)) {
        slog(LOG_ERR, "unabled to get the image\n");
        return 0;
    }
    for (int j = 0; j < height; j += 1) {
        for (int i = 0; i < width; i += 1) {
            unsigned long pixel = XGetPixel(ximage, i, j);
            out[0] = pixel & 0xFF;
            out[1] = (pixel >> 16) & 0xFF;
            out[2] = (pixel >> 8) & 0xFF;
            out += 3;
        }
    }
    return width * height * 3;
}

static bool init_image_pump_bmp(struct context* ctx, int width, int height) {
    int scr = XDefaultScreen(ctx->display);
    XShmSegmentInfo* shminfo = &ctx->p.bmp.shminfo;
    XImage* shmimage = XShmCreateImage(
        ctx->display, DefaultVisual(ctx->display, scr), DefaultDepth(ctx->display, scr)
        , ZPixmap, NULL, shminfo, width, height);

    shminfo->shmid = shmget(
        IPC_PRIVATE
        , DATA_BUFFER_HEAD + shmimage->bytes_per_line * shmimage->height
        , IPC_CREAT | 0777);
    if (shminfo->shmid == -1) {
        slog(LOG_ERR, "Cannot get shared memory!");
        return false;
    }

    shminfo->shmaddr = shmat(shminfo->shmid, 0, 0);
    shminfo->readOnly = False;
    shmimage->data = shminfo->shmaddr + DATA_BUFFER_HEAD;
    if (!XShmAttach(ctx->display, shminfo)) {
        slog(LOG_ERR, "Failed to attach shared memory!");
        return false;
    }
    ctx->p.bmp.shmimage = shmimage;
    image_buffer = malloc(IMAGECMD_HEAD_LEN + width * height * 3);
    ctx->get_image = get_image_bmp;
    return true;
}

static cairo_status_t write_png(void* closure, const unsigned char* data, unsigned int length) {
    struct png_wr_ctx* wr_ctx = (struct png_wr_ctx*)closure;
    memcpy(wr_ctx->out + wr_ctx->offset, data, length);
    wr_ctx->offset += length;
    return CAIRO_STATUS_SUCCESS;
}

static int get_image_png(struct context* ctx, char* out, int x, int y, int width, int height) {
    cairo_surface_t* xsurface;
    cairo_surface_t* isurface;
    cairo_rectangle_int_t rect;
    struct png_wr_ctx wr_ctx;
    wr_ctx.out = out;
    wr_ctx.offset = 0;
    rect.x = x;
    rect.y = y;
    rect.width = width;
    rect.height = height;
    xsurface = cairo_xlib_surface_create(
        ctx->display, ctx->root, XDefaultVisual(ctx->display, XDefaultScreen(ctx->display))
        , x + width, y + height);
    isurface = cairo_surface_map_to_image(xsurface, &rect);
    cairo_surface_write_to_png_stream(isurface, write_png, &wr_ctx);
    cairo_surface_unmap_image(xsurface, isurface);
    cairo_surface_destroy(xsurface);
    return wr_ctx.offset;
}

static bool init_image_pump_png(struct context* ctx, int width, int height) {
    image_buffer = malloc(IMAGECMD_HEAD_LEN + width * height * 3);
    ctx->get_image = get_image_png;
    return true;
}

static int get_image_webp(struct context* ctx, char* out, int x, int y, int width, int height) {
    XImage* ximage = ctx->p.bmp.shmimage;
    ximage->width = width;
    ximage->height = height;
    if (!XShmGetImage(ctx->display, ctx->root
                      , ximage, x, y, AllPlanes)) {
        slog(LOG_ERR, "unabled to get the image\n");
        return 0;
    }
    WebPPicture* pic = &ctx->p.webp.picture;
    pic->argb = (uint32_t*)ximage->data;
    pic->argb_stride = width;
    WebPMemoryWriter w;
    w.mem = out;
    w.max_size = width * height * 3;
    w.size = 0;
    pic->custom_ptr = &w;
    WebPEncode(&ctx->p.webp.config, pic);
    return w.size;
}

static bool init_image_pump_webp(struct context* ctx, int width, int height) {
    init_image_pump_bmp(ctx, width, height);
    ctx->get_image = get_image_webp;
    WebPConfig config;
    if (!WebPConfigPreset(&ctx->p.webp.config, WEBP_PRESET_PHOTO, 100)
        || !WebPConfigLosslessPreset(&ctx->p.webp.config, 3))
    {
        return false;
    }
    WebPPicture* pic = &ctx->p.webp.picture;
    if (!WebPPictureInit(pic)) {
        return 0;
    }
    pic->width = width;
    pic->height = height;
    if (!WebPPictureAlloc(pic)) {
        return 0;
    }
    pic->writer = WebPMemoryWrite;

    return true;
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

static bool init_cmd_reply(struct context* ctx, char* buf) {
    if (buf[0] != 0) {
        send_error_reply(ctx, ErrorBadMessage);
        return false;
    }

    if (buf[1] > MAX_VIREDERO_PROT_VERSION) {
        send_error_reply(ctx, ErrorVersion);
        return false;
    }
    
    XWindowAttributes attrib;
    XGetWindowAttributes(ctx->display, ctx->root, &attrib);
    bool init_res;
    if ((buf[2] & SF_PNG) != 0) {
#ifdef USE_PNG
        init_res = init_image_pump_png(ctx, attrib.width, attrib.height);
#else
        init_res = init_image_pump_webp(ctx, attrib.width, attrib.height);
#endif
        buf[2] = SF_PNG; // android automatically detect png/webp/jpeg formats on decoding
    } else if ((buf[2] & SF_RGB) != 0) {
        init_res = init_image_pump_bmp(ctx, attrib.width, attrib.height);
        buf[2] = SF_RGB;
    } else {
        send_error_reply(ctx, ErrorScreenFormatNotSupported);
        return false;
    }

    if (! init_res) {
        send_error_reply(ctx, ErrorInitFailed);
        return false;
    }

    if ((buf[3] & PF_RGBA) == 0) {
        send_error_reply(ctx, ErrorPointerFormatNotSupported);
        return false;
    }
    buf[0] = InitReply;
    buf[1] = ResultSuccess;
    buf[2] = SF_PNG;
    buf[3] = PF_RGBA;
    ((int*)(buf + 4))[0] = htonl(attrib.width);
    ((int*)(buf + 4))[1] = htonl(attrib.height);
    return ctx->send_reply(ctx, buf, 12);
}

static bool handshake(struct context* ctx) {
    char buf[MAX_INIT_BUF_SIZE];

    if (! ctx->init_conn(ctx, buf, INIT_CMD_LEN)) {
        return false;
    }
    return init_cmd_reply(ctx, buf);
}

static void update_fail_cnt(bool res, int* fail) {
    if (res) {
        *fail = 0;
    } else {
        *fail += 1;
    }
}

static void pump(struct context* ctx) {
    unsigned long oldmillis = 0;
    int oldx = 0;
    int oldy = 0;
    int fail_cnt = 0;
    unsigned long fps_startmillis = now();
    int frame_cnt = 0;
    while (!ctx->fin && fail_cnt < FAILURES_EXIT_PUMP) {
        struct timespec tp;
        unsigned long millis = now();
        if (millis - oldmillis > POINTER_CHECK_INTERVAL_MSEC) {
            int junk, x, y;
            Window junkw;
            XQueryPointer(ctx->display, ctx->root, &junkw, &junkw
                          , &x, &y, &junk, &junk, &junk);
            if (x != oldx || y != oldy) {
                update_fail_cnt(output_pointer_coords(ctx, x, y), &fail_cnt);
                oldx = x;
                oldy = y;
            }
            oldmillis = millis;
        }
        while (XPending(ctx->display) > 0 && millis - oldmillis < POINTER_CHECK_INTERVAL_MSEC) {
            XEvent event;
            XNextEvent(ctx->display, &event);
            if (ctx->cursor_evt_base + XFixesCursorNotify == event.type) {
                update_fail_cnt(output_pointer_image(ctx), &fail_cnt);
            } else if (ctx->damage_evt_base + XDamageNotify == event.type) {
                XDamageNotifyEvent* de = (XDamageNotifyEvent*) &event;
                if (de->drawable == ctx->root) {
                    update_fail_cnt(output_damage(ctx, de->area.x, de->area.y
                                                  , de->area.width, de->area.height), &fail_cnt);
                    frame_cnt += 1;
                    if (millis - fps_startmillis > FPS_LOG_INTERVAL_MSEC) {
                        slog(LOG_INFO, "%d fps\n", frame_cnt / ((millis - fps_startmillis) / 1000));
                        fps_startmillis = millis;
                        frame_cnt = 0;
                    }
                }
            }
            millis = now();
        }
        if (ctx->check_reinit(ctx, image_buffer, INIT_CMD_LEN)) {
            slog(LOG_WARNING, "Remote side initiated reinit. Replying...\n");
            init_cmd_reply(ctx, image_buffer);
        }
    }
    ctx->fin = 0;
}

static int check_len_or_die(char* value, char* field_name) {
    int len = strlen(value);
    if (len <= DISP_NAME_MAXLEN) {
        return len;
    }
    fprintf(stderr, "%s %s is longer then %d."
            " We can't handle it. Good bye.\n"
            , field_name, optarg, DISP_NAME_MAXLEN);
    exit(1);
}

static struct context context;


int main(int argc, char* argv[]) {
    char* disp_name = ":0";
    char* path;
    int c;
    int debug = 0;
    int len;
    long int port;
    int i;
    int handshake_attempts = 2;

    openlog(PROG, LOG_PERROR | LOG_CONS | LOG_PID, LOG_DAEMON);
    while ((c = getopt (argc, argv, "hdu:D:l:p:")) != -1) {
        switch (c)
        {
        case 'd':
            debug = 1;
            break;
        case 'D':
            len = check_len_or_die(optarg, "Display name");
            disp_name = malloc(len + 1);
            strncpy(disp_name, optarg, len + 1);
            break;
        case 'l':
            port = strtol(optarg, NULL, 10);
            if (port < 1 || port > 65535) {
                fprintf(stderr, "Port %s is not in range."
                        " Will use default port %d\n", optarg, DEFAULT_PORT);
                port = DEFAULT_PORT;
            }
            init_socket(&context, (uint16_t)port);
            break;
        case 'p':
            len = check_len_or_die(optarg, "File name");
            path = malloc(len + 1);
            strncpy(path, optarg, len + 1);
            init_ppm(&context, path);
            break;
#if WITH_USB
        case 'u':
            check_len_or_die(optarg, "USB device");
            char* delim = strchr(optarg, '.');
            int port, bus;
            if (delim != NULL) {
                bus = strtol(optarg, NULL, 10);
                port = strtol(delim + 1, NULL, 10);
            } else {
                fprintf(stderr, "USB device have to be specified as <bus>.<port>. Exiting...\n");
                exit(1);
            }

            init_usb(&context, bus, port);
            break;
#endif /*WITH_USB*/
        default:
            usage();
            exit(1);
            break;
        }
    }
    
    if (debug) {
        log_level = LOG_DEBUG;
    } else {
        daemonize();
    }
    if (!setup_display(disp_name, &context)) {
        exit(1);
    }
    slog(LOG_NOTICE, "%s up and running", PROG);

    while (context.init_conn && (handshake_attempts > 0) && !handshake(&context)) {
        slog(LOG_ERR, "handshake failed. Retrying...");
        handshake_attempts -= 1;
    }
    if (0 == handshake_attempts) {
        slog(LOG_WARNING, "All handshake attempts failed. Exiting...");
        exit(0);
    }
    slog(LOG_INFO, "handshake success");
    output_pointer_image(&context);
    pump(&context);
}

