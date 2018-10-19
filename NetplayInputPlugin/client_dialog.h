#pragma once

#include "stdafx.h"

#include "user_data.h"

class game;

class client_dialog {
    public:
        client_dialog(HMODULE hmod, HWND main_window);
        ~client_dialog();
        void set_message_handler(std::function<void(std::string)> message_handler);
        void set_close_handler(std::function<void(void)> close_handler);
        void status(const std::string& text);
        void error(const std::string& text);
        void message(const std::string& name, const std::string& message);
        void update_user_list(const std::map<uint32_t, user_data>& users);
        void update_server_list(const std::map<std::string, double>& servers);
        void set_lag(uint8_t lag);
        void minimize();
        void destroy();
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

        void gui_thread();
        bool scroll_at_bottom();
        void scroll_to_bottom();
        void select_end();
        void insert_text(const std::string& text);
        void append_timestamp();
        void alert_user(bool force);

        static INT_PTR CALLBACK DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
};
