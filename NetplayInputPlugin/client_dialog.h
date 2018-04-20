#pragma once

#include <windows.h>
#include <string>
#include <map>
#include <boost/thread.hpp>

class game;

class client_dialog {
    public:
        client_dialog(HMODULE hmod, game& my_game);
        ~client_dialog();
        void status(const std::wstring& text);
        void error(const std::wstring& text);
        void chat(const std::wstring& name, const std::wstring& message);
        void update_user_list(const std::map<uint32_t, std::wstring>& names, const std::map<uint32_t, uint32_t>& pings);
    protected:
    private:
        HMODULE hmod;
        game& my_game;
        HMODULE h_rich;
        HWND hwndDlg;
        boost::barrier initialized;
        boost::thread thread;
        boost::mutex mut;

        void gui_thread();
        bool scroll_at_bottom();
        void scroll_to_bottom();
        void select_end();
        void insert_text(const std::wstring& text);
        void append_timestamp();
        void alert_user();

        static INT_PTR CALLBACK DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
};
