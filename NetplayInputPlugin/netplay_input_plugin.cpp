#include "Controller 1.0.h"
#include "id_variable.h"
#include "plugin_dialog.h"
#include "settings.h"
#include "input_plugin.h"
#include "game.h"

#include <boost/shared_ptr.hpp>
#include <string>
#include <cassert>

#ifdef DEBUG
#include <fcntl.h>
#include <cstdio>
#endif

using namespace std;

#if defined(__cplusplus)
extern "C" {
#endif

static bool loaded = false;
static bool rom_open = false;
static HMODULE this_dll = NULL;
static HWND main_window = NULL;
static CONTROL* netplay_controllers;
static boost::shared_ptr<settings> my_settings;
static boost::shared_ptr<input_plugin> my_plugin;
static boost::shared_ptr<game> my_game;
static wstring my_location;
static bool control_visited[MAX_PLAYERS];

#ifdef DEBUG
static bool create_console() {
    if (AllocConsole()) {
        HANDLE handle_out = GetStdHandle(STD_OUTPUT_HANDLE);
        int hCrt = _open_osfhandle((long) handle_out, _O_TEXT);
        FILE* hf_out = _fdopen(hCrt, "w");
        setvbuf(hf_out, NULL, _IONBF, 1);
        *stdout = *hf_out;

        HANDLE handle_in = GetStdHandle(STD_INPUT_HANDLE);
        hCrt = _open_osfhandle((long) handle_in, _O_TEXT);
        FILE* hf_in = _fdopen(hCrt, "r");
        setvbuf(hf_in, NULL, _IONBF, 128);
        *stdin = *hf_in;

        printf("WARNING: DO NOT CLOSE THIS WINDOW DIRECTLY!\n");

        return true;
    } else {
        return false;
    }
}
#endif

void set_visited(bool b) {
    for (int i = 0; i < 4; i++) {
        control_visited[i] = b;
    }
}

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

    #ifdef DEBUG
    create_console();
    #endif

    SetErrorMode(SEM_FAILCRITICALERRORS);

    wchar_t my_location_array[MAX_PATH];
    GetModuleFileName(this_dll, my_location_array, MAX_PATH);
    wcsrchr(my_location_array, L'\\')[1] = 0;
    my_location = my_location_array;

    my_settings = boost::shared_ptr<settings>(new settings(my_location + L"netplay_input_plugin.ini"));

    try {
        my_plugin = boost::shared_ptr<input_plugin>(new input_plugin(my_location + my_settings->get_plugin_dll()));
    } catch(const exception&) {
        my_plugin = boost::shared_ptr<input_plugin>();

        #ifdef DEBUG
        printf("Error: %s\n", e.what());
        #endif
    }

    loaded = true;
}

EXPORT int _IDENTIFYING_VARIABLE = 0;

EXPORT void CALL CloseDLL (void) {
    #ifdef DEBUG
    printf("CloseDLL.\n");
    #endif

    if (my_game != NULL) {
        my_settings->set_name(my_game->get_name());
    }

    my_settings->save();

    my_game.reset();
    my_settings.reset();
    my_plugin.reset();

    loaded = false;

    #ifdef DEBUG
    printf("CloseDLL Done.\n");
    #endif
}

EXPORT void CALL ControllerCommand( int Control, BYTE * Command) {
    load();
}

EXPORT void CALL DllAbout ( HWND hParent ) {
    #ifdef DEBUG
    printf("DllAbout.\n");
    #endif

    load();

    MessageBox(hParent, L"NetPlay Input Plugin\n\nVersion: 0.20\n\nAuthor: AQZ", L"About", MB_OK | MB_ICONINFORMATION);

    #ifdef DEBUG
    printf("DllAbout Done.\n");
    #endif
}

EXPORT void CALL DllConfig ( HWND hParent ) {
    #ifdef DEBUG
    printf("DllConfig.\n");
    #endif

    load();

    if (rom_open) {
        if (my_plugin != NULL) {
            my_plugin->DllConfig(hParent);

            if (my_game != NULL) {
                my_game->set_local_controllers(my_plugin->controls);
            }
        }
    } else {
        my_plugin = boost::shared_ptr<input_plugin>();

        assert(main_window != NULL);

        plugin_dialog dialog(this_dll, hParent, my_location, my_settings->get_plugin_dll(), main_window);

        if (dialog.ok_clicked()) {
            my_settings->set_plugin_dll(dialog.get_plugin_dll());
        }

        try {
            my_plugin = boost::shared_ptr<input_plugin>(new input_plugin(my_location + my_settings->get_plugin_dll()));
            my_plugin->InitiateControllers0100(main_window, my_plugin->controls);
        }
        catch(const exception&){}
    }

    #ifdef DEBUG
    printf("DllConfig Done.\n");
    #endif
}

