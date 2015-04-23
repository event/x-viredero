/*
 * X11 window sequence collector for viredero
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
#include <string.h>
#include <syslog.h>
#include <netinet/in.h>

#define min(a, b) (((a) < (b)) ? (a) : (b))

#define PROG "xv-testo"
#define SLEEP_TIME_MSEC 50
#define DISP_NAME_MAXLEN 64

static int max_log_level = 8;

struct bmp_context {
    int num;
    char* path;
    char* fname;
};
    
static int bmp_writer_init(struct bmp_context* bmp_ctx) {
    bmp_ctx->path = "/tmp/imgs/w%0.6d.bmp";
    bmp_ctx->num = 0;
    bmp_ctx->fname = malloc(64);
}
    
struct __attribute__ ((__packed__)) bm_head {
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
};

struct __attribute__ ((__packed__)) bm_info_head {
    uint32_t biSize;
    uint32_t biWidth;
    uint32_t biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    uint32_t biXPelsPerMeter;
    uint32_t biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
};

static void usage() {
    printf("USAGE: "PROG" <ipaddr:port>\n");
}

struct bmp_context bmp_ctx;

static int error(const char* text) {
    fprintf(stderr, "%s: %m", text);
    exit(1);
}

#define FNAME_TMPLT "/tmp/xv-client/image%.6d.bmp"
#define BUF_SIZE 2048
#define BYTE_PER_PIX 4

static char buf[BUF_SIZE];
static char fname[128];

int main(int argc, char* argv[]) {
    char* disp_name;
    int fin = 0;
    uint16_t port;
    int sock;
    char* _port;
    struct sockaddr_in addr;
    if (argc != 2) {
        usage();
        exit(1);
    }
    _port = strrchr((char*)argv[1], ':');
    if (NULL == _port) {
        usage();
        exit(1);
    }
    _port[0] = '\0';
    _port += 1;
    port = (uint16_t)strtol(_port, NULL, 10);
    inet_aton(argv[1], &addr.sin_addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        error("socket creation failed");
    }
    if (connect(sock, (struct sockaddr*)&addr, sizeof(struct sockaddr_in)) < 0) {
        error("connect failed");
    }
    struct bm_head head;
    struct bm_info_head ihead; 
    int num = 0;
    head.bfType = 0x4D42;
    head.bfReserved1 = 0;
    head.bfReserved2 = 0;
    ihead.biSize = sizeof(struct bm_info_head);
    ihead.biPlanes = 1;
    ihead.biBitCount = 8 * BYTE_PER_PIX;
    ihead.biCompression = 0;
    ihead.biXPelsPerMeter = 0;
    ihead.biYPelsPerMeter = 0;
    ihead.biClrUsed = 0;
    ihead.biClrImportant = 0;
    while (num < 5) {
        int x, y, width, height, size;
        char cmd;
        recv(sock, &cmd, 1, 0);
        fprintf(stderr, "got cmd %hhd\n", cmd);
        recv(sock, &width, 4, 0);
        recv(sock, &height, 4, 0);
        recv(sock, &x, 4, 0);
        recv(sock, &y, 4, 0);
        FILE *f;
        width = ntohl(width);
        height = ntohl(height);
        fprintf(stderr, "got x/y/w/h = %d/%d/%d/%d\n", ntohl(x), ntohl(y), width, height);
        
        head.bfSize = sizeof(struct bm_head) + sizeof(struct bm_info_head)
            + width * height * BYTE_PER_PIX;
        head.bfOffBits = sizeof(struct bm_head) + sizeof(struct bm_info_head);
    
        ihead.biWidth = width;
        ihead.biHeight = height;
        ihead.biSizeImage = width * height * BYTE_PER_PIX;
        sprintf(fname, FNAME_TMPLT, num);
        f = fopen(fname, "wb");
        if(f == NULL) {
            return;
        }
        fwrite(&head, sizeof(struct bm_head), 1, f);
        fwrite(&ihead, sizeof(struct bm_info_head), 1, f);
        size = BYTE_PER_PIX * width * height;
        fprintf(stderr, "image %.6d size %d\n", num, size);
        while (size > 0) {
            int i = recv(sock, buf, min(size, BUF_SIZE), 0);
//            fprintf(stderr, "read %d\n", i);
            fwrite(buf, i, 1, f);
            size -= i;
        }
        fclose(f);
        num += 1;
    }
}
