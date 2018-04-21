#include <boost/lexical_cast.hpp>
#include <boost/numeric/conversion/cast.hpp>
#include <boost/tokenizer.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include "game.h"
#include "client_dialog.h"
#include "util.h"
#include "client.h"

using namespace std;

game::game(HMODULE hmod, HWND main_window) {
    my_dialog = std::shared_ptr<client_dialog>(new client_dialog(hmod, *this, main_window));
    game_started = false;
    online = false;
    current_lag = 0;
    lag = DEFAULT_LAG;
    frame = 0;
    golf = false;

    my_dialog->status(L"List of available commands:\n"
                      L"* /name <name>           -- set your name\n"
                      L"* /server <port>         -- host a server\n"
                      L"* /connect <host> <port> -- connect to a server\n"
                      L"* /start                 -- start the game\n"
                      L"* /lag <lag>             -- set the netplay input lag\n"
                      L"* /golf                  -- toggle golf mode on and off");

    my_client = std::shared_ptr<client>(new client(*my_dialog, *this));
    my_server = std::shared_ptr<server>(new server(*my_dialog, lag));
}

game::~game() {
    my_client.reset();
    my_server.reset();

    cond.notify_all();
}

wstring game::get_name() {
    boost::recursive_mutex::scoped_lock lock(mut);

    return name;
}

uint8_t game::get_remote_count() {
    boost::recursive_mutex::scoped_lock lock(mut);

    return get_total_count() - player_count;
}

vector<CONTROL> game::get_local_controllers() {
    boost::recursive_mutex::scoped_lock lock(mut);

    return local_controllers;
}

bool game::plugged_in(uint8_t index) {
    boost::recursive_mutex::scoped_lock lock(mut);

    return local_controllers[index].Present;
}

void game::set_name(const wstring& name) {
    boost::recursive_mutex::scoped_lock lock(mut);

    this->name = name;
    if (name.size() > 255) {
        this->name.resize(255);
    }

    my_dialog->status(L"Name set to " + this->name + L".");
}

void game::set_local_controllers(CONTROL controllers[MAX_PLAYERS]) {
    int count = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        controllers[i].RawData = FALSE; // Disallow raw data

        if (controllers[i].Present) {
            count++;
        }
    }

    boost::recursive_mutex::scoped_lock lock(mut);

    if (game_started) {
        return;
    }

    local_controllers.assign(controllers, controllers + MAX_PLAYERS);

    my_client->io_s.post(boost::bind(&client::send_controllers, my_client, local_controllers));

    my_dialog->status(L"Requested local players: " + boost::lexical_cast<wstring>(count));
}