EXPORT void CALL DllTest ( HWND hParent ) {
    #ifdef DEBUG
    printf("DllTest.\n");
    #endif

    load();

    if (my_plugin == NULL) {
        MessageBox(NULL, L"No input plugin has been selected.", L"Warning", MB_OK | MB_ICONWARNING);
    }

    #ifdef DEBUG
    printf("DllTest Done.\n");
    #endif
}

EXPORT void CALL GetDllInfo ( PLUGIN_INFO * PluginInfo ) {
    #ifdef DEBUG
    printf("GetDllInfo.\n");
    #endif

    load();

    PluginInfo->Version = 0x0100;
    PluginInfo->Type = PLUGIN_TYPE_CONTROLLER;

    strncpy_s(PluginInfo->Name, PLUGIN_NAME_LENGTH, "AQZ NetPlay v0.20", PLUGIN_NAME_LENGTH);

    #ifdef DEBUG
    printf("GetDllInfo Done.\n");
    #endif
}

EXPORT void CALL GetKeys(int Control, BUTTONS * Keys ) {
    static vector<BUTTONS> input(4);

    Keys->Value = 0;

    load();

    if (control_visited[Control]) {
        set_visited(false);

        if (my_plugin == NULL || my_game == NULL) {
            for (int i = 0; i < MAX_PLAYERS; i++) {
                input[i].Value = 0;
            }
        } else {
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (my_game->plugged_in(i)) {
                    my_plugin->GetKeys(i, &input[i]);
                } else {
                    input[i].Value = 0;
                }
            }

            my_game->process_input(input);
        }
    }

    *Keys = input[Control];
    control_visited[Control] = true;
}

EXPORT void CALL InitiateControllers (HWND hMainWindow, CONTROL Controls[4]) {
    #ifdef DEBUG
    printf("InitiateControllers.\n");
    #endif

    load();

    netplay_controllers = Controls;

    if (my_plugin != NULL) {
        my_plugin->InitiateControllers0100(hMainWindow, my_plugin->controls);
    }

    main_window = hMainWindow;

    #ifdef DEBUG
    printf("InitiateControllers Done.\n");
    #endif
}

EXPORT void CALL ReadController ( int Control, BYTE * Command ) {
    load();
}

EXPORT void CALL RomClosed (void) {
    #ifdef DEBUG
    printf("RomClosed.\n");
    #endif

    load();

    if (my_game != NULL) {
        my_settings->set_name(my_game->get_name());
    }

    my_game.reset();

    if (my_plugin != NULL) {
        my_plugin->RomClosed();
    }

    rom_open = false;

    #ifdef DEBUG
    printf("RomClosed Done.\n");
    #endif
}

EXPORT void CALL RomOpen (void) {
    #ifdef DEBUG
    printf("RomOpen.\n");
    #endif

    load();

    assert(main_window != NULL);

    rom_open = true;

    if (my_plugin == NULL) {
        DllConfig(main_window);
    }

    if (my_plugin != NULL) {
        my_game = boost::shared_ptr<game>(new game(this_dll));
        my_game->set_name(my_settings->get_name());
        my_game->set_netplay_controllers(netplay_controllers);

        my_plugin->RomOpen();
        my_game->set_local_controllers(my_plugin->controls);

        my_game->wait_for_game_to_start();
    }

    set_visited(true);

    #ifdef DEBUG
    printf("RomOpen Done.\n");
    #endif
}

EXPORT void CALL WM_KeyDown( WPARAM wParam, LPARAM lParam ) {
    #ifdef DEBUG
    printf("WM_KeyDown.\n");
    #endif

    load();

    if (my_game != NULL) {
        my_game->WM_KeyDown(wParam, lParam);
    }

    if (my_plugin != NULL) {
        my_plugin->WM_KeyDown(wParam, lParam);
    }

    #ifdef DEBUG
    printf("WM_KeyDown Done.\n");
    #endif
}

EXPORT void CALL WM_KeyUp( WPARAM wParam, LPARAM lParam ) {
    #ifdef DEBUG
    printf("WM_KeyUp.\n");
    #endif

    load();

    if (my_game != NULL) {
        my_game->WM_KeyUp(wParam, lParam);
    }

    if (my_plugin != NULL) {
        my_plugin->WM_KeyUp(wParam, lParam);
    }

    #ifdef DEBUG
    printf("WM_KeyUp Done.\n");
    #endif
}

#if defined(__cplusplus)
}
#endif
