#pragma once

#include "stdafx.h"

#include "user.h"

class game;

class client_dialog {
    public:
        client_dialog(HMODULE hmod, HWND main_window);
        ~client_dialog();
        void set_message_handler(std::function<void(std::string)> message_handler);
        void set_close_handler(std::function<void(void)> close_handler);
        void status(const std::string& text);
        void error(const std::string& text);
        void chat(const std::string& name, const std::string& message);
        void update_user_list(const std::map<uint32_t, user>& users);
        void set_minimize_on_close(bool minimize_on_close);
    protected:
    private:
        HMODULE hmod;
        HWND main_window;
        std::function<void(std::string)> message_handler;
        std::function<void(void)> close_handler;
        HMODULE h_rich;
        HWND hwndDlg;
        std::thread thread;
        std::promise<bool> initialized;
        bool minimize_on_close = false;
        std::mutex mut;
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
