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

#include <netinet/in.h>
#include <sys/shm.h>

#include <X11/Xlibint.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/XShm.h>

#include "x-viredero.h"

#define PROG "x-viredero"
#define SLEEP_TIME_MSEC 50
#define DISP_NAME_MAXLEN 64
#define DATA_BUFFER_HEAD 32
#define BLK_OUT_ENDPOINT 2

#if WITH_USB
#include <libusb-1.0/libusb.h>
#define USB_MANUFACTURER "Leonid Movshovich"
#define USB_MODEL "x-viredero"
#define USB_DESCRIPTION "viredero is a virtual reality desktop view"
#define USB_VERSION "2.1"
#define USB_URI "http://play.google.com/"
#define USB_SERIAL_NUM "12344321"
#define USB_XFER_TIMEO_MSEC 1000
#endif

#define BMP_FNAME_BUF_SIZE 128

static int max_log_level = 8;

static void slog(int prio, char* format, ...) {
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

static char* fill_imagecmd_header(char* data, int w, int h, int x, int y) {
    char* header = data - IMAGECMD_HEAD_LEN;
    header[0] = IMAGECMD;
    ((int*)(header + 1))[0] = htonl(w);
    ((int*)(header + 1))[1] = htonl(h);
    ((int*)(header + 1))[2] = htonl(x);
    ((int*)(header + 1))[3] = htonl(y);
    return header;
}

static void zpixmap2rgb(char* data, unsigned long len
                        , char roff, char goff, char boff) {
    uint32_t* src = (uint32_t*)data;
    uint32_t* end = src + len;
    while (src < end) {
        char red = (src[0] >> roff) & 0xFF;
        char green = (src[0] >> goff) & 0xFF;
        char blue = (src[0] >> boff) & 0xFF;
        data[0] = blue;
        data[1] = red;
        data[2] = green;
        data += 3;
        src += 1;
    }
}

static int sock_writer(struct context* ctx, int x, int y, int width, int height
                       , char* data, int size) {
    int fd = ctx->w.sctx.sock;
    if (0 == fd) {
        fd = accept(ctx->w.sctx.listen_sock, NULL, NULL);
        if (fd < 0) {
            slog(LOG_ERR, "Failed to accept connection: %m");
            exit(1);
        }
        ctx->w.sctx.sock = fd;
    }
    char* header = fill_imagecmd_header(data, width, height, x, y);
    size += 17;
    while (size > 0) {
        int sent = send(fd, header, size, 0);
        if (sent <= 0) {
            slog(LOG_WARNING, "send failed: %m");
            close(fd);
            fd = 0;
            return 0;
        }
        size -= sent;
        header += sent;
    }
    return 1;
}

static void swap_lines(char* line0, char* line1, int size) {
    int i;
    for (i = 0; i < size; i += 1) {
        line0[i] ^= line1[i];
        line1[i] ^= line0[i];
        line0[i] ^= line1[i];
    }
}

static int bmp_writer(struct context* ctx, int x, int y, int width, int height
                      , char* data, int size) {
    struct bmp_context* bctx = &ctx->w.bctx;
    FILE *f;
   
    bctx->head.bfSize = sizeof(struct bm_head) + sizeof(struct bm_info_head)
        + size;
    bctx->ihead.biWidth = width;
    bctx->ihead.biHeight = height;
    bctx->ihead.biSizeImage = size;

    snprintf(bctx->fname, BMP_FNAME_BUF_SIZE, bctx->path, bctx->num);
    slog(LOG_DEBUG, "save damage to %s", bctx->fname);
    f = fopen(bctx->fname, "wb");
    if(f == NULL)
        return;
    fwrite(&bctx->head, sizeof(struct bm_head), 1, f);
    fwrite(&bctx->ihead, sizeof(struct bm_info_head), 1, f);
    int i;
    int byte_w = 3 * width;
    for (i = 0; i < height/2; i += 1) {
        swap_lines(&data[byte_w * i], &data[byte_w * (height - i - 1)], 3 * width);
    }
    fwrite(data, size, 1, f);
    fclose(f);
    bctx->num += 1;
}

int output_damage(struct context* ctx, int x, int y, int width, int height) {
    slog(LOG_DEBUG, "outputing damage: %d %d %d %d\n", x, y, width, height);
    ctx->shmimage->width = width;
    ctx->shmimage->height = height;
    if (!XShmGetImage(ctx->display, ctx->root
                      , ctx->shmimage, x, y, AllPlanes)) {
        slog(LOG_ERR, "unabled to get the image\n");
        return 0;
    }
    zpixmap2rgb(ctx->shmimage->data, width * height, 16, 8, 0);
    ctx->write(ctx, x, y, width, height, ctx->shmimage->data
               , width * height * 3);
    return 1;
}

#if WITH_USB

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
        slog(LOG_DEBUG, "USB: failed to open : %s", libusb_strerror(res));
        return 0;
    }
    res = libusb_claim_interface(hndl, 0);
    if (res < 0) {
        slog(LOG_DEBUG, "USB: failed to claim interface : %s", libusb_strerror(res));
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

static void init_usb(struct usb_context* uctx, uint16_t vid, uint16_t pid) {
    libusb_device** devs;
    libusb_device* tgt = NULL;
    int cnt;
    uint8_t port, bus;
    libusb_init(NULL);
    libusb_set_debug(NULL, 3);
    cnt = libusb_get_device_list(NULL, &devs);
    if (cnt < 0) {
        slog(LOG_ERR, "USB: listing devices failed: %s", libusb_strerror(cnt));
        exit(1);
    }
    while (cnt > 0 && NULL == tgt) {
        cnt -= 1;
        if (try_setup_accessory(devs[cnt])) {
            tgt = devs[cnt];
        }            
    }
    if (NULL == tgt) {
        slog(LOG_ERR, "USB: failed to setup accessory mode on any device");
        exit(1);
    }
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
    slog(LOG_NOTICE, "USB: accessory mode setup success");
    int res = libusb_open(devs[cnt], &uctx->hndl);
    libusb_free_device_list(devs, 1);
    
    if (res < 0) {
        slog(LOG_ERR, "USB: failed to open : %s", libusb_strerror(res));
        return;
    }
    res = libusb_claim_interface(uctx->hndl, 0);
    if (res < 0) {
        slog(LOG_ERR, "USB: failed to claim interface : %s", libusb_strerror(res));
        libusb_close(uctx->hndl);
        return;
    }
}

static int usb_writer(struct context* ctx, int x, int y, int width, int height
                      , char* data, int size) {
    int sent;
    char* header = fill_imagecmd_header(data, width, height, x, y);
    size += 17;
    while (size > 0) { 
        int response = libusb_bulk_transfer(ctx->w.uctx.hndl, BLK_OUT_ENDPOINT, header
                                            , size, &sent, USB_XFER_TIMEO_MSEC);
        if (response != 0) {
            slog(LOG_ERR, "USB transfer failed: %s", libusb_strerror(response));
            return 0;
        }
        size -= sent;
        header += sent;
    }
    return 1;
}
#endif

static void init_bmp_folder(struct bmp_context* bctx, char* path) {
    bctx->num = 0;
    bctx->path = path;
    bctx->fname = malloc(BMP_FNAME_BUF_SIZE);
    bctx->head.bfType = 0x4D42;
    bctx->head.bfOffBits = sizeof(struct bm_head) + sizeof(struct bm_info_head);
    bctx->head.bfReserved1 = 0;
    bctx->head.bfReserved2 = 0;
    
    bctx->ihead.biSize = sizeof(struct bm_info_head);
    bctx->ihead.biPlanes = 1;
    bctx->ihead.biBitCount = 24;
    bctx->ihead.biCompression = 0;
    bctx->ihead.biXPelsPerMeter = 0;
    bctx->ihead.biYPelsPerMeter = 0;
    bctx->ihead.biClrUsed = 0;
    bctx->ihead.biClrImportant = 0;
}

static void init_socket(struct sock_context* sctx, uint16_t port) {
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        slog(LOG_ERR, "Socket creation failed: %m");
        exit(1);
    }
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0) {
        slog(LOG_ERR, "Socket optionn failed: %m");
        exit(1);
    }        
    if (bind(sock, (struct sockaddr*)&addr, sizeof(struct sockaddr_in)) < 0) {
        slog(LOG_ERR, "Socket bind failed: %m");
        exit(1);
    }
    if (listen(sock, 8) < 0) {
        slog(LOG_ERR, "Socket listen failed: %m");
        exit(1);
    }
    sctx->listen_sock = sock;
    sctx->sock = 0;
}

static int setup_display(const char * display_name, struct context* ctx) {
    Display * display = XOpenDisplay(display_name);
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
 
    ctx->shminfo.shmaddr = shmat(ctx->shminfo.shmid, 0, 0) + DATA_BUFFER_HEAD;
    ctx->shminfo.readOnly = False;
    ctx->shmimage->data = ctx->shminfo.shmaddr;
    if (!XShmAttach(display, &ctx->shminfo)) {
        slog(LOG_ERR, "Failed to attach shared memory!");
        return 0;
    }
 
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

static struct context context;

int main(int argc, char* argv[]) {
    char* disp_name;
    char* path;
    uint16_t port = DEFAULT_PORT;
    XEvent event;
    int c;
    int debug = 0;
    int ssock;
    int len;
    long int _port;
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
            init_socket(&context.w.sctx, (uint16_t)_port);
            context.write = sock_writer;
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
            init_bmp_folder(&context.w.bctx, path);
            context.write = bmp_writer;
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
                init_usb(&context.w.uctx, vid, pid);
            } else {
                init_usb(&context.w.uctx, -1, -1);
            }
            context.write = usb_writer;
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

    setup_display(disp_name, &context);
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
            }
        }
        usleep(SLEEP_TIME_MSEC);
    }
}
  
