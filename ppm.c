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
#include <stdbool.h>
#include <unistd.h>
#include <syslog.h>

#include "x-viredero.h"

#define PPM_FNAME_BUF_SIZE 128
#define PPM_IMAGE_FORMAT SF_PNG

static bool ppm_img_writer_with_header(struct context* ctx, char* data
                                       , int data_len, char* header) {
    struct ppm_context* pctx = &ctx->w.pctx;
    FILE *f;
    if (0 == data_len) {
        return false;
    }
    snprintf(pctx->fname, PPM_FNAME_BUF_SIZE, pctx->path, pctx->num);
    slog(LOG_DEBUG, "save damage to %s", pctx->fname);
    f = fopen(pctx->fname, "wb");
    if (NULL == f) {
        return false;
    }
    if (header) {
        fwrite(header, strlen(header), 1, f);
    }
    fwrite(data, data_len, 1, f);
    fclose(f);
    pctx->num += 1;
    return true;
}

static bool ppm_img_writer(struct context* ctx, int x, int y, int width, int height
                           , char* data, int data_len) {
#if PPM_IMAGE_FORMAT == SF_RGB
    char header[32];
    sprintf(header, "P6 %d %d 255\n", width, height); //bmp header
    ppm_img_writer_with_header(ctx, data, data_len, header);
#endif
    ppm_img_writer_with_header(ctx, data, data_len, NULL);

}
static bool ppm_pntr_writer(struct context* ctx, int x, int y, int width, int height
                           , char* data) {
    return ppm_img_writer_with_header(ctx, data, width * height * 4, NULL);
}

static bool ppm_init_conn(struct context* ctx, char* buf, int size) {
    buf[0] = 0;
    buf[1] = 1; //VIREDERO protocol version
    buf[2] = PPM_IMAGE_FORMAT;
    buf[3] = PF_RGBA;
    return true;
}

static bool return_true() {
    return true;
}

static bool return_false() {
    return false;
}

void init_ppm(struct context* ctx, char* path) {
    struct ppm_context* pctx = &ctx->w.pctx;
    pctx->num = 0;
    pctx->path = path;
    pctx->fname = malloc(PPM_FNAME_BUF_SIZE);
    ctx->write_image = ppm_img_writer;
    ctx->write_pointer = ppm_pntr_writer;
    ctx->init_conn = ppm_init_conn;
    ctx->check_reinit = return_false;
    ctx->send_reply = return_true;
}


