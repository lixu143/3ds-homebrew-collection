/**
 * @file main.c
 * @brief HTTP file-share sysmodule entry point (always-on background process).
 *
 * This is a Category:Base system process installed to NAND as a CIA. It is
 * NOT launched from the HOME menu: a companion Launcher starts it via
 * NS_LaunchTitle(). Once running it stays resident and serves the SD card
 * share folder over HTTP while you play games.
 *
 * Shutdown is cooperative:
 *  - srv NOTIF_EXIT (system is shutting down / someone asked us to terminate)
 *  - the Launcher calling NS_TerminateProcessTID() on us
 */
#include <3ds.h>
#include <stdio.h>
#include <string.h>

#include "../../common/httpshare.h"
#include "wifi.h"

/* srv notification IDs (same values used by rehid / 3Input) */
#define NOTIF_EXIT      0x100u

/* How long the main loop sleeps between housekeeping passes (ns).
 * The HTTP engine runs inside hs_server_loop() on its own thread; this loop
 * only does WiFi keep-alive and watches for the exit notification. */
#define TICK_NS         (100 * 1000 * 1000ULL)   /* 100 ms */

/* libctru normally calls this for applications. As a sysmodule we must do it
 * ourselves and open ONLY the services we actually need. Opening apt/fs/sdmc
 * the way an app does is unnecessary and can fail for a Base process. */
void __appInit(void)
{
    srvInit();          /* required for every service */
    fsInit();           /* sdmc access for file transfers */
    sdmcInit();
}

void __appExit(void)
{
    sdmcExit();
    fsExit();
    srvExit();
}

/* A sysmodule has no screen; pass NULL as the log callback (the engine simply
 * skips logging) to keep this build free of any debug-output API dependency. */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    /* Start the HTTP engine. socInit() happens inside hs_init(). If WiFi is
     * not up yet this can fail; the loop below retries after link recovery. */
    int rc = hs_init(HS_DEFAULT_PORT, NULL);

    /* Subscribe to the "please terminate" notification so the OS / Launcher
     * can stop us cleanly. If this fails we still run, just without this path. */
    Handle notif = 0;
    bool haveNotif = R_SUCCEEDED(srvEnableNotification(&notif));
    if (haveNotif) srvSubscribe(NOTIF_EXIT);

    Thread serverThread = NULL;
    static u8 serverStack[0x10000] __attribute__((aligned(8)));
    if (rc == 0) {
        s32 prio = 0;
        svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
        serverThread = threadCreate(hs_server_loop, NULL, sizeof(serverStack),
                                    prio + 1, -2, false);
    }

    bool running = true;
    while (running) {
        svcSleepThread(TICK_NS);

        /* Check whether the system asked us to terminate. */
        if (haveNotif) {
            u32 id = 0;
            while (R_SUCCEEDED(srvReceiveNotification(&id))) {
                if (id == NOTIF_EXIT) { running = false; break; }
            }
        }

        /* WiFi keep-alive; if the link came back the IP may have changed, so
         * ask the engine to tear down and rebuild its listener. */
        wifi_poll();
        if (wifi_took_recovery()) {
            hs_request_stop();
            if (serverThread) { threadJoin(serverThread, U64_MAX); threadFree(serverThread); serverThread = NULL; }
            hs_exit();
            rc = hs_init(HS_DEFAULT_PORT, NULL);
            if (rc == 0) {
                s32 prio = 0;
                svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
                serverThread = threadCreate(hs_server_loop, NULL, sizeof(serverStack),
                                            prio + 1, -2, false);
            }
        }
    }

    /* orderly shutdown */
    hs_request_stop();
    if (serverThread) { threadJoin(serverThread, U64_MAX); threadFree(serverThread); }
    hs_exit();
    wifi_exit();
    if (haveNotif) svcCloseHandle(notif);

    /* A sysmodule must never "return" to a launcher the normal way; ending
     * the process is the correct terminal action. */
    svcExitProcess();
    return 0;
}
