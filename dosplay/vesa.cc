#include "vesa.h"
#include <dpmi.h>
#include <go32.h>
#include <pc.h>
#include <sys/nearptr.h>
#include <sys/farptr.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

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

static int clamp(int val) {
    if (val < 0) return 0;
    if (val > 255) return 255;
    return val;
}

uint32_t* vesa_init(int req_width, int req_height) {
    if (!__djgpp_nearptr_enable()) return NULL;

    __dpmi_regs r;
    uint16_t best_mode = 0;
    uint32_t phys_base = 0;

    // We use mode 0x118 (1024x768x32 or 24) directly, with LFB bit (0x4000)
    // 0x4118 for VBE LFB.
    uint16_t mode = 0x4118;

    // Use DJGPP's standard Transfer Buffer for DOS API communication
    int tb_seg = _go32_info_block.linear_address_of_transfer_buffer >> 4;
    int tb_lin = _go32_info_block.linear_address_of_transfer_buffer;

    // Get mode info just to get physical base address
    memset(&r, 0, sizeof(r));
    r.x.ax = 0x4F01;
    r.x.cx = 0x118; // without LFB bit to query
    r.x.es = tb_seg;
    r.x.di = 0;
    __dpmi_int(0x10, &r);

    if (r.x.ax != 0x004F) {
        printf("VESA mode 0x118 not supported!\n");
        return NULL;
    }

    VbeModeInfo *minfo = (VbeModeInfo *)(tb_lin + __djgpp_conventional_base);
    phys_base = minfo->physbase;
    vesa_phys_base = phys_base;
    vesa_pitch = minfo->pitch;
    vesa_bpp = minfo->bpp;

    if (phys_base == 0) {
        printf("No LFB available for this mode!\n");
        return NULL;
    }

    // Map physical memory (safe size using actual pitch)
    __dpmi_meminfo mem;
    mem.address = phys_base;
    mem.size = vesa_pitch * minfo->y_res; 
    if (__dpmi_physical_address_mapping(&mem) != 0) {
        printf("Failed to map LFB!\n");
        return NULL;
    }

    // Set mode
    memset(&r, 0, sizeof(r));
    r.x.ax = 0x4F02;
    r.x.bx = mode;
    __dpmi_int(0x10, &r);

    return (uint32_t *)(mem.address + __djgpp_conventional_base);
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
    while (inportb(0x3DA) & 8);
    while (!(inportb(0x3DA) & 8));
}

void vesa_draw_yuv420(uint32_t* lfb, 
                      const uint8_t* y_ptr, const uint8_t* u_ptr, const uint8_t* v_ptr,
                      int pic_width, int pic_height,
                      int stride_y, int stride_c,
                      int screen_width, int screen_height) 
{
    // Simple top-left aligned mapping
    int w = pic_width < screen_width ? pic_width : screen_width;
    int h = pic_height < screen_height ? pic_height : screen_height;

    static uint32_t* backbuffer = NULL;
    if (!backbuffer) {
        backbuffer = (uint32_t*)malloc(screen_width * screen_height * 4);
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
    
    // Fast burst copy to VRAM
    memcpy(lfb, backbuffer, screen_height * vesa_pitch);
}
