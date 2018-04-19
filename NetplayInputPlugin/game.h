#pragma once

#include <vector>

#include "Controller 1.0.h"
#include "blocking_queue.h"
#include "client_server_common.h"
#include "client.h"
#include "client_dialog.h"

class game {
    public:
        game(HMODULE hmod);
        ~game();

        std::wstring get_name();
        uint8_t get_remote_count();
        std::vector<CONTROL> get_local_controllers();
        bool plugged_in(uint8_t index);

        void set_name(const std::wstring& name);
        void set_local_controllers(CONTROL controllers[MAX_PLAYERS]);
        void process_command(const std::wstring& command);
        void process_input(std::vector<BUTTONS>& input);
        void set_lag(uint8_t lag);
        void game_has_started();
        void set_netplay_controllers(CONTROL netplay_controllers[MAX_PLAYERS]);
        void update_netplay_controllers(const std::vector<CONTROL>& netplay_controllers);
        void set_player_start(uint8_t player_start);
        void set_player_count(uint8_t player_count);
        void set_user_name(uint32_t id, const std::wstring& name);
        void set_user_latency(uint32_t id, uint32_t latency);
        void remove_user(uint32_t id);
        void chat_received(uint32_t id, const std::wstring& message);
        void incoming_remote_input(const std::vector<BUTTONS>& input);
        void WM_KeyDown(WPARAM wParam, LPARAM lParam);
        void WM_KeyUp(WPARAM wParam, LPARAM lParam);
        void wait_for_game_to_start();
        void client_error();
        const std::map<uint32_t, std::wstring>& get_names() const;
        const std::map<uint32_t, uint32_t>& get_latencies() const;
    protected:
    private:
        boost::recursive_mutex mut;

        bool game_started;
        bool online;
        boost::condition_variable_any cond;
        int current_lag;
        long frame;
        bool golf;

        std::wstring name;
        std::map<uint32_t, std::wstring> names;
        std::map<uint32_t, uint32_t> latencies;
        uint8_t player_start;
        uint8_t player_count;
        uint8_t lag;

        CONTROL* netplay_controllers;
        std::vector<CONTROL> local_controllers;
        std::list<std::vector<BUTTONS>> remote_input;
        std::list<std::vector<BUTTONS>> local_input;
        blocking_queue<std::vector<BUTTONS>> queue;

        boost::shared_ptr<client_dialog> my_dialog;
        boost::shared_ptr<client> my_client;
        boost::shared_ptr<server> my_server;

        uint8_t get_total_count();
        void enqueue_if_ready();
};