void game::process_command(const wstring& command) {
    boost::recursive_mutex::scoped_lock lock(mut);

    if (command.substr(0, 1) == L"/") {
        boost::char_separator<wchar_t> sep(L" \t\n\r");
        boost::tokenizer<boost::char_separator<wchar_t>, wstring::const_iterator, wstring> tokens(command, sep);
        auto it = tokens.begin();

        if (*it == L"/name") {
            if (++it != tokens.end()) {
                set_name(*it);
                my_client->io_s.post(boost::bind(&client::send_name, my_client, *it));
            } else {
                my_dialog->error(L"Missing parameter.");
            }
        } else if (*it == L"/server") {
            if (game_started) {
                my_dialog->error(L"Game has already started.");
                return;
            }

            if (++it == tokens.end()) {
                my_dialog->error(L"Missing parameter.");
                return;
            }

            try {
                uint16_t port = boost::lexical_cast<uint16_t>(*it);

                my_client = std::shared_ptr<client>(new client(*my_dialog, *this));
                my_server = std::shared_ptr<server>(new server(*my_dialog, lag));

                port = my_server->start(port);

                if (port) {
                    my_client->connect(L"127.0.0.1", port);
                }
            } catch(const exception& e) {
                my_dialog->error(L"\"" + widen(e.what()) + L"\"");
            }
        } else if (*it == L"/connect") {
            if (game_started) {
                my_dialog->error(L"Game has already started.");
                return;
            }

            if (++it == tokens.end()) {
                my_dialog->error(L"Missing parameter.");
                return;
            }

            wstring host = *it;

            if (++it == tokens.end()) {
                my_dialog->error(L"Missing parameter.");
                return;
            }

            try {
                uint16_t port = boost::lexical_cast<uint16_t>(*it);

                my_client = std::shared_ptr<client>(new client(*my_dialog, *this));
                my_server = std::shared_ptr<server>(new server(*my_dialog, lag));

                my_client->connect(host, port);
            } catch (const exception& e) {
                my_dialog->error(L"\"" + widen(e.what()) + L"\"");
            }
        } else if (*it == L"/start") {
            if (game_started) {
                my_dialog->error(L"Game has already started.");
                return;
            }

            my_client->io_s.post(boost::bind(&client::send_start_game, my_client));
        } else if (*it == L"/lag") {
            if (++it != tokens.end()) {
                try {
                    uint8_t lag = boost::numeric_cast<uint8_t>(boost::lexical_cast<uint32_t>(*it));

                    set_lag(lag);

                    my_client->io_s.post(boost::bind(&client::send_lag, my_client, lag));
                } catch(const exception& e) {
                    my_dialog->error(L"\"" + widen(e.what()) + L"\"");
                }
            } else {
                my_dialog->error(L"Missing parameter.");
            }
        }
        else if (*it == L"/my_lag") {
            if (++it != tokens.end()) {
                try {
                    uint8_t lag = boost::numeric_cast<uint8_t>(boost::lexical_cast<uint32_t>(*it));
                    set_lag(lag);
                } catch (const exception& e) {
                    my_dialog->error(L"\"" + widen(e.what()) + L"\"");
                }
            } else {
                my_dialog->error(L"Missing parameter.");
            }
        } else if (*it == L"/your_lag") {
            if (++it != tokens.end()) {
                try {
                    uint8_t lag = boost::numeric_cast<uint8_t>(boost::lexical_cast<uint32_t>(*it));
                    my_client->io_s.post(boost::bind(&client::send_lag, my_client, lag));
                } catch (const exception& e) {
                    my_dialog->error(L"\"" + widen(e.what()) + L"\"");
                }
            } else {
                my_dialog->error(L"Missing parameter.");
            }
        } else if (*it == L"/golf") {
            golf = !golf;
            
            if (golf) {
                my_dialog->status(L"Golf mode is turned ON.");
            } else {
                my_dialog->status(L"Golf mode is turned OFF.");
            }
        } else {
            my_dialog->error(L"Unknown command '" + *it + L"'.");
        }
    } else {
        my_dialog->chat(name, command);

        my_client->io_s.post(boost::bind(&client::send_chat, my_client, command));
    }
}

void game::process_input(vector<BUTTONS>& input) {
    {
        boost::recursive_mutex::scoped_lock lock(mut);

        if (player_count > 0) {
            if (golf) {
                for (int i = 0; lag != 0 && i < input.size(); i++) {
                    if (local_controllers[i].Present && input[i].Z_TRIG) {
                        my_client->io_s.post(boost::bind(&client::send_lag, my_client, lag));
                        set_lag(0);
                    }
                }
            }

            vector<BUTTONS> input_prime;
            for (int i = 0; i < input.size() && input_prime.size() < player_count; i++) {
                if (local_controllers[i].Present) {
                    input_prime.push_back(input[i]);
                }
            }

            current_lag--;

            while (current_lag < lag) {
                my_client->io_s.post(boost::bind(&client::send_input, my_client, input_prime));
                local_input.push_back(input_prime);
                current_lag++;
            }
        }

        enqueue_if_ready();

        frame++;
    }

    vector<BUTTONS> processed = queue.pop();

    for (int i = 0; i < processed.size(); i++) {
        input[i] = processed[i];
    }
}

void game::set_lag(uint8_t lag) {
    boost::recursive_mutex::scoped_lock lock(mut);

    this->lag = lag;

    my_dialog->status(L"Lag set to " + boost::lexical_cast<wstring>((int) lag) + L".");
}

