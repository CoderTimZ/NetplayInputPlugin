#include "stdafx.h"

#include "Controller 1.0.h"
#include "id_variable.h"
#include "plugin_dialog.h"
#include "settings.h"
#include "input_plugin.h"
#include "client.h"
#include "util.h"
#include "version.h"

using namespace std;

#if defined(__cplusplus)
extern "C" {
#endif

static bool loaded = false;
static bool rom_open = false;
static HMODULE this_dll = NULL;
static HWND main_window = NULL;
static CONTROL* netplay_controllers;
static shared_ptr<settings> my_settings;
static shared_ptr<input_plugin> my_plugin;
static shared_ptr<client> my_client;
static string my_location;
static array<bool, MAX_PLAYERS> port_already_visited;

BOOL WINAPI DllMain(HMODULE hinstDLL, DWORD dwReason, LPVOID lpvReserved) {
    switch (dwReason) {
        case DLL_PROCESS_ATTACH:
            this_dll = hinstDLL;
            break;

        case DLL_PROCESS_DETACH:
            break;
        
        case DLL_THREAD_ATTACH:
            break;
        
        case DLL_THREAD_DETACH:
            break;
    }

    return TRUE;
}

void load() {
    if (loaded) {
        return;
    }

    SetErrorMode(SEM_FAILCRITICALERRORS);

    wchar_t my_location_array[MAX_PATH];
    GetModuleFileName(this_dll, my_location_array, MAX_PATH);
    wcsrchr(my_location_array, L'\\')[1] = 0;
    my_location = wstring_to_utf8(my_location_array);

    my_settings = make_shared<settings>(my_location + "netplay_input_plugin.ini");

    try {
        my_plugin = make_shared<input_plugin>(my_location + my_settings->get_plugin_dll());
    } catch(const exception&) {
        my_plugin.reset();
    }

    loaded = true;
}

EXPORT int _IDENTIFYING_VARIABLE = 0;

EXPORT void CALL CloseDLL (void) {
    if (my_client) {
        my_settings->set_name(my_client->get_name());
    }

    my_settings->save();

    my_client.reset();
    my_settings.reset();
    my_plugin.reset();

    loaded = false;
}

EXPORT void CALL ControllerCommand( int Control, BYTE * Command) {
    load();
}

EXPORT void CALL DllAbout ( HWND hParent ) {
    load();

    string message = string(APP_NAME) + "\n\nVersion: " + string(APP_VERSION) + "\n\nAuthor: @CoderTimZ (aka AQZ)\n\nWebsite: www.play64.com";

    MessageBox(hParent, utf8_to_wstring(message).c_str(), L"About", MB_OK | MB_ICONINFORMATION);
}

EXPORT void CALL DllConfig ( HWND hParent ) {
    load();

    if (rom_open) {
        if (my_plugin != NULL) {
            my_plugin->DllConfig(hParent);

            if (my_client != NULL) {
                my_client->set_local_controllers(my_plugin->controls);
            }
        }
    } else {
        my_plugin = shared_ptr<input_plugin>();

        assert(main_window != NULL);

        plugin_dialog dialog(this_dll, hParent, my_location, my_settings->get_plugin_dll(), main_window);

        if (dialog.ok_clicked()) {
            my_settings->set_plugin_dll(dialog.get_plugin_dll());
        }

        try {
            my_plugin = make_shared<input_plugin>(my_location + my_settings->get_plugin_dll());
            my_plugin->InitiateControllers0100(main_window, my_plugin->controls);
        }
        catch(const exception&) { }
    }
}

EXPORT void CALL DllTest ( HWND hParent ) {
    load();

    if (my_plugin == NULL) {
        MessageBox(NULL, L"No input plugin has been selected", L"Warning", MB_OK | MB_ICONWARNING);
    }
}

EXPORT void CALL GetDllInfo ( PLUGIN_INFO * PluginInfo ) {
    load();

    PluginInfo->Version = 0x0100;
    PluginInfo->Type = PLUGIN_TYPE_CONTROLLER;

    strncpy(PluginInfo->Name, APP_NAME_AND_VERSION, sizeof PLUGIN_INFO::Name);
}

EXPORT void CALL GetKeys(int Control, BUTTONS * Keys ) {
    load();

    if (my_plugin == NULL || my_client == NULL) {
        return;
    }

    my_client->wait_until_start();

    if (port_already_visited[Control]) {
        port_already_visited.fill(false);

        BUTTONS local_input[MAX_PLAYERS];
        for (int port = 0; port < MAX_PLAYERS; port++) {
            my_plugin->GetKeys(port, &local_input[port]);
        }

        my_client->process_input(local_input);
    }
    
    my_client->get_input(Control, Keys);

    port_already_visited[Control] = true;
}

EXPORT void CALL InitiateControllers (HWND hMainWindow, CONTROL Controls[4]) {
    load();

    netplay_controllers = Controls;

    if (my_plugin != NULL) {
        my_plugin->InitiateControllers0100(hMainWindow, my_plugin->controls);
    }

    main_window = hMainWindow;
}

EXPORT void CALL ReadController ( int Control, BYTE * Command ) {
    load();
}

EXPORT void CALL RomClosed (void) {
    load();

    if (my_client) {
        my_settings->set_name(my_client->get_name());
        my_client->post_close();
        my_client.reset();
    }

    if (my_plugin) {
        my_plugin->RomClosed();
    }

    rom_open = false;
}

EXPORT void CALL RomOpen (void) {
    load();

    assert(main_window != NULL);

    rom_open = true;

    if (my_plugin == NULL) {
        DllConfig(main_window);
    }

    if (my_plugin != NULL) {
        my_client = make_shared<client>(make_shared<asio::io_service>(), make_shared<client_dialog>(this_dll, main_window));
        my_client->set_name(my_settings->get_name());
        my_client->set_netplay_controllers(netplay_controllers);
        my_client->load_public_server_list();

        my_plugin->RomOpen();
        my_client->set_local_controllers(my_plugin->controls);
    }

    port_already_visited.fill(true);
}

EXPORT void CALL WM_KeyDown( WPARAM wParam, LPARAM lParam ) {
    load();

    if (my_plugin != NULL) {
        my_plugin->WM_KeyDown(wParam, lParam);
    }
}

EXPORT void CALL WM_KeyUp( WPARAM wParam, LPARAM lParam ) {
    load();

    if (my_plugin != NULL) {
        my_plugin->WM_KeyUp(wParam, lParam);
    }
}

#if defined(__cplusplus)
}
#endif
