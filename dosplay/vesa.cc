#include "vesa.h"
#include <dpmi.h>
#include <go32.h>
#include <pc.h>
#include <sys/nearptr.h>
#include <sys/farptr.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <emmintrin.h>

#define DOS_MEM_BASE 0x0
#define VESA_INFO_PTR 0x2000 // use a small buffer in conventional memory (e.g. at 0x2000)

typedef struct __attribute__((packed)) {
    uint16_t attributes;
    uint8_t winA, winB;
    uint16_t granularity;
    uint16_t winsize;
    uint16_t segmentA, segmentB;
    uint32_t realFctPtr;
    uint16_t pitch;
    uint16_t x_res, y_res;
    uint8_t w_char, y_char, planes, bpp, banks;
    uint8_t memory_model, bank_size, image_pages;
    uint8_t reserved0;
    uint8_t red_mask, red_position;
    uint8_t green_mask, green_position;
    uint8_t blue_mask, blue_position;
    uint8_t rsv_mask, rsv_position;
    uint8_t directcolor_attributes;
    uint32_t physbase;
    uint32_t offscreen_mem_offset;
    uint16_t offscreen_mem_size;
    uint8_t reserved1[206];
} VbeModeInfo;

int vesa_pitch = 0;
int vesa_bpp = 0;
uint32_t vesa_phys_base = 0;

// Page-flip state ------------------------------------------------------------
// 1 = flip during vertical retrace (tear-free, paced by refresh)
// 0 = flip immediately (uncapped, for benchmarking; may tear at the flip line)
int vesa_vsync_flip = 1;

static int       g_mode_h  = 0;        // scanlines per page (= mode height)
static uint8_t  *g_page[2] = {0, 0};   // near pointers to VRAM page 0 / page 1
static int       g_back    = 1;        // page currently used as back buffer

static int clamp(int val) {
    if (val < 0) return 0;
    if (val > 255) return 255;
    return val;
}

static void vesa_set_display_start(int page) {
    __dpmi_regs r;
    memset(&r, 0, sizeof(r));
    r.x.ax = 0x4F07;
    r.h.bh = 0x00;
    r.h.bl = vesa_vsync_flip ? 0x80 : 0x00; // 0x80 = schedule at vertical retrace
    r.x.cx = 0;                              // first displayed pixel (x)
    r.x.dx = page * g_mode_h;                // first displayed scanline (y)
    __dpmi_int(0x10, &r);
}

// Show the page we just drew into, then make the other page the new target.
void vesa_flip(void) {
    vesa_set_display_start(g_back);
    g_back ^= 1;
}

void vesa_set_vsync(int on) {
    vesa_vsync_flip = on ? 1 : 0;
}

uint32_t* vesa_init(int req_width, int req_height) {
    if (!__djgpp_nearptr_enable()) return NULL;

    __dpmi_regs r;
    uint16_t mode = 0x4118; // 0x118 (1024x768) + LFB bit

    int tb_seg = _go32_info_block.linear_address_of_transfer_buffer >> 4;
    int tb_lin = _go32_info_block.linear_address_of_transfer_buffer;

    // Query mode info (without LFB bit) to get physbase / pitch / bpp.
    memset(&r, 0, sizeof(r));
    r.x.ax = 0x4F01;
    r.x.cx = 0x118;
    r.x.es = tb_seg;
    r.x.di = 0;
    __dpmi_int(0x10, &r);

    if (r.x.ax != 0x004F) {
        printf("VESA mode 0x118 not supported!\n");
        return NULL;
    }

    VbeModeInfo *minfo = (VbeModeInfo *)(tb_lin + __djgpp_conventional_base);
    uint32_t phys_base = minfo->physbase;
    vesa_phys_base = phys_base;
    vesa_pitch = minfo->pitch;
    vesa_bpp = minfo->bpp;
    g_mode_h = minfo->y_res;

    if (phys_base == 0) {
        printf("No LFB available for this mode!\n");
        return NULL;
    }

    // Map TWO pages so we can page-flip. Needs >= 2 * pitch * height of VRAM
    // (6 MB at 1024x768x32) inside the LFB aperture.
    uint32_t page_bytes = (uint32_t)vesa_pitch * minfo->y_res;
    __dpmi_meminfo mem;
    mem.address = phys_base;
    mem.size = page_bytes * 2;
    if (__dpmi_physical_address_mapping(&mem) != 0) {
        // Fall back to a single page (no flipping) if 2 pages can't be mapped.
        mem.address = phys_base;
        mem.size = page_bytes;
        if (__dpmi_physical_address_mapping(&mem) != 0) {
            printf("Failed to map LFB!\n");
            return NULL;
        }
        uint8_t *base1 = (uint8_t *)(mem.address + __djgpp_conventional_base);
        g_page[0] = base1;
        g_page[1] = base1;   // both point at the same page -> flip is a no-op
    } else {
        uint8_t *base = (uint8_t *)(mem.address + __djgpp_conventional_base);
        g_page[0] = base;
        g_page[1] = base + page_bytes;
        // clear both pages so the very first flip never shows VRAM garbage
        memset(g_page[0], 0, page_bytes);
        memset(g_page[1], 0, page_bytes);
    }

    // Set the graphics mode.
    memset(&r, 0, sizeof(r));
    r.x.ax = 0x4F02;
    r.x.bx = mode;
    __dpmi_int(0x10, &r);

    // Display page 0; draw into page 1 first.
    g_back = 1;
    int saved = vesa_vsync_flip;
    vesa_vsync_flip = 0;           // don't wait for retrace during init
    vesa_set_display_start(0);
    vesa_vsync_flip = saved;

    return (uint32_t *)g_page[0];
}

