/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include <fcntl.h>
#include <stdio.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <linux/fb.h>
#include <linux/kd.h>

#include <pixelflinger/pixelflinger.h>

#include "minui.h"

#ifdef BOARD_USE_CUSTOM_RECOVERY_FONT
#include BOARD_USE_CUSTOM_RECOVERY_FONT
#else
#include "font_cn_32x32.h"
#endif
#include "unicode_map.h"
#include "zlib.h"
#include <string.h>

typedef unsigned int ucs4_t;
#define RET_SHIFT_ILSEQ(n)  (-1-2*(n))
#define RET_ILSEQ           RET_SHIFT_ILSEQ(0)
#define RET_TOOFEW(n)       (-2-2*(n))
#define RET_ILUNI      -1
#define RET_TOOSMALL   -2


#ifdef RECOVERY_BGRA
#define PIXEL_FORMAT GGL_PIXEL_FORMAT_BGRA_8888
#define PIXEL_SIZE 4
#endif
#ifdef RECOVERY_RGBX
#define PIXEL_FORMAT GGL_PIXEL_FORMAT_RGBX_8888
#define PIXEL_SIZE 4
#endif
#ifndef PIXEL_FORMAT
#define PIXEL_FORMAT GGL_PIXEL_FORMAT_RGB_565
#define PIXEL_SIZE 2
#endif


static GGLSurface font_ftex;

// #define PRINT_SCREENINFO 1 // Enables printing of screen info to log

typedef struct {
    GGLSurface texture;
    unsigned offset[97];
    void** fontdata;
    unsigned count;
    unsigned *unicodemap;
    unsigned char *cwidth;
    unsigned char *cheight;
    unsigned ascent;
} GRFont;

static GRFont *gr_font = 0;
static GGLContext *gr_context = 0;
static GGLSurface gr_font_texture;
static GGLSurface gr_framebuffer[2];
static GGLSurface gr_mem_surface;
static unsigned gr_active_fb = 0;

static int gr_fb_fd = -1;
static int gr_vt_fd = -1;

static struct fb_var_screeninfo vi;
static struct fb_fix_screeninfo fi;

#ifdef PRINT_SCREENINFO
static void print_fb_var_screeninfo()
{
	LOGI("vi.xres: %d\n", vi.xres);
	LOGI("vi.yres: %d\n", vi.yres);
	LOGI("vi.xres_virtual: %d\n", vi.xres_virtual);
	LOGI("vi.yres_virtual: %d\n", vi.yres_virtual);
	LOGI("vi.xoffset: %d\n", vi.xoffset);
	LOGI("vi.yoffset: %d\n", vi.yoffset);
	LOGI("vi.bits_per_pixel: %d\n", vi.bits_per_pixel);
	LOGI("vi.grayscale: %d\n", vi.grayscale);
}
#endif

