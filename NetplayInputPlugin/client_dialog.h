#pragma once

#include "stdafx.h"

enum SERVER_STATUS {
    SERVER_STATUS_PENDING          = -1,
    SERVER_STATUS_ERROR            = -2,
    SERVER_STATUS_VERSION_MISMATCH = -3,
    SERVER_STATUS_OUTDATED_CLIENT  = -4,
    SERVER_STATUS_OUTDATED_SERVER  = -5
};

class client_dialog {
    public:
        client_dialog(HMODULE hmod, HWND main_window);
        ~client_dialog();
        void set_message_handler(std::function<void(std::string)> message_handler);
        void set_close_handler(std::function<void(void)> close_handler);
        void info(const std::string& text);
        void error(const std::string& text);
        void message(const std::string& name, const std::string& message);
        void update_user_list(const std::vector<std::string>& lines);
        void update_server_list(const std::map<std::string, double>& servers);
        //void set_lag(uint8_t lag);
        //void set_latency(double latency);
        void minimize();
        void destroy();
        HWND get_emulator_window();
        bool is_emulator_project64z();
    protected:
    private:
        HMODULE hmod;
        HWND main_window;
        std::function<void(std::string)> message_handler;
        std::function<void(void)> close_handler;
        HWND hwndDlg;
        std::thread thread;
        std::promise<bool> initialized;
        std::mutex mut;
        std::vector<std::string> server_list;
        std::string original_title;
        bool destroyed = false;
        bool project64z = false;

        void gui_thread();
        bool scroll_at_bottom();
        void scroll_to_bottom();
        void select_end();
        void insert_text(const std::string& text);
        void append_timestamp();
        void alert_user(bool force);

        static INT_PTR CALLBACK DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
};