#include <conio.h>

void vesa_restore_text_mode() {
    __dpmi_regs r;
    memset(&r, 0, sizeof(r));
    r.x.ax = 0x0003;
    __dpmi_int(0x10, &r);

    // Fallback/Reinforcement using conio.h
    textmode(C80);
    clrscr();
}

void vesa_wait_vsync() {
    int timeout = 1000000;
    while ((inportb(0x3DA) & 8) && timeout-- > 0);
    timeout = 1000000;
    while (!(inportb(0x3DA) & 8) && timeout-- > 0);
}

static void memcpy_wc(void *dst, const void *src, size_t len) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    size_t i = 0;
    for (; i + 63 < len; i += 64) {
        __m128i r0 = _mm_loadu_si128((const __m128i*)(s + i + 0));
        __m128i r1 = _mm_loadu_si128((const __m128i*)(s + i + 16));
        __m128i r2 = _mm_loadu_si128((const __m128i*)(s + i + 32));
        __m128i r3 = _mm_loadu_si128((const __m128i*)(s + i + 48));
        // Non-temporal stores enforce write-combining internally
        _mm_stream_si128((__m128i*)(d + i + 0), r0);
        _mm_stream_si128((__m128i*)(d + i + 16), r1);
        _mm_stream_si128((__m128i*)(d + i + 32), r2);
        _mm_stream_si128((__m128i*)(d + i + 48), r3);
    }
    for (; i < len; i++) {
        d[i] = s[i];
    }
    // Streaming stores are weakly ordered: make the whole blit globally visible
    // BEFORE we flip the display start to this page.
    _mm_sfence();
}

void vesa_draw_yuv420(uint32_t* lfb,
                      const uint8_t* y_ptr, const uint8_t* u_ptr, const uint8_t* v_ptr,
                      int pic_width, int pic_height,
                      int stride_y, int stride_c,
                      int screen_width, int screen_height)
{
    (void)lfb; // target page is chosen internally for double buffering

    // Simple top-left aligned mapping
    int w = pic_width < screen_width ? pic_width : screen_width;
    int h = pic_height < screen_height ? pic_height : screen_height;

    // Render into a cached system-RAM backbuffer (fast), then burst to VRAM.
    static uint32_t* backbuffer = NULL;
    if (!backbuffer) {
        backbuffer = (uint32_t*)malloc((size_t)vesa_pitch * screen_height);
    }

    for (int y = 0; y < h; y++) {
        uint8_t *dst_row = (uint8_t*)backbuffer + (y * vesa_pitch);
        const uint8_t *y_row = y_ptr + (y * stride_y);
        const uint8_t *u_row = u_ptr + ((y / 2) * stride_c);
        const uint8_t *v_row = v_ptr + ((y / 2) * stride_c);

        for (int x = 0; x < w; x+=2) {
            int U = u_row[x / 2] - 128;
            int V = v_row[x / 2] - 128;
            int U_G = (U >> 1) + (U >> 4) + (U >> 5);
            int V_G = (V >> 1) + (V >> 3) + (V >> 4) + (V >> 5);
            int U_B = U + (U >> 1) + (U >> 2) + (U >> 6);
            int V_R = V + (V >> 2) + (V >> 3) + (V >> 5);

            for (int dx = 0; dx < 2; dx++) {
                int Y = y_row[x + dx];

                int R = Y + V_R;
                int G = Y - U_G - V_G;
                int B = Y + U_B;

                R = R < 0 ? 0 : (R > 255 ? 255 : R);
                G = G < 0 ? 0 : (G > 255 ? 255 : G);
                B = B < 0 ? 0 : (B > 255 ? 255 : B);

                if (vesa_bpp == 32) {
                    ((uint32_t*)dst_row)[x + dx] = (R << 16) | (G << 8) | B;
                } else if (vesa_bpp == 24) {
                    dst_row[(x+dx)*3 + 0] = B;
                    dst_row[(x+dx)*3 + 1] = G;
                    dst_row[(x+dx)*3 + 2] = R;
                } else if (vesa_bpp == 16) {
                    ((uint16_t*)dst_row)[x + dx] = ((R >> 3) << 11) | ((G >> 2) << 5) | (B >> 3);
                }
            }
        }
    }

    // Burst-copy into the hidden VRAM page, then flip it on screen.
    memcpy_wc(g_page[g_back], backbuffer, (size_t)screen_height * vesa_pitch);
    vesa_flip();
}
