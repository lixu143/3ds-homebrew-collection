/**
 * @file wifi.c
 * @brief WiFi keep-alive for a resident sysmodule.
 *
 * Based on 3Input's wifi.cpp (verified on real hardware). Strategy:
 *  - hold our own `ac` connection request for as long as the module runs;
 *  - once a second check the link (ACU_GetStatus);
 *  - if it dropped, switch the radio back on (some games disable it) and
 *    re-request a connection a few times;
 *  - expose a "recovered" flag so the HTTP layer can rebuild its listener
 *    because the LAN IP may have changed.
 */
#include "wifi.h"
#include <3ds.h>

#define CHECK_TICKS   (SYSCLOCK_ARM11)       /* check ~once per second */
#define RETRY_TICKS   (SYSCLOCK_ARM11 * 3)   /* between reconnect attempts */
#define MAX_ATTEMPTS  10
#define AC_CONNECTED  3

static bool  s_acUp      = false;
static bool  s_nwmUp     = false;
static bool  s_reqHeld   = false;
static bool  s_linkUp    = false;
static bool  s_recovered = false;
static int   s_attempts  = 0;
static u64   s_nextCheck = 0;
static Handle s_connEvt  = 0;
static const char* s_status = "WiFi: pending";

static bool link_is_up(void)
{
    u32 st = 0, ap = 0;
    return R_SUCCEEDED(ACU_GetStatus(&st)) && st == AC_CONNECTED &&
           R_SUCCEEDED(ACU_GetWifiStatus(&ap)) && ap != 0;
}

/* Issue our own async connect request, like Rosalina's forced connection.
 * Returns false if one is still pending or the request failed. */
static bool request_connection(void)
{
    if (s_connEvt) {
        if (R_FAILED(svcWaitSynchronization(s_connEvt, 0)))
            return false; /* previous request still in flight */
        svcCloseHandle(s_connEvt);
        s_connEvt = 0;
    }

    acuConfig config;
    ACU_CreateDefaultConfig(&config);
    ACU_SetNetworkArea(&config, 2);
    ACU_SetAllowApType(&config, 0xFF); /* allow every slot */
    ACU_SetRequestEulaVersion(&config);

    if (R_FAILED(svcCreateEvent(&s_connEvt, RESET_ONESHOT))) {
        s_connEvt = 0;
        return false;
    }
    if (R_FAILED(ACU_ConnectAsync(&config, s_connEvt))) {
        svcCloseHandle(s_connEvt);
        s_connEvt = 0;
        return false;
    }
    return true;
}

void wifi_poll(void)
{
    u64 now = svcGetSystemTick();
    if (now < s_nextCheck) return;
    s_nextCheck = now + CHECK_TICKS;

    if (!s_acUp) {
        /* Never acExit() while running: an open ac session is itself part of
         * how the link is kept alive. */
        if (R_FAILED(acInit())) {
            s_status = "WiFi: acInit failed";
            s_nextCheck = now + RETRY_TICKS;
            return;
        }
        s_acUp = true;
    }

    if (link_is_up()) {
        if (!s_linkUp) {
            s_linkUp = true;
            s_attempts = 0;
            s_recovered = true; /* link came back -> IP may have changed */
        }
        if (!s_reqHeld)
            s_reqHeld = request_connection();
        s_status = "WiFi: connected";
        return;
    }

    /* link down */
    if (s_linkUp) { s_linkUp = false; s_attempts = 0; }
    if (s_attempts >= MAX_ATTEMPTS) {
        s_status = "WiFi: link down (gave up)";
        return;
    }
    s_attempts++;
    s_status = "WiFi: reconnecting";
    s_nextCheck = now + RETRY_TICKS;

    /* Some games switch the radio off after they start. */
    if (!s_nwmUp) s_nwmUp = R_SUCCEEDED(nwmExtInit());
    if (s_nwmUp) NWMEXT_ControlWirelessEnabled(true); /* "already on" is harmless */

    request_connection();
}

int wifi_took_recovery(void)
{
    int r = s_recovered;
    s_recovered = false;
    return r;
}

const char* wifi_status_str(void) { return s_status; }

void wifi_exit(void)
{
    if (s_connEvt) { svcCloseHandle(s_connEvt); s_connEvt = 0; }
    if (s_nwmUp) { nwmExtExit(); s_nwmUp = false; }
    if (s_acUp) { acExit(); s_acUp = false; }
}
