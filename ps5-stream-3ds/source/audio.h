/**
 * @file audio.h
 * @brief PBS Payload /audio receiver for Nintendo 3DS / New 3DS.
 *
 * PBS serves a continuous audio stream at GET /audio:
 *   HTTP/1.1 200 OK ... \r\n\r\n
 *   then repeated blocks: [u32 len BE][u32 pts BE][len bytes payload]
 *   payload = pcm_f32le, 48 kHz, stereo (interleaved L,R float32).
 *
 * This module reads that stream in a worker thread, converts float32 -> s16
 * and feeds the 3DS NDSP hardware mixer (48 kHz stereo PCM16), so no software
 * codec is needed. socInit() must already have been called by the video
 * receiver (mjpeg_start) before audio_start() is used.
 */
#ifndef AUDIO_H
#define AUDIO_H

#include <3ds.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int      connected;     /* 1 while the audio thread is online        */
    uint32_t chunks;        /* audio blocks received this session        */
    uint32_t bytes_rx;      /* payload bytes received (excl. headers)    */
    float    volume;        /* current volume (0.0 .. 1.0)               */
    char     status[128];   /* short human-readable status               */
} AudioStatus;

/**
 * Start the audio receiver thread. host/port are the PBS server address.
 * Returns 0 on success, negative on failure. Does NOT call socInit().
 */
int  audio_start(const char *host, uint16_t port);

/** Stop the audio thread, clear the DSP channel and free buffers. */
void audio_stop(void);

/** Copy a status snapshot (thread-safe enough for a UI read). */
void audio_get_status(AudioStatus *out);

/** Set output volume, 0.0 (mute) .. 1.0 (full). Clamped internally. */
void audio_set_volume(float v);

#ifdef __cplusplus
}
#endif

#endif
