#pragma once

#include "stdafx.h"

#include "input_plugin.h"

class plugin_dialog {
    public:
        plugin_dialog(HMODULE this_dll, HWND parent, const std::string& search_location, const std::string& plugin_dll, CONTROL_INFO control_info);
        ~plugin_dialog();
        bool ok_clicked();
        const std::string& get_plugin_dll();
    protected:
    private:
        plugin_dialog();
        void populate_plugin_map();
        void populate_combo(HWND combo);
        void combo_selection_changed(HWND hwndDlg);

        std::string search_location;
        std::map<std::string, std::string> plugins;
        bool ok;
        std::string plugin_dll;
        CONTROL_INFO control_info;
        std::shared_ptr<input_plugin> plugin;

        static INT_PTR CALLBACK DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
        static plugin_dialog* dialog;
};
