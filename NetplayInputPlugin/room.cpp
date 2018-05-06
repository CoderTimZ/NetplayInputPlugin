#include "stdafx.h"

#include "room.h"
#include "user.h"
#include "client_server_common.h"

using namespace std;
using namespace asio;

room::room(const string& id, server_ptr my_server) : id(id), my_server(my_server), started(false) {

}

room::~room() {

}

const string& room::get_id() const {
    return id;
}

void room::close() {
    for (auto& u : users) {
        u->close();
    }

    my_server->on_room_close(shared_from_this());
}

user_ptr room::get_user(uint32_t id) {
    auto it = find_if(begin(users), end(users), [&](user_ptr& u) { return u->get_id() == id; });
    return it == end(users) ? nullptr : *it;
}

int room::player_count() {
    int count = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (netplay_controllers[i].present) {
            count++;
        }
    }
    return count;
}

void room::on_user_join(user_ptr user) {
    if (started) {
        user->send_message(ERROR_MESSAGE, "Game is already in progress");
        user->close();
        return;
    }

    for (auto& u : users) {
        u->send_join(user->get_id(), user->get_name());
    }
    users.push_back(user);
    user->set_room(shared_from_this());
    for (auto& u : users) {
        user->send_join(u->get_id(), u->get_name());
    }
    user->send_ping(server::time());
    user->send_lag(lag);
    user->send_message(STATUS_MESSAGE, "The server set the lag to " + to_string(lag));
    
    update_controllers();
}

void room::on_user_quit(user_ptr user) {
    auto it = find_if(begin(users), end(users), [&](user_ptr& u) { return u == user; });
    if (it == end(users)) return;

    for (auto& u : users) {
        u->send_quit(user->get_id());
    }

    users.erase(it);

    if (started && user->is_player()) {
        close();
    }  else if (users.empty()) {
        close();
    } else {
        update_controllers();
    }
}


int room::get_total_latency() {
    int max_latency = -1, second_max_latency = -1;
    for (auto& u : users) {
        if (u->is_player()) {
            auto latency = u->get_median_latency();
            if (latency > second_max_latency) {
                second_max_latency = latency;
            }
            if (second_max_latency > max_latency) {
                swap(second_max_latency, max_latency);
            }
        }
    }
    return second_max_latency >= 0 ? max_latency + second_max_latency : 0;
}

int room::get_fps() {
    for (auto& u : users) {
        if (u->is_player()) {
            return u->get_fps();
        }
    }

    return -1;
}

void room::auto_adjust_lag() {
    int fps = get_fps();
    if (fps == -1) return;

    int latency = get_total_latency();

    int ideal_lag = min((int)ceil(latency * fps / 1000.0), 255);
    if (ideal_lag < lag) {
        send_lag(-1, lag - 1);
    } else if (ideal_lag > lag) {
        send_lag(-1, lag + 1);
    }
}

void room::on_tick() {
    send_latencies();

    for (auto& u : users) {
        u->send_ping(server::time());
    }

    if (autolag) {
        auto_adjust_lag();
    }
}

void room::on_game_start() {
    if (started) return;
    started = true;

    for (auto& u : users) {
        u->send_start_game();
    }
}

void room::update_controllers() {
    netplay_controllers.fill(controller());
    uint8_t netplay_port = 0;
    for (auto& u : users) {
        const auto& local_controllers = u->get_controllers();
        for (uint8_t local_port = 0; local_port < local_controllers.size(); local_port++) {
            if (local_controllers[local_port].present && netplay_port < netplay_controllers.size()) {
                netplay_controllers[netplay_port] = local_controllers[local_port];
                u->my_controller_map.insert(local_port, netplay_port);
                netplay_port++;
            } else {
                u->my_controller_map.insert(local_port, -1);
            }
        }
    }

    for (auto& u : users) {
        u->send_netplay_controllers(netplay_controllers);

        packet p;
        p << CONTROLLERS;
        p << u->get_id();
        for (auto& c : u->controllers) {
            p << c.plugin << c.present << c.raw_data;
        }
        for (auto l2n : u->my_controller_map.local_to_netplay) {
            p << l2n;
        }

        for (auto& u2 : users) {
            u2->send(p);
        }
    }
}

void room::send_status(const string& message) {
    for (auto& u : users) {
        u->send_message(STATUS_MESSAGE, message);
    }
}

void room::send_error(const string& message) {
    for (auto& u : users) {
        u->send_message(ERROR_MESSAGE, message);
    }
}

void room::send_lag(int32_t id, uint8_t lag) {
    this->lag = lag;

    string message = (id == -1 ? "The server" : get_user(id)->get_name()) + " set the lag to " + to_string(lag);

    int fps = get_fps();
    if (fps > 0) {
        int latency = lag * 1000 / fps;
        message += " (" + to_string(latency) + " ms)";
    }

    for (auto& u : users) {
        if (u->get_id() == id) continue;
        u->send_lag(lag);
        u->send_message(STATUS_MESSAGE, message);
    }
}

void room::send_latencies() {
    packet p;
    p << LATENCY;
    for (auto& u : users) {
        p << u->get_id() << u->get_latency();
    }
    for (auto& u : users) {
        u->send(p);
    }
}
