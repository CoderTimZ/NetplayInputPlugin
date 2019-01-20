#include "stdafx.h"

#include "input_plugin.h"
#include "id_variable.h"
#include "util.h"

using namespace std;

input_plugin::input_plugin(string path) {
    control_info.Controls = controls;

    dll = LoadLibrary(utf8_to_wstring(path).c_str());

    if (!dll) {
        DWORD error_code = GetLastError();
        throw runtime_error("Could not load input plugin dll: " + to_string(error_code));
    }

    if (GetProcAddress(dll, _IDENTIFYING_VARIABLE_NAME)) {
        FreeLibrary(dll);
        throw runtime_error("Cannot load another Netplay plugin");
    }

    GetDllInfo = (void(*)(PLUGIN_INFO * PluginInfo)) GetProcAddress(dll, "GetDllInfo");

    PluginInfo.Type = (WORD)(-1);
    if (GetDllInfo) {
        GetDllInfo(&PluginInfo);
    }

    if (PluginInfo.Type != PLUGIN_TYPE_CONTROLLER) {
        FreeLibrary(dll);
        throw runtime_error("Plugin is not an input plugin");
    }

    if (PluginInfo.Version != 0x0100 && PluginInfo.Version != 0x0101) {
        FreeLibrary(dll);
        throw runtime_error("Plugin must be version 1.0 or 1.1");
    }

    if (PluginInfo.Version == 0x0100) {
        InitiateControllers0100 = (void(*)(HWND hMainWindow, CONTROL Controls[4])) GetProcAddress(dll, "InitiateControllers");
    } else if (PluginInfo.Version == 0x0101) {
        InitiateControllers0101 = (void(*)(CONTROL_INFO Controls)) GetProcAddress(dll, "InitiateControllers");
    }
    CloseDLL                 = (void(*)(void))                                   GetProcAddress(dll, "CloseDLL");
    ControllerCommand        = (void(*)(int, BYTE *))                            GetProcAddress(dll, "ControllerCommand");
    DllAbout                 = (void(*)(HWND hParent))                           GetProcAddress(dll, "DllAbout");
    DllConfig                = (void(*)(HWND hParent))                           GetProcAddress(dll, "DllConfig");
    DllTest                  = (void(*)(HWND hParent))                           GetProcAddress(dll, "DllTest");
    GetKeys                  = (void(*)(int Control, BUTTONS * Keys))            GetProcAddress(dll, "GetKeys");
    ReadController           = (void(*)(int Control, BYTE * Command))            GetProcAddress(dll, "ReadController");
    RomClosed                = (void(*)(void))                                   GetProcAddress(dll, "RomClosed");
    RomOpen                  = (void(*)(void))                                   GetProcAddress(dll, "RomOpen");
    WM_KeyDown               = (void(*)(WPARAM wParam, LPARAM lParam))           GetProcAddress(dll, "WM_KeyDown");
    WM_KeyUp                 = (void(*)(WPARAM wParam, LPARAM lParam))           GetProcAddress(dll, "WM_KeyUp");

    if (!InitiateControllers0100 && !InitiateControllers0101) {
        FreeLibrary(dll);
        throw runtime_error("Required function 'InitiateControllers' is missing from dll");
    }

    if (!CloseDLL) {
        FreeLibrary(dll);
        throw runtime_error("Required function 'CloseDLL' is missing from dll");
    }

    if (!DllConfig) {
        FreeLibrary(dll);
        throw runtime_error("Required function 'DllConfig' is missing from dll");
    }

    if (!GetKeys) {
        FreeLibrary(dll);
        throw runtime_error("Required function 'GetKeys' is missing from dll");
    }

    if (!RomClosed) {
        FreeLibrary(dll);
        throw runtime_error("Required function 'RomClosed' is missing from dll");
    }

    if (!RomOpen) {
        FreeLibrary(dll);
        throw runtime_error("Required function 'RomOpen' is missing from dll");
    }

    if (!WM_KeyDown) {
        FreeLibrary(dll);
        throw runtime_error("Required function 'WM_KeyDown' is missing from dll");
    }

    if (!WM_KeyUp) {
        FreeLibrary(dll);
        throw runtime_error("Required function 'WM_KeyUp' is missing from dll");
    }
}

input_plugin::~input_plugin() {
    CloseDLL();
    FreeLibrary(dll);
}
