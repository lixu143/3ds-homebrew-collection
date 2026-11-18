/**
 * @file inputctl.h
 * @brief PS5 remote-controller input bridge (PBS Payload HTTP input API).
 *
 * PBS Payload exposes a virtual DualSense on the PS5. This module connects
 * to it and periodically POSTs controller state so the 3DS buttons/touch
 * can drive the PS5. Protocol (from PBS Payload README):
 *
 *   POST /input/connect                 -> returns a token (plain text)
 *   POST /input/state                   headers:
 *       X-PBS-Input: 1
 *       X-PBS-Controller: <token>
 *     body (8 unsigned decimal CSV fields):
 *       sequence,buttons,lx,ly,rx,ry,l2,r2
 *       sticks centered at 128, axes/triggers 0..255,
 *       sequence advances mod 2^32, send at least every 100 ms.
 *   POST /input/disconnect              (same headers)
 */
#ifndef INPUTCTL_H
#define INPUTCTL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <3ds.h>
#include <stdint.h>

/** A snapshot of desired virtual-controller state, produced by the UI. */
typedef struct {
    uint32_t buttons;   /* bitmask, see INPUT_BTN_* below */
    uint8_t  lx, ly;    /* left stick, 128 = center */
    uint8_t  rx, ry;    /* right stick */
    uint8_t  l2, r2;    /* triggers, 0..255 */
} InputState;

/* Button bits (bit position within the DualSense-style buttons field).
 * NOTE: PBS maps these to a standard gamepad bit layout; adjust in
 * inputctl.c's button_map if your PBS build uses a different bit order. */
#define INPUT_BTN_CROSS      (1u << 0)
#define INPUT_BTN_CIRCLE     (1u << 1)
#define INPUT_BTN_SQUARE     (1u << 2)
#define INPUT_BTN_TRIANGLE   (1u << 3)
#define INPUT_BTN_L1         (1u << 4)
#define INPUT_BTN_R1         (1u << 5)
#define INPUT_BTN_L2         (1u << 6)
#define INPUT_BTN_R2         (1u << 7)
#define INPUT_BTN_CREATE     (1u << 8)
#define INPUT_BTN_OPTIONS    (1u << 9)
#define INPUT_BTN_L3         (1u << 10)
#define INPUT_BTN_R3         (1u << 11)
#define INPUT_BTN_UP         (1u << 12)
#define INPUT_BTN_DOWN       (1u << 13)
#define INPUT_BTN_LEFT       (1u << 14)
#define INPUT_BTN_RIGHT      (1u << 15)
#define INPUT_BTN_PS         (1u << 16)

/** Connect to the PS5 input API, obtain a controller token. 0 on success. */
int  inputctl_connect(const char *host, uint16_t port);

/** Disconnect and release the virtual controller. */
void inputctl_disconnect(void);

/** Push one state update (called ~every 50-100 ms by the UI thread). */
int  inputctl_send(const InputState *st);

/** 1 if we currently own a controller session. */
int  inputctl_connected(void);

#ifdef __cplusplus
}
#endif

#endif
