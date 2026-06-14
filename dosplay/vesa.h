#ifndef VESA_H
#define VESA_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize VESA mode (e.g. 1024, 768)
// Returns the linear pointer to the frame buffer, or NULL on failure.
uint32_t* vesa_init(int req_width, int req_height);

// Restore normal DOS text mode
void vesa_restore_text_mode();

// Wait for VSYNC
void vesa_wait_vsync();

// Convert a YUV420 planar frame to RGB32 and write to the frame buffer
void      vesa_draw_yuv420(uint32_t* lfb, const uint8_t* y, const uint8_t* u, const uint8_t* v,
                           int pic_w, int pic_h, int stride_y, int stride_c, int screen_w, int screen_h);

void      vesa_flip(void);
void      vesa_set_vsync(int on);

extern int vesa_pitch;
extern int vesa_bpp;
extern uint32_t vesa_phys_base;
extern int vesa_vsync_flip;

#ifdef __cplusplus
}
#endif

#endif
