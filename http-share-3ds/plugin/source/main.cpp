/**
 * @file main.cpp
 * @brief HTTP Share 3GX plugin - background HTTP server while you play.
 *
 * Architecture (verified against ArticBase, a real 3GX plugin that runs a
 * background socket server inside a running game):
 *   PatchProcess()  -> called BEFORE the game starts; we spawn the server
 *                      thread here so it is live for the whole game session.
 *   main()          -> builds a small PluginMenu (opened with SELECT) that
 *                      shows the URL and transfer stats. The game keeps
 *                      running normally; the menu is only an overlay.
 *
 * The actual HTTP logic lives in ../common/httpshare.c and is shared with
 * the standalone app.
 */
#include <3ds.h>
#include <CTRPluginFramework.hpp>

#include <string>
#include <vector>

#include "../../common/httpshare.h"

namespace CTRPluginFramework
{
    static Thread  s_serverThread = nullptr;
    static u8      s_stack[0x10000] __attribute__((aligned(8)));
    static bool    s_started = false;
    static int     s_last_error = 0;

    /* background thread entry: runs the HTTP server loop forever */
    static void ServerThread(void *arg)
    {
        (void)arg;
        hs_server_loop(NULL);
    }

    /* Called before main and before the game starts: safe place to patch
       and to launch our long-lived background thread. */
    void PatchProcess(FwkSettings &settings)
    {
        (void)settings;
        int rc = hs_init(HS_DEFAULT_PORT, NULL);
        if (rc != 0) {
            s_last_error = rc;   // reported in the status menu
            return;
        }
        s32 prio = 0;
        svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
        s_serverThread = threadCreate(ServerThread, nullptr, sizeof(s_stack),
                                      prio - 1, -2, false);
        s_started = (s_serverThread != nullptr);
        if (!s_started)
            s_last_error = -9;
    }

    /* Called when the process exits: ask the server to stop and join. */
    void OnProcessExit(void)
    {
        if (!s_started) return;
        hs_request_stop();
        if (s_serverThread) {
            threadJoin(s_serverThread, U64_MAX);
            threadFree(s_serverThread);
            s_serverThread = nullptr;
        }
        hs_exit();
        s_started = false;
    }

    /* Build the small status menu shown with SELECT. */
    void InitMenu(PluginMenu &menu)
    {
        menu += new MenuEntry("Show status / URL", nullptr, [](MenuEntry *entry)
        {
            (void)entry;
            HSStatus st;
            hs_get_status(&st);
            std::string body;
            body = "HTTP Share 3DS\n\n";
            body += "Status : " + std::string(s_started ? "RUNNING" : "STOPPED") + "\n";
            body += "IP     : " + std::string(st.ip) + "\n";
            body += "URL    : http://" + std::string(st.ip) + ":" +
                    std::to_string(st.port) + "/\n\n";
            body += "Folder : " + std::string(HS_ROOT) + "\n\n";
            body += "Uploads   : " + std::to_string(st.uploads) + "\n";
            body += "Downloads : " + std::to_string(st.downloads) + "\n";
            body += "Deletes   : " + std::to_string(st.deletes) + "\n";
            body += "RX " + std::to_string(st.bytes_rx/1024) + " KB  /  TX " +
                    std::to_string(st.bytes_tx/1024) + " KB\n\n";
            body += "Last: " + std::string(st.last_event) + "\n";
            if (s_last_error)
                body += "Init error code: " + std::to_string(s_last_error) + "\n";
            MessageBox("HTTP Share", body)();
        });

        menu += new MenuEntry("Restart server", nullptr, [](MenuEntry *entry)
        {
            (void)entry;
            if (!s_started) {
                int rc = hs_init(HS_DEFAULT_PORT, NULL);
                if (rc == 0) {
                    s32 prio = 0;
                    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
                    s_serverThread = threadCreate(ServerThread, nullptr,
                                                  sizeof(s_stack), prio-1, -2, false);
                    s_started = (s_serverThread != nullptr);
                }
                s_last_error = s_started ? 0 : (rc != 0 ? rc : -9);
            } else {
                hs_request_stop();
                if (s_serverThread) {
                    threadJoin(s_serverThread, U64_MAX);
                    threadFree(s_serverThread);
                    s_serverThread = nullptr;
                }
                hs_exit();
                s_started = false;
            }
        });
    }

    int main(void)
    {
        PluginMenu *menu = new PluginMenu("HTTP Share 3DS", 1, 0, 0,
            "Background HTTP file server. Open, upload/download in a browser, "
            "then keep playing - no need to leave the game.");
        menu->SynchronizeWithFrame(true);
        InitMenu(*menu);
        menu->Run();
        delete menu;
        return 0;
    }
}
