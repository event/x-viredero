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

#include "x-viredero.h"

#define BMP_FNAME_BUF_SIZE 128
static void swap_lines(char* line0, char* line1, int size) {
    int i;
    for (i = 0; i < size; i += 1) {
        line0[i] ^= line1[i];
        line1[i] ^= line0[i];
        line0[i] ^= line1[i];
    }
}

static int bmp_img_writer(struct context* ctx, int x, int y, int width, int height
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

void init_bmp(struct context* ctx, char* path) {
    struct bmp_context* bctx = &ctx->w.bctx;
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
    ctx->image_write = bmp_img_writer;
    ctx->pointer_write = dummy_pointer_writer;
}


