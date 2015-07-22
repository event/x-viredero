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
#include <glob.h>

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
    va_list ap;
    va_start(ap, format);
    vfprintf(stdout, format, ap);
    va_end(ap);
    fprintf(stdout, "\n");
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

static void send_error_reply(struct context* ctx, enum CommandResultCode error) {
    char buf[2];
    buf[0] = InitReply;
    buf[1] = error;
    ctx->send_reply(ctx, buf, 2);
}

static int handshake(struct context* ctx) {
    char buf[INIT_CMD_LEN];
    char version;
    
    if (! ctx->init_conn(ctx, buf, INIT_CMD_LEN)) {
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
static char cursor_buf[18 + 256 * 4];
static int cursor_sent = 0;
int main(int argc, char* argv[]) {
    int i;
    int globres;
    glob_t g;
    char* buffer = (char*)malloc(8 * 1024 * 1024);
    init_usb(&context, 0, 0);
    
    slog(LOG_DEBUG, "%s up and running", PROG);
    if (context.init_conn) {
        if (!handshake(&context)) {
            slog(LOG_ERR, "handshake failed. Aborting...");
            exit(1);
        }
    }
    slog(LOG_NOTICE, "handshake success");
    globres = glob(argv[1], 0, NULL, &g);
    if (globres != 0) {
        slog(0, "glob failed: %d", globres);
        exit(1);
    }
    for(i = 0; i < g.gl_pathc; i += 1) {
        fscanf(stdin, "%s", buffer);
        char* p = g.gl_pathv[i];
        FILE* ppm = fopen(p, "rb");
        int width, height;
        slog(0, "show %s", p);
        if (fscanf(ppm, "P6 %d %d 255\n", &width, &height) != 2) {
            slog(0, "bad image %s", p);
            exit(1);
        }
        fread(buffer, 1, width * height * 3, ppm);
        
        context.write_image(&context, 0, 0, width, height, buffer);
    }
    globfree(&g);
}
  
