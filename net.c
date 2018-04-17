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

#include <netinet/in.h>

#include "x-viredero.h"

static bool sock_img_writer(struct context* ctx, int x, int y, int width, int height
                            , char* data, int data_len) {
    int size = width * height * 3;
    int fd = ctx->w.sctx.sock;
    if (0 == fd) {
        fd = accept(ctx->w.sctx.listen_sock, NULL, NULL);
        if (fd < 0) {
            slog(LOG_ERR, "Failed to accept connection: %m");
            exit(1);
        }
        ctx->w.sctx.sock = fd;
    }
    char* header = fill_imagecmd_header(data, data_len, width, height, x, y);
    size += 17;
    while (size > 0) {
        int sent = send(fd, header, size, 0);
        if (sent <= 0) {
            slog(LOG_WARNING, "send failed: %m");
            close(fd);
            fd = 0;
            return false;
        }
        size -= sent;
        header += sent;
    }
    return true;
}

void init_socket(struct context* ctx, uint16_t port) {
    struct sock_context* sctx = &ctx->w.sctx;
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
    ctx->write_image = sock_img_writer;
    ctx->write_pointer = dummy_pointer_writer;
}

