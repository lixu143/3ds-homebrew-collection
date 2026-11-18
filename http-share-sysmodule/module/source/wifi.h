/**
 * @file wifi.h
 * @brief Keep the WiFi link alive while a game is running.
 *
 * A background sysmodule must own an `ac` connection request, otherwise the
 * system/network may tear the link down. Mirrors the approach used by
 * Rosalina's debugger and by 3Input.
 */
#ifndef WIFI_H
#define WIFI_H

#ifdef __cplusplus
extern "C" {
#endif

/** Call ~once per second from the main loop. Cheap when nothing changed. */
void wifi_poll(void);

/** Returns 1 if the link came back since the last take (IP may have changed). */
int  wifi_took_recovery(void);

/** Current human-readable state, for the status log. */
const char* wifi_status_str(void);

/** Release any ac/nwm resources (called on module shutdown). */
void wifi_exit(void);

#ifdef __cplusplus
}
#endif

#endif
