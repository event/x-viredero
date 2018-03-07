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

static bool ppm_img_writer(struct context* ctx, int x, int y, int width, int height
                      , char* data) {
    struct ppm_context* pctx = &ctx->w.pctx;
    FILE *f;
    int size = width * height * 3;
    if (0 == size) {
        return false;
    }
    snprintf(pctx->fname, PPM_FNAME_BUF_SIZE, pctx->path, pctx->num);
    slog(LOG_DEBUG, "save damage to %s", pctx->fname);
    f = fopen(pctx->fname, "wb");
    if (f == NULL) {
        return false;
    }
    fprintf(f, "P6 %d %d 255\n", width, height);
    fwrite(data+1, size, 1, f);
    fclose(f);
    pctx->num += 1;
    return true;
}

static bool ppm_write_pointerr(struct context* ctx, int x, int y
                              , int width, int height, char* pointer) {
    struct ppm_context* pctx = &ctx->w.pctx;
    FILE *f;
    int size = width * height * 3;
    snprintf(pctx->fname, PPM_FNAME_BUF_SIZE, pctx->path, pctx->num);
//    slog(LOG_DEBUG, "save pointer to %s", pctx->fname);
    f = fopen(pctx->fname, "wb");
    if (f == NULL) {
        return false;
    }
    fprintf(f, "P6 %d %d 255\n", width, height);
    fwrite(pointer, size, 1, f);
    fclose(f);
    pctx->num += 1;
    return true;
}

void init_ppm(struct context* ctx, char* path) {
    struct ppm_context* pctx = &ctx->w.pctx;
    pctx->num = 0;
    pctx->path = path;
    pctx->fname = malloc(PPM_FNAME_BUF_SIZE);
    ctx->write_image = ppm_img_writer;
    ctx->write_pointer = ppm_img_writer;
}


