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
        void set_command_handler(std::function<void(std::string)> command_handler);
        void status(const std::string& text);
        void error(const std::string& text);
        void chat(const std::string& name, const std::string& message);
        void update_user_list(const std::map<uint32_t, std::string>& names, const std::map<uint32_t, uint32_t>& pings);
    protected:
    private:
        HMODULE hmod;
        HWND main_window;
        std::function<void(std::string)> command_handler;
        HMODULE h_rich;
        HWND hwndDlg;
        std::thread thread;
        std::promise<bool> initialized;

        void gui_thread();
        bool scroll_at_bottom();
        void scroll_to_bottom();
        void select_end();
        void insert_text(const std::string& text);
        void append_timestamp();
        void alert_user(bool force);

        static INT_PTR CALLBACK DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
};
