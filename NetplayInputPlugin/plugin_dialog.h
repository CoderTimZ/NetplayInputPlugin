#pragma once

#include "input_plugin.h"

#include <windows.h>
#include <map>
#include <string>

class plugin_dialog {
    public:
        plugin_dialog(HMODULE this_dll, HWND parent, const std::wstring& search_location, const std::wstring& plugin_dll, HWND main_window);
        ~plugin_dialog();
        bool ok_clicked();
        const std::wstring& get_plugin_dll();
    protected:
    private:
        plugin_dialog();
        void populate_plugin_map();
        void populate_combo(HWND combo);
        void combo_selection_changed(HWND hwndDlg);

        std::wstring search_location;
        std::map<std::string, std::wstring> plugins;
        bool ok;
        std::wstring plugin_dll;
        HWND main_window;
        input_plugin* plugin;

        static INT_PTR CALLBACK DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
        static plugin_dialog* dialog;
};
