#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <conio.h>
#include <errno.h>
#include <sys/time.h>

#ifndef ENOBUFS
#define ENOBUFS 105
#endif

#include "../edge264.h"
#include "vesa.h"

#define READ_BUF_SZ (1024 * 1024)

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: dosplay <video.264> [screen_w] [screen_h] [--framedrop] [--fps N]\n");
        return 1;
    }
    int screen_w = 1024;
    int screen_h = 768;
    int framedrop = 0;
    int fps_limit = 30;
    char *video_file = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--framedrop") == 0) {
            framedrop = 1;
        } else if (strcmp(argv[i], "--fps") == 0 && i + 1 < argc) {
            fps_limit = atoi(argv[i+1]);
            i++;
        } else if (!video_file) {
            video_file = argv[i];
        } else if (screen_w == 1024) {
            screen_w = atoi(argv[i]);
        } else {
            screen_h = atoi(argv[i]);
        }
    }

    if (!video_file) {
        printf("Usage: dosplay <video.264> [screen_w] [screen_h] [--framedrop] [--fps N]\n");
        return 1;
    }

    FILE *f = fopen(video_file, "rb");
    if (!f) {
        printf("Failed to open %s\n", video_file);
        return 1;
    }

    uint8_t *file_buf = (uint8_t *)malloc(READ_BUF_SZ);
    if (!file_buf) {
        printf("Out of memory for chunk buffer.\n");
        fclose(f);
        return 1;
    }

    size_t bytes_in_buf = fread(file_buf, 1, READ_BUF_SZ, f);
    const uint8_t *nal = file_buf;
    const uint8_t *end0 = file_buf + bytes_in_buf;
    int eof_reached = feof(f);
    
    // skip the first start code if present
    if (bytes_in_buf > 3 && file_buf[0] == 0 && file_buf[1] == 0 && (file_buf[2] == 1 || (file_buf[2] == 0 && file_buf[3] == 1))) {
        nal += 3 + (file_buf[2] == 0);
    }

    // 0 threads for single core
    Edge264Decoder *dec = edge264_alloc(0, NULL, NULL, 0, NULL, NULL, NULL);
    Edge264Frame frm;
    if (!dec) { printf("DEC IS NULL!\n"); return 1; }
    int res = 0;
    int frames_decoded = 0;
    struct timeval start_tv, end_tv;

    printf("Initializing VESA %dx%d...\n", screen_w, screen_h);
    uint32_t* lfb = vesa_init(screen_w, screen_h);
    if (!lfb) {
        printf("Failed to init VESA mode.\n");
        free(file_buf);
        edge264_free(&dec);
        fclose(f);
        return 1;
    }

    gettimeofday(&start_tv, NULL);

    while (1) {
        if (kbhit()) {
            int c = getch();
            if (c == 27) break; // ESC to quit
        }

        const uint8_t *start_code = edge264_find_start_code(nal, end0, 0);

        // If we didn't find the next start code and we're not at EOF, read more
        if (start_code == end0 && !eof_reached) {
            size_t unparsed_len = end0 - nal;
            memmove(file_buf, nal, unparsed_len);
            size_t read_bytes = fread(file_buf + unparsed_len, 1, READ_BUF_SZ - unparsed_len, f);
            bytes_in_buf = unparsed_len + read_bytes;
            nal = file_buf;
            end0 = file_buf + bytes_in_buf;
            eof_reached = feof(f);
            continue;
        }

        res = edge264_decode_NAL(dec, nal, start_code, NULL, NULL);
        
        while (!edge264_get_frame(dec, &frm, 0)) {
           // vesa_wait_vsync();
            if (framedrop <= 0 || (frames_decoded % (framedrop + 1) == 0)) {
                vesa_draw_yuv420(lfb, frm.samples[0], frm.samples[1], frm.samples[2],
                                 frm.width_Y, frm.height_Y,
                                 frm.stride_Y, frm.stride_C,
                                 screen_w, screen_h);
            }
            frames_decoded++;
            
            if (fps_limit > 0) {
                struct timeval now;
                double target_time = frames_decoded / (double)fps_limit;
                while (1) {
                    gettimeofday(&now, NULL);
                    double elapsed = (now.tv_sec - start_tv.tv_sec) + (now.tv_usec - start_tv.tv_usec) / 1000000.0;
                    if (elapsed >= target_time) break;
                }
            }
        }

        if (res != ENOBUFS) {
            nal = start_code + 3;
        }
        
        if (nal >= end0 && eof_reached) {
            break;
        }
    }

    // Drain remaining frames
    res = edge264_decode_NAL(dec, end0, end0, NULL, NULL);
    while (!edge264_get_frame(dec, &frm, 0)) {
        // vesa_wait_vsync();
        if (framedrop <= 0 || (frames_decoded % (framedrop + 1) == 0)) {
            vesa_draw_yuv420(lfb, frm.samples[0], frm.samples[1], frm.samples[2],
                             frm.width_Y, frm.height_Y,
                             frm.stride_Y, frm.stride_C,
                             screen_w, screen_h);
        }
        frames_decoded++;
        
        if (fps_limit > 0) {
            struct timeval now;
            double target_time = frames_decoded / (double)fps_limit;
            while (1) {
                gettimeofday(&now, NULL);
                double elapsed = (now.tv_sec - start_tv.tv_sec) + (now.tv_usec - start_tv.tv_usec) / 1000000.0;
                if (elapsed >= target_time) break;
            }
        }
    }

    gettimeofday(&end_tv, NULL);

    vesa_restore_text_mode();
    edge264_free(&dec);
    free(file_buf);
    fclose(f);

    double elapsed = (end_tv.tv_sec - start_tv.tv_sec) + (end_tv.tv_usec - start_tv.tv_usec) / 1000000.0;
    printf("Decoded %d frames in %.2f seconds.\n", frames_decoded, elapsed);
    if (elapsed > 0.0) {
        printf("Average FPS: %.2f\n", frames_decoded / elapsed);
    }

    return 0;
}
