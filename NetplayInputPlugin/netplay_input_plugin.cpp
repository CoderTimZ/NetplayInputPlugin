#include "stdafx.h"

#include "Controller 1.1.h"
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
static CONTROL_INFO control_info = { NULL, NULL, FALSE, NULL, NULL };
static shared_ptr<settings> my_settings;
static shared_ptr<input_plugin> my_plugin;
static shared_ptr<client> my_client;
static string my_location;
static array<bool, 4> port_already_visited;
static rom_info rom;

BOOL WINAPI DllMain(HMODULE hinstDLL, DWORD dwReason, LPVOID lpvReserved) {
    switch (dwReason) {
        case DLL_PROCESS_DETACH:
            break;

        case DLL_PROCESS_ATTACH:
            this_dll = hinstDLL;
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

void unload() {
    if (!loaded) {
        return;
    }

    if (my_client) {
        my_settings->set_name(my_client->get_name());
    }

    my_settings->save();

    my_client.reset();
    my_settings.reset();
    my_plugin.reset();

    loaded = false;
}

EXPORT int _IDENTIFYING_VARIABLE = 0;

EXPORT void CALL CloseDLL (void) {
    unload();
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
        if (my_plugin) {
            if (!my_plugin->controllers_initiated) {
                if (my_plugin->InitiateControllers0100) {
                    my_plugin->InitiateControllers0100(my_plugin->control_info.hMainWindow, my_plugin->control_info.Controls);
                    my_plugin->controllers_initiated = true;
                } else if (my_plugin->InitiateControllers0101) {
                    my_plugin->InitiateControllers0101(my_plugin->control_info);
                    my_plugin->controllers_initiated = true;
                }
            }
            my_plugin->DllConfig(hParent);

            if (my_client) {
                my_client->set_src_controllers(my_plugin->control_info.Controls);
            }
        }
    } else {
        my_plugin.reset();

        assert(control_info.hMainWindow);

        plugin_dialog dialog(this_dll, hParent, my_location, my_settings->get_plugin_dll(), control_info);

        if (dialog.ok_clicked()) {
            my_settings->set_plugin_dll(dialog.get_plugin_dll());
        }

        my_plugin = make_shared<input_plugin>(my_location + my_settings->get_plugin_dll());
        my_plugin->control_info.hMainWindow = control_info.hMainWindow;
        my_plugin->control_info.hinst = control_info.hinst;
        my_plugin->control_info.MemoryBswaped = control_info.MemoryBswaped;
        my_plugin->control_info.HEADER = control_info.HEADER;
    }
}

EXPORT void CALL DllTest ( HWND hParent ) {
    load();

    if (!my_plugin) {
        MessageBox(NULL, L"No input plugin has been selected", L"Warning", MB_OK | MB_ICONWARNING);
    }
}

EXPORT void CALL GetDllInfo ( PLUGIN_INFO * PluginInfo ) {
    load();

    PluginInfo->Version = 0x0101;
    PluginInfo->Type = PLUGIN_TYPE_CONTROLLER;

    strncpy(PluginInfo->Name, APP_NAME_AND_VERSION, sizeof PLUGIN_INFO::Name);
}

EXPORT void CALL GetKeys(int Control, BUTTONS* Keys) {
    static array<BUTTONS, 4> input;

    load();

    if (!my_plugin || !my_client) {
        return;
    }

    my_client->wait_until_start();

    if (port_already_visited[Control]) {
        port_already_visited.fill(false);

        for (int port = 0; port < 4; port++) {
            my_plugin->GetKeys(port, &input[port]);
        }

        my_client->process_input(input);
    }
    
    *Keys = input[Control];

    port_already_visited[Control] = true;
}

EXPORT void CALL InitiateControllers (CONTROL_INFO ControlInfo) {
    load();

    control_info = ControlInfo;

    if (my_plugin && !my_plugin->controllers_initiated) {
        my_plugin->control_info.hMainWindow = control_info.hMainWindow;
        my_plugin->control_info.hinst = control_info.hinst;
        my_plugin->control_info.MemoryBswaped = control_info.MemoryBswaped;
        my_plugin->control_info.HEADER = control_info.HEADER;

        rom.crc1 = 0;
        rom.crc2 = 0;
        rom.name.clear();
        rom.country_code = 0;
        rom.version = 0;
        if (control_info.HEADER) {
            int swap = (control_info.MemoryBswaped ? 3 : 0);
            for (int i = 0; i < 4; i++) {
                rom.crc1 |= (control_info.HEADER[(0x10 + i) ^ swap] << ((i ^ 3) * 8));
                rom.crc2 |= (control_info.HEADER[(0x14 + i) ^ swap] << ((i ^ 3) * 8));
            }
            rom.name.reserve(20);
            for (int i = 0; i < 20; i++) {
                rom.name += control_info.HEADER[(0x20 + i) ^ swap];
            }
            rom.name.resize(rom.name.find_last_not_of(' ') + 1);
            rom.country_code = control_info.HEADER[0x3E ^ swap];
            rom.version = control_info.HEADER[0x3F ^ swap];
        }

        if (my_plugin->InitiateControllers0100) {
            my_plugin->InitiateControllers0100(my_plugin->control_info.hMainWindow, my_plugin->control_info.Controls);
            my_plugin->controllers_initiated = true;
        } else if (my_plugin->InitiateControllers0101) {
            my_plugin->InitiateControllers0101(my_plugin->control_info);
            my_plugin->controllers_initiated = true;
        }
    }
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

    assert(control_info.hMainWindow);

    rom_open = true;

    if (!my_plugin) {
        DllConfig(control_info.hMainWindow);
    }

    if (my_plugin) {
        my_client = make_shared<client>(make_shared<client_dialog>(this_dll, control_info.hMainWindow));
        my_client->set_name(my_settings->get_name());
        my_client->set_rom_info(rom);
        my_client->set_dst_controllers(control_info.Controls);
        my_client->load_public_server_list();

        my_plugin->RomOpen();
        my_client->set_src_controllers(my_plugin->control_info.Controls);
    }

    port_already_visited.fill(true);
}

EXPORT void CALL WM_KeyDown( WPARAM wParam, LPARAM lParam ) {
    load();

    if (my_plugin) {
        my_plugin->WM_KeyDown(wParam, lParam);
    }
}

EXPORT void CALL WM_KeyUp( WPARAM wParam, LPARAM lParam ) {
    load();

    if (my_plugin) {
        my_plugin->WM_KeyUp(wParam, lParam);
    }
}

#if defined(__cplusplus)
}
#endif