static int get_framebuffer(GGLSurface *fb)
{
    int fd;
    void *bits;

    fd = open("/dev/graphics/fb0", O_RDWR);
    if (fd < 0) {
        perror("cannot open fb0");
        return -1;
    }

    if (ioctl(fd, FBIOGET_VSCREENINFO, &vi) < 0) {
        perror("failed to get fb0 info");
        close(fd);
        return -1;
    }

    fprintf(stderr, "Pixel format: %dx%d @ %dbpp\n", vi.xres, vi.yres, vi.bits_per_pixel);

    vi.bits_per_pixel = PIXEL_SIZE * 8;
    if (PIXEL_FORMAT == GGL_PIXEL_FORMAT_BGRA_8888) {
        fprintf(stderr, "Pixel format: BGRA_8888\n");
        if (PIXEL_SIZE != 4)    fprintf(stderr, "E: Pixel Size mismatch!\n");
        vi.red.offset     = 8;
        vi.red.length     = 8;
        vi.green.offset   = 16;
        vi.green.length   = 8;
        vi.blue.offset    = 24;
        vi.blue.length    = 8;
        vi.transp.offset  = 0;
        vi.transp.length  = 8;
    } else if (PIXEL_FORMAT == GGL_PIXEL_FORMAT_RGBX_8888) {
        fprintf(stderr, "Pixel format: RGBX_8888\n");
        if (PIXEL_SIZE != 4)    fprintf(stderr, "E: Pixel Size mismatch!\n");
        vi.red.offset     = 24;
        vi.red.length     = 8;
        vi.green.offset   = 16;
        vi.green.length   = 8;
        vi.blue.offset    = 8;
        vi.blue.length    = 8;
        vi.transp.offset  = 0;
        vi.transp.length  = 8;
    } else if (PIXEL_FORMAT == GGL_PIXEL_FORMAT_RGB_565) {
#ifdef RECOVERY_RGB_565
		fprintf(stderr, "Pixel format: RGB_565\n");
		vi.blue.offset    = 0;
		vi.green.offset   = 5;
		vi.red.offset     = 11;
#else
        fprintf(stderr, "Pixel format: BGR_565\n");
		vi.blue.offset    = 11;
		vi.green.offset   = 5;
		vi.red.offset     = 0;
#endif
		if (PIXEL_SIZE != 2)    fprintf(stderr, "E: Pixel Size mismatch!\n");
		vi.blue.length    = 5;
		vi.green.length   = 6;
		vi.red.length     = 5;
        vi.blue.msb_right = 0;
        vi.green.msb_right = 0;
        vi.red.msb_right = 0;
        vi.transp.offset  = 0;
        vi.transp.length  = 0;
    }
    else
    {
        perror("unknown pixel format");
        close(fd);
        return -1;
    }

    vi.vmode = FB_VMODE_NONINTERLACED;
    vi.activate = FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;

    if (ioctl(fd, FBIOPUT_VSCREENINFO, &vi) < 0) {
        perror("failed to put fb0 info");
        close(fd);
        return -1;
    }

    if (ioctl(fd, FBIOGET_FSCREENINFO, &fi) < 0) {
        perror("failed to get fb0 info");
        close(fd);
        return -1;
    }

    bits = mmap(0, fi.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (bits == MAP_FAILED) {
        perror("failed to mmap framebuffer");
        close(fd);
        return -1;
    }

#ifdef RECOVERY_GRAPHICS_USE_LINELENGTH
    vi.xres_virtual = fi.line_length / PIXEL_SIZE;
#endif

    fb->version = sizeof(*fb);
    fb->width = vi.xres;
    fb->height = vi.yres;
#ifdef BOARD_HAS_JANKY_BACKBUFFER
    LOGI("setting JANKY BACKBUFFER\n");
    fb->stride = fi.line_length/2;
#else
    fb->stride = vi.xres_virtual;
#endif
    fb->data = bits;
    fb->format = PIXEL_FORMAT;
    memset(fb->data, 0, vi.yres * fb->stride * PIXEL_SIZE);

    fb++;

    fb->version = sizeof(*fb);
    fb->width = vi.xres;
    fb->height = vi.yres;
#ifdef BOARD_HAS_JANKY_BACKBUFFER
    fb->stride = fi.line_length/2;
    fb->data = (void*) (((unsigned) bits) + vi.yres * fi.line_length);
#else
    fb->stride = vi.xres_virtual;
    fb->data = (void*) (((unsigned) bits) + vi.yres * fb->stride * PIXEL_SIZE);
#endif
    fb->format = PIXEL_FORMAT;
    memset(fb->data, 0, vi.yres * fb->stride * PIXEL_SIZE);

#ifdef PRINT_SCREENINFO
	print_fb_var_screeninfo();
#endif

    return fd;
}

static void get_memory_surface(GGLSurface* ms) {
  ms->version = sizeof(*ms);
  ms->width = vi.xres;
  ms->height = vi.yres;
  ms->stride = vi.xres_virtual;
  ms->data = malloc(vi.xres_virtual * vi.yres * PIXEL_SIZE);
  ms->format = PIXEL_FORMAT;
}

static void set_active_framebuffer(unsigned n)
{
    if (n > 1) return;
    vi.yres_virtual = vi.yres * 2;
    vi.yoffset = n * vi.yres;
//    vi.bits_per_pixel = PIXEL_SIZE * 8;
    if (ioctl(gr_fb_fd, FBIOPUT_VSCREENINFO, &vi) < 0) {
        perror("active fb swap failed");
    }
}

void gr_flip(void)
{
    GGLContext *gl = gr_context;

    /* swap front and back buffers */
    gr_active_fb = (gr_active_fb + 1) & 1;

#ifdef BOARD_HAS_FLIPPED_SCREEN
    /* flip buffer 180 degrees for devices with physicaly inverted screens */
    unsigned int i;
    for (i = 1; i < (vi.xres * vi.yres); i++) {
        unsigned short tmp = gr_mem_surface.data[i];
        gr_mem_surface.data[i] = gr_mem_surface.data[(vi.xres * vi.yres * 2) - i];
        gr_mem_surface.data[(vi.xres * vi.yres * 2) - i] = tmp;
    }
#endif

    /* copy data from the in-memory surface to the buffer we're about
     * to make active. */
    memcpy(gr_framebuffer[gr_active_fb].data, gr_mem_surface.data,
           vi.xres_virtual * vi.yres * PIXEL_SIZE);

    /* inform the display driver */
    set_active_framebuffer(gr_active_fb);
}

void gr_color(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
    GGLContext *gl = gr_context;
    GGLint color[4];
    color[0] = ((r << 8) | r) + 1;
    color[1] = ((g << 8) | g) + 1;
    color[2] = ((b << 8) | b) + 1;
    color[3] = ((a << 8) | a) + 1;
    gl->color4xv(gl, color);
}

static int utf8_mbtowc (ucs4_t *pwc, const unsigned char *s, int n)
{
  unsigned char c = s[0];

  if (c < 0x80) {
    *pwc = c;
    return 1;
  } else if (c < 0xc2) {
    return RET_ILSEQ;
  } else if (c < 0xe0) {
    if (n < 2)
      return RET_TOOFEW(0);
    if (!((s[1] ^ 0x80) < 0x40))
      return RET_ILSEQ;
    *pwc = ((ucs4_t) (c & 0x1f) << 6)
           | (ucs4_t) (s[1] ^ 0x80);
    return 2;
  } else if (c < 0xf0) {
    if (n < 3)
      return RET_TOOFEW(0);
    if (!((s[1] ^ 0x80) < 0x40 && (s[2] ^ 0x80) < 0x40
          && (c >= 0xe1 || s[1] >= 0xa0)))
      return RET_ILSEQ;
    *pwc = ((ucs4_t) (c & 0x0f) << 12)
           | ((ucs4_t) (s[1] ^ 0x80) << 6)
           | (ucs4_t) (s[2] ^ 0x80);
    return 3;
  } else if (c < 0xf8 && sizeof(ucs4_t)*8 >= 32) {
    if (n < 4)
      return RET_TOOFEW(0);
    if (!((s[1] ^ 0x80) < 0x40 && (s[2] ^ 0x80) < 0x40
          && (s[3] ^ 0x80) < 0x40
          && (c >= 0xf1 || s[1] >= 0x90)))
      return RET_ILSEQ;
    *pwc = ((ucs4_t) (c & 0x07) << 18)
           | ((ucs4_t) (s[1] ^ 0x80) << 12)
           | ((ucs4_t) (s[2] ^ 0x80) << 6)
           | (ucs4_t) (s[3] ^ 0x80);
    return 4;
  } else if (c < 0xfc && sizeof(ucs4_t)*8 >= 32) {
    if (n < 5)
      return RET_TOOFEW(0);
    if (!((s[1] ^ 0x80) < 0x40 && (s[2] ^ 0x80) < 0x40
          && (s[3] ^ 0x80) < 0x40 && (s[4] ^ 0x80) < 0x40
          && (c >= 0xf9 || s[1] >= 0x88)))
      return RET_ILSEQ;
    *pwc = ((ucs4_t) (c & 0x03) << 24)
           | ((ucs4_t) (s[1] ^ 0x80) << 18)
           | ((ucs4_t) (s[2] ^ 0x80) << 12)
           | ((ucs4_t) (s[3] ^ 0x80) << 6)
           | (ucs4_t) (s[4] ^ 0x80);
    return 5;
  } else if (c < 0xfe && sizeof(ucs4_t)*8 >= 32) {
    if (n < 6)
      return RET_TOOFEW(0);
    if (!((s[1] ^ 0x80) < 0x40 && (s[2] ^ 0x80) < 0x40
          && (s[3] ^ 0x80) < 0x40 && (s[4] ^ 0x80) < 0x40
          && (s[5] ^ 0x80) < 0x40
          && (c >= 0xfd || s[1] >= 0x84)))
      return RET_ILSEQ;
    *pwc = ((ucs4_t) (c & 0x01) << 30)
           | ((ucs4_t) (s[1] ^ 0x80) << 24)
           | ((ucs4_t) (s[2] ^ 0x80) << 18)
           | ((ucs4_t) (s[3] ^ 0x80) << 12)
           | ((ucs4_t) (s[4] ^ 0x80) << 6)
           | (ucs4_t) (s[5] ^ 0x80);
    return 6;
  } else
    return RET_ILSEQ;
}

int ch_utf8_length(const char* s)
{
    int res;
    ucs4_t ch;

    res = utf8_mbtowc(&ch, (const unsigned char*)s, strlen(s));
    if(res <= 0)
        return 0;
    return res;
}

int ch_utf8(const char* s)
{
    int res;
    ucs4_t ch;

    res = utf8_mbtowc(&ch, (const unsigned char*)s, strlen(s));
    if(res <= 0)
        return 0;
    return ch;
}

int getCharID(const char* s, void* pFont)
{
	unsigned i, unicode;
	GRFont *gfont = (GRFont*) pFont;
	if (!gfont)  gfont = gr_font;
	unicode = ch_utf8(s);
	for (i = 0; i < gfont->count; i++) 
	{
		if (unicode == gfont->unicodemap[i])
		return i;
	}
	return 0;
}

int gr_measureEx(const char *s, void* font)
{
    GRFont* fnt = (GRFont*) font;
    int n, l, off;

    if (!fnt)   fnt = gr_font;

    n = 0;
    off = 0;
    while(*(s + off)) {
        l = ch_utf8_length(s + off);
		n += fnt->cwidth[getCharID(s+off,font)];
        off += l;
    }
    return n;
}

int gr_textEx(int x, int y, const char *s, void* pFont)
{
    GGLContext *gl = gr_context;
    GRFont *gfont = (GRFont*) pFont;
    unsigned off, width, height, n;

    /* Handle default font */
    if (!gfont)  gfont = gr_font;

    y -= gfont->ascent;

    gl->texEnvi(gl, GGL_TEXTURE_ENV, GGL_TEXTURE_ENV_MODE, GGL_REPLACE);
    gl->texGeni(gl, GGL_S, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
    gl->texGeni(gl, GGL_T, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
    gl->enable(gl, GGL_TEXTURE_2D);

    while(*s) {
        if(*((unsigned char*)(s)) < 0x20) {
            s++;
            continue;
        }
		off = getCharID(s,pFont);
        n = ch_utf8_length(s);
        if(n <= 0)
            break;
        s += n;
    	width = gfont->cwidth[off];
    	height = gfont->cheight[off];
        memcpy(&font_ftex, &gfont->texture, sizeof(font_ftex));
        font_ftex.width = width;
        font_ftex.height = height;
        font_ftex.stride = width;
        font_ftex.data = gfont->fontdata[off];
        gl->bindTexture(gl, &font_ftex);
	    gl->texCoord2i(gl, 0 - x, 0 - y);
        gl->recti(gl, x, y, x + width, y + height);
        x += width;
    }
    return x;
}

int gr_textExW(int x, int y, const char *s, void* pFont, int max_width)
{
    GGLContext *gl = gr_context;
    GRFont *gfont = (GRFont*) pFont;
    unsigned off, width, height, n;

    /* Handle default font */
    if (!gfont)  gfont = gr_font;

    y -= gfont->ascent;

    gl->texEnvi(gl, GGL_TEXTURE_ENV, GGL_TEXTURE_ENV_MODE, GGL_REPLACE);
    gl->texGeni(gl, GGL_S, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
    gl->texGeni(gl, GGL_T, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
    gl->enable(gl, GGL_TEXTURE_2D);

    while(*s) {
        if(*((unsigned char*)(s)) < 0x20) {
            s++;
            continue;
        }
		off = getCharID(s,pFont);
        n = ch_utf8_length(s);
        if(n <= 0)
            break;
        s += n;
    	width = gfont->cwidth[off];
    	height = gfont->cheight[off];
        memcpy(&font_ftex, &gfont->texture, sizeof(font_ftex));
        font_ftex.width = width;
        font_ftex.height = height;
        font_ftex.stride = width;
        font_ftex.data = gfont->fontdata[off];
        gl->bindTexture(gl, &font_ftex);
	    gl->texCoord2i(gl, 0 - x, 0 - y);
		if ((x + (int)width) < max_width) {
			gl->recti(gl, x, y, x + width, y + height);
			x += width;
		}
		else {
			gl->recti(gl, x, y, max_width, y + height);
			x += max_width;
			return x;
		}
    }
    return x;
}

int gr_textExWH(int x, int y, const char *s, void* pFont, int max_width, int max_height)
{
    GGLContext *gl = gr_context;
    GRFont *gfont = (GRFont*) pFont;
    unsigned off, width, height, n;
    int rect_x, rect_y;

    /* Handle default font */
    if (!gfont)  gfont = gr_font;

    y -= gfont->ascent;

    gl->texEnvi(gl, GGL_TEXTURE_ENV, GGL_TEXTURE_ENV_MODE, GGL_REPLACE);
    gl->texGeni(gl, GGL_S, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
    gl->texGeni(gl, GGL_T, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
    gl->enable(gl, GGL_TEXTURE_2D);

    while(*s) {
        if(*((unsigned char*)(s)) < 0x20) {
            s++;
            continue;
        }
		off = getCharID(s,pFont);
        n = ch_utf8_length(s);
        if(n <= 0)
            break;
        s += n;
    	width = gfont->cwidth[off];
    	height = gfont->cheight[off];
        memcpy(&font_ftex, &gfont->texture, sizeof(font_ftex));
        font_ftex.width = width;
        font_ftex.height = height;
        font_ftex.stride = width;
        font_ftex.data = gfont->fontdata[off];
        gl->bindTexture(gl, &font_ftex);
		if ((x + (int)width) < max_width)
			rect_x = x + width;
		else
			rect_x = max_width;
		if (y + height < (unsigned int)(max_height))
			rect_y = y + height;
		else
			rect_y = max_height;
	    gl->texCoord2i(gl, 0 - x, 0 - y);
		gl->recti(gl, x, y, rect_x, rect_y);
		x += width;
		if (x > max_width)
			return x;
        }
    return x;
}

int twgr_text(int x, int y, const char *s)
{
    GGLContext *gl = gr_context;
    GRFont *gfont = gr_font;
    unsigned off, width, height, n;

    y -= gfont->ascent;

    gl->texEnvi(gl, GGL_TEXTURE_ENV, GGL_TEXTURE_ENV_MODE, GGL_REPLACE);
    gl->texGeni(gl, GGL_S, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
    gl->texGeni(gl, GGL_T, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
    gl->enable(gl, GGL_TEXTURE_2D);

    while(*s) {
        if(*((unsigned char*)(s)) < 0x20) {
            s++;
            continue;
        }
		off = getCharID(s,NULL);
        n = ch_utf8_length(s);
        if(n <= 0)
            break;
        s += n;
    	width = gfont->cwidth[off];
    	height = gfont->cheight[off];
        memcpy(&font_ftex, &gfont->texture, sizeof(font_ftex));
        font_ftex.width = width;
        font_ftex.height = height;
        font_ftex.stride = width;
        font_ftex.data = gfont->fontdata[off];
        gl->bindTexture(gl, &font_ftex);
	    gl->texCoord2i(gl, 0 - x, 0 - y);
        gl->recti(gl, x, y, x + width, y + height);
        x += width;
    }
    return x;
}

void gr_fill(int x, int y, int w, int h)
{
    GGLContext *gl = gr_context;
    gl->disable(gl, GGL_TEXTURE_2D);
    gl->recti(gl, x, y, x + w, y + h);
}

void gr_blit(gr_surface source, int sx, int sy, int w, int h, int dx, int dy) {
    if (gr_context == NULL) {
        return;
    }

    GGLContext *gl = gr_context;
    gl->bindTexture(gl, (GGLSurface*) source);
    gl->texEnvi(gl, GGL_TEXTURE_ENV, GGL_TEXTURE_ENV_MODE, GGL_REPLACE);
    gl->texGeni(gl, GGL_S, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
    gl->texGeni(gl, GGL_T, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
    gl->enable(gl, GGL_TEXTURE_2D);
    gl->texCoord2i(gl, sx - dx, sy - dy);
    gl->recti(gl, dx, dy, dx + w, dy + h);
}

unsigned int gr_get_width(gr_surface surface) {
    if (surface == NULL) {
        return 0;
    }
    return ((GGLSurface*) surface)->width;
}

unsigned int gr_get_height(gr_surface surface) {
    if (surface == NULL) {
        return 0;
    }
    return ((GGLSurface*) surface)->height;
}

void* gr_loadFont(const char* fontName)
{
    int fd, bit, bmp ,pos;
    GRFont *font = 0;
    GGLSurface *ftex;
    unsigned char data, *cwidth, *cheight;
    unsigned width, height, i;
    void** font_data;

    fd = open(fontName, O_RDONLY);
    if (fd == -1)
    {
        char tmp[128];
        sprintf(tmp, "/res/fonts/%s.dat", fontName);
        fd = open(tmp, O_RDONLY);
        if (fd == -1)
            return NULL;
    }
    font = calloc(sizeof(*font), 1);
    ftex = &font->texture;
    read(fd, &width, sizeof(unsigned));
    if ((width&0x00FFFFFF)==0x088B1F) 
    	return gr_loadFont_cn(fontName);
    read(fd, &height, sizeof(unsigned));
    read(fd, font->offset, sizeof(unsigned) * 96);
    font->offset[96] = width;
	font_data = (void**)malloc(gr_font->count * sizeof(void*));
    cwidth = malloc(gr_font->count);
    cheight = malloc(gr_font->count);
	for (i=0; i < gr_font->count;i++) {
		if (i < 95) {
			cwidth[i] = font->offset[i+1]-font->offset[i];
			cheight[i] = height;
			font_data[i] = malloc(cwidth[i]*cheight[i]);
			memset(font_data[i], 0, cwidth[i]*cheight[i]);
		}
		else {
			cwidth[i] = gr_font->cwidth[i];
			cheight[i] = gr_font->cheight[i];
			font_data[i] = malloc(cwidth[i]*cheight[i]);
			memset(font_data[i], 0, cwidth[i]*cheight[i]);
			font_data[i] = gr_font->fontdata[i];
		}
	}
    i = 0;
    while (i < width * height) {
        read(fd, &data, 1);
        for (bit = 0; bit < 8; bit++) {
			for (pos = 0;pos < 95; pos++) {
				if (i%width == font->offset[pos]) 
					bmp = pos;
			}
			pos = i%width-font->offset[bmp]+i/width*cwidth[bmp];
            if (data&(1<<(7-bit)))
            	((unsigned char*)(font_data[bmp]))[pos] = 0xFF;
            else
            	((unsigned char*)(font_data[bmp]))[pos] = 0x00;
			i++;
            if (i == width * height)  break;
        }
    }
    close(fd);
    ftex->version = sizeof(*ftex);
    ftex->format = GGL_PIXEL_FORMAT_A_8;
    font->count = gr_font->count;
    font->unicodemap = unicodemap;
	font->cwidth = cwidth;
	font->cheight = cheight;
    font->fontdata = font_data;
	font->ascent = 0;
    return (void*) font;
}

void* gr_loadFont_cn(const char* fontName)
{
	gzFile fd;
    GRFont *font = 0;
    GGLSurface *ftex;
    unsigned char *cwidth, *cheight, *width, *height, *commonsign;
    unsigned *unicode, *common, *extend, *fontindex, count, cnt, i, j, repeat, font_count;
    void** font_data;
	void** font_data_tmp;

	fd = gzopen(fontName,"rb");
    if (fd == NULL)
    {
        char tmp[128];
        sprintf(tmp, "/res/fonts/%s.dat", fontName);
        fd = gzopen(tmp, "rb");
        if (fd == NULL)
            return NULL;
    }
    font = calloc(sizeof(*font), 1);
    ftex = &font->texture;
    gzread(fd, &count, sizeof(unsigned));
	unicode = malloc(count*sizeof(unsigned));
    gzread(fd, unicode, count*sizeof(unsigned));
	common = malloc(UNICODE_NUM*sizeof(unsigned));
	commonsign = malloc(UNICODE_NUM);
	memset(commonsign, 0, UNICODE_NUM);
	extend = malloc(count*sizeof(unsigned));
    repeat = 0;
    cnt = 0;
	for (i=0; i < count;i++) {
		int tmp = 0;
		for (j=0; j < UNICODE_NUM;j++) {
			if (unicode[i] == unicodemap[j]) {
				commonsign[j] = 1;
				common[j] = i;
				repeat++;
				tmp = 1;
			}
		}
		if (!tmp) {
			extend[cnt] = i;
			cnt++;
		}
	}
	font_count = count+UNICODE_NUM-repeat;
	fontindex = malloc(font_count*sizeof(unsigned));
	for(i=UNICODE_NUM,j=0;j<cnt;i++,j++)
		fontindex[i] = unicode[extend[j]];
    width = malloc(count);
    height = malloc(count);
    gzread(fd, width, count);
    gzread(fd, height, count);
    font_data_tmp = (void**)malloc(count * sizeof(void*));
    for (i=0; i < count;i++) {
    	font_data_tmp[i] = malloc(width[i]*height[i]);
		memset(font_data_tmp[i], 0, width[i]*height[i]);
		gzread(fd, font_data_tmp[i], width[i]*height[i]);
	}
    cwidth = malloc(font_count);
    cheight = malloc(font_count);
    font_data = (void**)malloc(font_count * sizeof(void*));
	for (i=0; i < font_count;i++) {
		if (i<UNICODE_NUM) {
			fontindex[i] = unicodemap[i];
			if (commonsign[i]) {
				cwidth[i] = width[common[i]];
				cheight[i] = height[common[i]];
				font_data[i] = malloc(cwidth[i]*cheight[i]);
				memset(font_data[i], 0, cwidth[i]*cheight[i]);
				font_data[i] = font_data_tmp[common[i]];
			}
			else {
				cwidth[i] = gr_font->cwidth[i];
				cheight[i] = gr_font->cheight[i];
				font_data[i] = malloc(cwidth[i]*cheight[i]);
				memset(font_data[i], 0, cwidth[i]*cheight[i]);
				font_data[i] = gr_font->fontdata[i];
			}
		}
		else {
			for(j=0;j<cnt;j++) {
				if (unicode[extend[j]] == fontindex[i]) {
					cwidth[i] = width[extend[j]];
					cheight[i] = height[extend[j]];
					font_data[i] = malloc(cwidth[i]*cheight[i]);
					memset(font_data[i], 0, cwidth[i]*cheight[i]);
					font_data[i] = font_data_tmp[extend[j]];
				}
			}
		}
	}
    gzclose(fd);
    ftex->version = sizeof(*ftex);
    ftex->format = GGL_PIXEL_FORMAT_A_8;
    font->count = font_count;
    font->unicodemap = fontindex;
	font->cwidth = cwidth;
	font->cheight = cheight;
    font->fontdata = font_data;
	font->ascent = 0;
    return (void*) font;
}

int gr_getFontDetails(void* font, unsigned* cheight, unsigned* maxwidth)
{
    GRFont *fnt = (GRFont*) font;

    if (!fnt)   fnt = gr_font;
    if (!fnt)   return -1;
	
    if (cheight) {
        unsigned pos;
        *cheight = 0;
        for (pos = 0; pos < fnt->count; pos++) {
            unsigned int height = fnt->cheight[pos];
            if (height > *cheight)
                *cheight = height;
        }
    }
    if (maxwidth) {
        unsigned pos;
        *maxwidth = 0;
        for (pos = 0; pos < fnt->count; pos++) {
            unsigned int width = pos * fnt->cwidth[pos];
            if (width > *maxwidth)
                *maxwidth = width;
        }
    }
    return 0;
}

static void gr_init_font(void)
{
    GGLSurface *ftex;
    unsigned char *bits;
    unsigned char *in, data;
    int bmp, pos;
    unsigned i, d, n;
    void** font_data;
    unsigned char *width, *height;
    gr_font = calloc(sizeof(*gr_font), 1);
    ftex = &gr_font->texture;

    font_data = (void**)malloc(font.count * sizeof(void*));
    width = malloc(font.count);
    height = malloc(font.count);
    for(n = 0; n < font.count; n++) {
		if (n<95) {
			font_data[n] = malloc(font.ewidth*font.eheight);
			memset(font_data[n], 0, font.ewidth*font.eheight);
			width[n] = font.ewidth;
			height[n] = font.eheight;
		}
		else {
			font_data[n] = malloc(font.cwidth*font.cheight);
			memset(font_data[n], 0, font.cwidth * font.cheight);
			width[n] = font.cwidth;
			height[n] = font.cheight;
		}
	}
    d = 0;
    in = font.rundata;
    while((data = *in++)) {
        n = data & 0x7f;
        for(i = 0; i < n; i++, d++) {
			if (d<95*font.ewidth*font.eheight) {
				bmp = d/(font.ewidth*font.eheight);
				pos = d%(font.ewidth*font.eheight);
			}
			else {
				bmp = (d-95*font.ewidth*font.eheight)/(font.cwidth*font.cheight)+95;
				pos = (d-95*font.ewidth*font.eheight)%(font.cwidth*font.cheight);
			}
            ((unsigned char*)(font_data[bmp]))[pos] = (data & 0x80) ? 0xff : 0;
        }

    }

    ftex->version = sizeof(*ftex);
    ftex->format = GGL_PIXEL_FORMAT_A_8;

	gr_font->count = font.count;
    gr_font->unicodemap = unicodemap;
    gr_font->cwidth = width;
    gr_font->cheight = height;
    gr_font->fontdata = font_data;
	gr_font->ascent = 0;
}

int gr_init(void)
{
    gglInit(&gr_context);
    GGLContext *gl = gr_context;

    gr_init_font();
    gr_vt_fd = open("/dev/tty0", O_RDWR | O_SYNC);
    if (gr_vt_fd < 0) {
        // This is non-fatal; post-Cupcake kernels don't have tty0.
    } else if (ioctl(gr_vt_fd, KDSETMODE, (void*) KD_GRAPHICS)) {
        // However, if we do open tty0, we expect the ioctl to work.
        perror("failed KDSETMODE to KD_GRAPHICS on tty0");
        gr_exit();
        return -1;
    }

    gr_fb_fd = get_framebuffer(gr_framebuffer);
    if (gr_fb_fd < 0) {
        perror("Unable to get framebuffer.\n");
        gr_exit();
        return -1;
    }

    get_memory_surface(&gr_mem_surface);

    fprintf(stderr, "framebuffer: fd %d (%d x %d)\n",
            gr_fb_fd, gr_framebuffer[0].width, gr_framebuffer[0].height);

    /* start with 0 as front (displayed) and 1 as back (drawing) */
    gr_active_fb = 0;
    set_active_framebuffer(0);
    gl->colorBuffer(gl, &gr_mem_surface);

    gl->activeTexture(gl, 0);
    gl->enable(gl, GGL_BLEND);
    gl->blendFunc(gl, GGL_SRC_ALPHA, GGL_ONE_MINUS_SRC_ALPHA);

//    gr_fb_blank(true);
//    gr_fb_blank(false);

    return 0;
}

void gr_exit(void)
{
    close(gr_fb_fd);
    gr_fb_fd = -1;

    free(gr_mem_surface.data);

    ioctl(gr_vt_fd, KDSETMODE, (void*) KD_TEXT);
    close(gr_vt_fd);
    gr_vt_fd = -1;
}

int gr_fb_width(void)
{
    return gr_framebuffer[0].width;
}

int gr_fb_height(void)
{
    return gr_framebuffer[0].height;
}

gr_pixel *gr_fb_data(void)
{
    return (unsigned short *) gr_mem_surface.data;
}

void gr_fb_blank(int blank)
{
    int ret;

    ret = ioctl(gr_fb_fd, FBIOBLANK, blank ? FB_BLANK_POWERDOWN : FB_BLANK_UNBLANK);
    if (ret < 0)
        perror("ioctl(): blank");
}

int gr_get_surface(gr_surface* surface)
{
    GGLSurface* ms = malloc(sizeof(GGLSurface));
    if (!ms)    return -1;

    // Allocate the data
    get_memory_surface(ms);

    // Now, copy the data
    memcpy(ms->data, gr_mem_surface.data, vi.xres * vi.yres * vi.bits_per_pixel / 8);

    *surface = (gr_surface*) ms;
    return 0;
}

int gr_free_surface(gr_surface surface)
{
    if (!surface)
        return -1;

    GGLSurface* ms = (GGLSurface*) surface;
    free(ms->data);
    free(ms);
    return 0;
}

void gr_write_frame_to_file(int fd)
{
    write(fd, gr_mem_surface.data, vi.xres * vi.yres * vi.bits_per_pixel / 8);
}

