#include "stdafx.h"

#include "room.h"
#include "user.h"
#include "common.h"

using namespace std;
using namespace asio;

room::room(const string& id, server* server, rom_info rom)
    : id(id), my_server(server), rom(rom) { }

const string& room::get_id() const {
    return id;
}

void room::close() {
    for (auto& u : user_list) {
        u->close();
    }
    my_server->on_room_close(this);
}

void room::on_user_join(user* user) {
    if (started) {
        user->send_error("Game is already in progress");
        user->close();
        return;
    }

    if (rom && user->info.rom && rom != user->info.rom) {
        user->send_error(rom.to_string() + " is being played in this room");
        user->close();
        return;
    }

    user->info.id = static_cast<uint32_t>(user_map.size());
    user->info.authority = user->info.id;
    user->info.has_authority = true;
    for (auto& u : user_list) {
        u->send_join(user->info);
    }
    user_map.push_back(user);
    user_list.push_back(user);

    user->set_room(this);
    user->send_accept();
    
    log("[" + get_id() + "] " + user->info.name + " (" + user->address + ") joined");

    user->send_ping();
    user->set_lag(lag, nullptr);
    
    update_controller_map();
    send_controllers();

    user->send(packet() << GOLF << golf);
}

void room::on_user_quit(user* user) {
    for (auto& u : user_list) {
        if (u->info.authority == user->info.id) {
            delegate_authority(u->info.id, u->info.id);
        }
    }

    auto it = find_if(begin(user_map), end(user_map), [&](auto& u) { return u == user; });
    if (it == end(user_map)) return;
    *it = nullptr;

    user_list.clear();
    for (auto& u : user_map) {
        if (u) user_list.push_back(u);
    }

    log("[" + get_id() + "] " + user->info.name + " quit");

    if (user_list.empty()) {
        close();
        return;
    }

    for (auto& u : user_list) {
        u->send_quit(user->info.id);
    }

    if (started) {
        for (auto& u : user_list) {
            u->flush_input();
        }
    } else {
        update_controller_map();
        send_controllers();
    }
}

double room::get_latency() const {
    double max1 = -INFINITY;
    double max2 = -INFINITY;
    for (auto& u : user_list) {
        if (!u->info.has_authority) continue;
        auto latency = u->get_median_latency();
        if (latency > max1) {
            max2 = max1;
            max1 = latency;
        } else if (latency > max2) {
            max2 = latency;
        }
    }
    return max(0.0, max1 + max2) / 2;
}

double room::get_input_rate() const {
    for (auto& u : user_list) {
        if (!u->info.has_authority) continue;
        return u->get_input_rate();
    }
    return nan("");
}

void room::auto_adjust_lag() {
    double input_rate = get_input_rate();
    if (isnan(input_rate)) return;

    int ideal_lag = min((int)ceil(get_latency() * input_rate - 0.1), 255);
    if (ideal_lag < lag) {
        set_lag(lag - 1, nullptr);
    } else if (ideal_lag > lag) {
        set_lag(lag + 1, nullptr);
    }
}

void room::on_ping_tick() {
    send_latencies();

    if (autolag && started) {
        auto_adjust_lag();
    }

    for (auto& u : user_list) {
        u->send_ping();
    }
}

void room::on_game_start() {
    if (started) return;
    started = true;

    for (auto& u : user_list) {
        u->send_start_game();
    }
}

void room::update_controller_map() {
    uint8_t dst_port = 0;
    for (auto& u : user_list) {
        if (u->info.manual_map) continue;
        u->info.map.clear();
        for (uint8_t src_port = 0; src_port < 4 && dst_port < 4; src_port++) {
            if (u->info.controllers[src_port].present) {
                u->info.map.set(src_port, dst_port++);
            }
        }
    }
}

void room::send_controllers() {
    packet p;
    p << CONTROLLERS;
    for (auto& u : user_list) {
        for (auto& c : u->info.controllers) {
            p << c;
        }
        p << u->info.map;
    }

    for (auto& u : user_list) {
        u->send(p);
    }
}

void room::send_info(const string& message) {
    for (auto& u : user_list) {
        u->send_info(message);
    }
}

void room::send_error(const string& message) {
    log("[" + get_id() + "] " + message);

    for (auto& u : user_list) {
        u->send_error(message);
    }
}

void room::set_lag(uint8_t lag, user* source) {
    packet p;
    p << LAG << lag << (source ? source->info.id : 0xFFFFFFFF);

    this->lag = lag;

    for (auto& u : user_list) {
        if (u == source) continue;
        u->info.lag = lag;
        p << u->info.id;
    }

    for (auto& u : user_list) {
        u->send(p);
    }

    if (source) {
        for (auto& u : user_list) {
            if (u == source) continue;
            u->send_info(source->info.name + " set the lag to " + to_string((int)lag));
        }
    }
}

void room::send_latencies() {
    packet p;
    p << LATENCY;
    for (auto& u : user_list) {
        p << u->info.latency;
    }
    for (auto& u : user_list) {
        u->send(p);
    }
}

void room::delegate_authority(uint32_t user_id, uint32_t authority, user* source) {
    auto& user = user_map.at(user_id);
    if (!user) return;
    user->info.authority = authority;
    for (auto& u : user_list) {
        if (source && u->info.id == source->info.id) continue;
        u->send_delegate_authority(user->info.id, user->info.authority);
    }
    for (auto& u : user_list) {
        u->info.has_authority = false;
    }
    for (auto& u : user_list) {
        auto& auth_user = user_map.at(u->info.authority);
        if (auth_user) {
            auth_user->info.has_authority = true;
        }
    }
}

void room::on_input_from(user* from) {
    user* min_user = nullptr;
    for (auto& u : user_list) {
        if (u->info.input_id < from->info.input_id) {
            if (min_user) { // More than one user with a lower input_id
                return;
            } else { // At least one user with a lower input_id
                min_user = u;
            }
        }
    }

    if (min_user) { // Exactly one user with a lower input_id
        if (min_user->info.authority == min_user->info.id) { // User does not need to wait for their own input. Flush immediately
            min_user->flush_input();
        }
    } else { // No users with lower a input_id
        if (from->info.authority == from->info.id) {
            for (auto& u : user_list) {
                if (u->info.id == from->info.id) continue;
                u->flush_input();
            }
        } else {
            for (auto& u : user_list) {
                u->flush_input();
            }
        }
    }
}