void game::game_has_started() {
    boost::recursive_mutex::scoped_lock lock(mut);

    if (game_started) {
        return;
    }

    game_started = true;
    online = true;
    cond.notify_all();
    my_dialog->status(L"Game has started!");
}

void game::client_error() {
    boost::recursive_mutex::scoped_lock lock(mut);

    online = false;
    cond.notify_all();
    enqueue_if_ready();

    names.clear();
    latencies.clear();
    my_dialog->update_user_list(names, latencies);
}

void game::set_netplay_controllers(CONTROL netplay_controllers[MAX_PLAYERS]) {
    boost::recursive_mutex::scoped_lock lock(mut);

    this->netplay_controllers = netplay_controllers;
}

void game::update_netplay_controllers(const array<CONTROL, MAX_PLAYERS>& netplay_controllers) {
    boost::recursive_mutex::scoped_lock lock(mut);

    for (int i = 0; i < netplay_controllers.size(); i++) {
        this->netplay_controllers[i] = netplay_controllers[i];
    }
}

void game::set_player_index(uint8_t player_index) {
    boost::recursive_mutex::scoped_lock lock(mut);

    this->player_index = player_index;
}

void game::set_player_count(uint8_t player_count) {
    boost::recursive_mutex::scoped_lock lock(mut);

    this->player_count = player_count;

    my_dialog->status(L"Local players: " + boost::lexical_cast<wstring>((int) this->player_count));
}

void game::set_user_name(uint32_t id, const wstring& name) {
    boost::recursive_mutex::scoped_lock lock(mut);

    if (names.find(id) == names.end()) {
        my_dialog->status(name + L" has joined.");
    } else {
        my_dialog->status(names[id] + L" is now " + name + L".");
    }

    names[id] = name;

    my_dialog->update_user_list(names, latencies);
}

void game::set_user_latency(uint32_t id, uint32_t latency) {
    boost::recursive_mutex::scoped_lock lock(mut);
    latencies[id] = latency;
}

void game::remove_user(uint32_t id) {
    boost::recursive_mutex::scoped_lock lock(mut);

    my_dialog->status(names[id] + L" has left.");
    names.erase(id);
    latencies.erase(id);

    my_dialog->update_user_list(names, latencies);
}

void game::chat_received(uint32_t id, const wstring& message) {
    boost::recursive_mutex::scoped_lock lock(mut);

    my_dialog->chat(names[id], message);
}

void game::incoming_remote_input(const vector<BUTTONS>& input) {
    boost::recursive_mutex::scoped_lock lock(mut);

    remote_input.push_back(input);

    enqueue_if_ready();
}

void game::WM_KeyDown(WPARAM wParam, LPARAM lParam) { }

void game::WM_KeyUp(WPARAM wParam, LPARAM lParam) { }

void game::wait_for_game_to_start() {
    boost::recursive_mutex::scoped_lock lock(mut);

    while (!game_started) {
        cond.wait(lock);
    }
}

uint8_t game::get_total_count() {
    uint8_t count = 0;

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (netplay_controllers[i].Present) {
            count++;
        }
    }

    return count;
}

void game::enqueue_if_ready() {
    if (player_count > 0 && local_input.empty()) {
        return;
    }

    if (online && get_remote_count() > 0 && remote_input.empty()) {
        return;
    }

    vector<BUTTONS> input(get_total_count());
    for (int i = 0; i < input.size(); i++) {
        input[i].Value = 0;
    }

    if (!remote_input.empty()) {
        for (int i = 0; i < remote_input.front().size(); i++) {
            if (i < player_index) {
                input[i] = remote_input.front()[i];
            } else {
                input[i + player_count] = remote_input.front()[i];
            }
        }

        remote_input.pop_front();
    }

    if (!local_input.empty()) {
        for (int i = 0; i < local_input.front().size(); i++) {
            input[i + player_index] = local_input.front()[i];
        }

        local_input.pop_front();
    }

    queue.push(input);
}

const std::map<uint32_t, std::wstring>& game::get_names() const {
    return names;
}

const std::map<uint32_t, uint32_t>& game::get_latencies() const {
    return latencies;
}