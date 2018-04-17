#pragma once

#include "Controller 1.0.h"

#include <windows.h>
#include <string>

class input_plugin {
    public:
        input_plugin(std::wstring path);
        ~input_plugin();

        HMODULE dll;
        PLUGIN_INFO PluginInfo;
        CONTROL controls[4];

        void (*CloseDLL)               (void);
        void (*ControllerCommand)      (int Control, BYTE * Command);
        void (*DllAbout)               (HWND hParent);
        void (*DllConfig)              (HWND hParent);
        void (*DllTest)                (HWND hParent);
        void (*GetDllInfo)             (PLUGIN_INFO * PluginInfo);
        void (*GetKeys)                (int Control, BUTTONS * Keys);
        void (*InitiateControllers0100)(HWND hMainWindow, CONTROL Controls[4]);
        // void (*InitiateControllers0101)(CONTROL_INFO * ControlInfo);
        void (*ReadController)         (int Control, BYTE * Command);
        void (*RomClosed)              (void);
        void (*RomOpen)                (void);
        void (*WM_KeyDown)             (WPARAM wParam, LPARAM lParam);
        void (*WM_KeyUp)               (WPARAM wParam, LPARAM lParam);
    protected:
    private:
        input_plugin();
};
