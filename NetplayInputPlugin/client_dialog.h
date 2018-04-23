#pragma once

#include <windows.h>
#include <string>
#include <map>
#include <functional>
#include <thread>
#include <future>

class game;

class client_dialog {
    public:
        client_dialog(HMODULE hmod, HWND main_window);
        ~client_dialog();
        void set_command_handler(std::function<void(std::wstring)> command_handler);
        void status(const std::wstring& text);
        void error(const std::wstring& text);
        void chat(const std::wstring& name, const std::wstring& message);
        void update_user_list(const std::map<uint32_t, std::wstring>& names, const std::map<uint32_t, uint32_t>& pings);
    protected:
    private:
        HMODULE hmod;
        HWND main_window;
        std::function<void(std::wstring)> command_handler;
        HMODULE h_rich;
        HWND hwndDlg;
        std::thread thread;
        std::promise<bool> initialized;

        void gui_thread();
        bool scroll_at_bottom();
        void scroll_to_bottom();
        void select_end();
        void insert_text(const std::wstring& text);
        void append_timestamp();
        void alert_user();

        static INT_PTR CALLBACK DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
};
