/**
 * @file main.c
 * @brief Launcher application for the HTTP share sysmodule.
 *
 * This ordinary CIA does one job: start (or restart) the resident sysmodule
 * and show the IP/port you should point your browser at. It is meant to be
 * set as Luma's autoboot target so that, on power-on, it launches the module
 * and immediately hands the screen back to the HOME menu.
 *
 * TID formula for a Base system process:
 *   0x00040130 00000000 + (UniqueId << 8) + 0x02
 * Keep UNIQUE_ID here in sync with MODULE_UNIQUE_ID in the module Makefile.
 */
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ====== keep in sync with module/Makefile APP_UNIQUE_ID ====== */
#define MODULE_UNIQUE_ID  0x05A17u
#define MODULE_TID  (0x0004013000000000ULL | \
                     ((u64)MODULE_UNIQUE_ID << 8) | 0x02ULL)

#define SOC_ALIGN      0x1000
#define SOC_BUFFERSIZE 0x50000
#include <malloc.h>

static void show_ip(u32 *outAddr)
{
    static u32 *socbuf = NULL;
    socbuf = (u32*)memalign(SOC_ALIGN, SOC_BUFFERSIZE);
    if (!socbuf) { printf("  memalign failed\n"); return; }
    if (R_FAILED(socInit(socbuf, SOC_BUFFERSIZE))) {
        printf("  socInit failed (WiFi off?)\n");
        free(socbuf); socbuf = NULL;
        return;
    }
    struct in_addr ip, nm, bc;
    if (SOCU_GetIPInfo(&ip, &nm, &bc) == 0) {
        printf("  http://%s:8080/\n", inet_ntoa(ip));
        *outAddr = ip.s_addr;
    } else {
        printf("  (no IP - connect to WiFi first)\n");
    }
    socExit();
    free(socbuf); socbuf = NULL;
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    nsInit();
    hidInit();
    acInit();

    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);
    consoleInit(GFX_BOTTOM, NULL);

    u32 connected = 0;
    ACU_GetStatus(&connected);

    printf("HTTP Share 3DS - background server\n\n");
    printf("WiFi: %s\n", connected == 3 ? "connected" : "NOT connected");
    u32 anyIp = 0;
    if (connected == 3) show_ip(&anyIp);
    else printf("  Please join a WiFi network first.\n");

    printf("\nA = start / restart server\n");
    printf("B = exit\n\n");
    printf("If started, the server keeps running in the\n");
    printf("background even after you close this app.\n");

    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();

        if (kDown & KEY_A) {
            u32 pid = 0;
            /* Terminate any old instance first (ignore "not running"). */
            NS_TerminateProcessTID(MODULE_TID, 5 * 1000 * 1000 * 1000ULL);
            Result r = NS_LaunchTitle(MODULE_TID, 0, &pid);
            printf("\nLaunch module: %s", R_FAILED(r) ? "FAILED" : "OK");
            if (R_SUCCEEDED(r)) printf(" (pid %u)", pid);
            printf("\n  TID = %016llX\n", MODULE_TID);
            if (R_FAILED(r))
                printf("  Install HTTPShare_Module.cia via FBI first!\n");
        }
        if (kDown & KEY_B) break;

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    gfxExit();
    acExit();
    hidExit();
    nsExit();
    return 0;
}
