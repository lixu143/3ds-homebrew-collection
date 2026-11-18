/**
 * @file renderer.h
 * @brief Present decoded MJPEG frames on the 3DS top screen via citro3d.
 */
#ifndef RENDERER_H
#define RENDERER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <3ds.h>

/** Initialise the render target / texture state. Call once after C2D_Init. */
int  renderer_init(void);

/** Clean up GPU resources. */
void renderer_exit(void);

/**
 * Upload one RGBA8888 frame and draw it fullscreen on the top screen.
 * rgba is a tightly packed w*h*4 buffer (row stride = w*4).
 */
void renderer_show(uint8_t *rgba, int w, int h, int fit_mode);

/** Draw a "no signal" placeholder screen. */
void renderer_placeholder(const char *line1, const char *line2);

#ifdef __cplusplus
}
#endif

#endif
