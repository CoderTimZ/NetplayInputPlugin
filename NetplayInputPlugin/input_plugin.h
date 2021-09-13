#pragma once

#include "stdafx.h"

#include "Controller 1.1.h"

class input_plugin {
    public:
        input_plugin(std::string path);
        ~input_plugin();

        HMODULE dll = NULL;
        PLUGIN_INFO info;
        CONTROL controls[4];
        bool controllers_initiated = false;

        bool initiate_controllers(CONTROL_INFO info);

        void (*CloseDLL)               (void)                                  = NULL;
        void (*ControllerCommand)      (int Control, BYTE * Command)           = NULL;
        void (*DllAbout)               (HWND hParent)                          = NULL;
        void (*DllConfig)              (HWND hParent)                          = NULL;
        void (*DllTest)                (HWND hParent)                          = NULL;
        void (*GetDllInfo)             (PLUGIN_INFO * PluginInfo)              = NULL;
        void (*GetKeys)                (int Control, BUTTONS * Keys)           = NULL;
        void (*InitiateControllers0100)(HWND hMainWindow, CONTROL Controls[4]) = NULL;
        void (*InitiateControllers0101)(CONTROL_INFO ControlInfo)              = NULL;
        void (*ReadController)         (int Control, BYTE * Command)           = NULL;
        void (*RomClosed)              (void)                                  = NULL;
        void (*RomOpen)                (void)                                  = NULL;
        void (*WM_KeyDown)             (WPARAM wParam, LPARAM lParam)          = NULL;
        void (*WM_KeyUp)               (WPARAM wParam, LPARAM lParam)          = NULL;
    protected:
    private:
        input_plugin();
};
