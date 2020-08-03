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
    if (timer) {
        timer->cancel();
        timer.reset();
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

    for (auto& u : user_list) {
        u->send_join(user->info);
    }
    user->id = static_cast<uint32_t>(user_map.size());
    user->set_room(this);
    user->send_accept();

    user_map.push_back(user);
    user_list.push_back(user);
    
    log("[" + get_id() + "] " + user->info.name + " (" + user->address + ") joined");

    user->send_ping();
    user->set_lag(lag, nullptr);
    user->set_input_authority(input_authority);
    
    update_controller_map();
    send_controllers();

    user->send(packet() << GOLF << golf);
}

void room::on_user_quit(user* user) {
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
        u->send_quit(user->id);
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
        if (u->info.input_authority == HOST) continue;
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
        if (u->info.input_authority == HOST) continue;
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

void room::start_or_stop_input_timer() {
    bool start_timer = false;

    if (started) {
        if (golf) {
            start_timer = true;
        } else {
            for (auto& u : user_list) {
                if (u->info.input_authority == HOST) {
                    start_timer = true;
                    break;
                }
            }
        }
    }
    
    if (start_timer && !timer) {
        timer = make_unique<steady_timer>(*my_server->service);
        next_input_tick = std::chrono::steady_clock::now();
        on_input_tick();
    } else if (!start_timer && timer) {
        timer->cancel();
        timer.reset();
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

void room::on_input_tick() {
    while (next_input_tick <= std::chrono::steady_clock::now()) {
        send_hia_input();
        next_input_tick += 1000000000ns / hia_rate;
    }

    timer->expires_at(next_input_tick);

    auto self(weak_from_this());
    timer->async_wait([self, this](const error_code& error) {
        if (self.expired()) return;
        if (error == error::operation_aborted) {

        } else if (error) {
            log(cerr, error.message());
        } else {
            on_input_tick();
        }
    });
}

void room::send_hia_input() {
    user* hia_user = nullptr;
    for (auto& u : user_list) {
        if (u->info.input_authority != HOST) continue;
        if (!hia_user || u->info.input_id < hia_user->info.input_id) {
            hia_user = u;
        }
    }
    if (!hia_user) return;

    auto input_id = hia_user->info.input_id;

    for (auto& u : user_list) {
        if (u->info.input_authority == HOST && u->info.input_id == input_id) {
            u->info.add_input_history(u->info.input_id, u->hia_input);
            for (auto& v : user_list) {
                v->write_input_from(u);
            }
        }
    }

    on_input_from(hia_user);
}

void room::on_game_start() {
    if (started) return;
    started = true;

    for (auto& u : user_list) {
        u->send_start_game();
    }
    
    start_or_stop_input_timer();
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
    p << LAG << lag << (source ? source->id : 0xFFFFFFFF);

    this->lag = lag;

    for (auto& u : user_list) {
        if (u == source) continue;
        u->info.lag = lag;
        p << u->id;
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
        if (min_user->info.input_authority == CLIENT) { // User does not need to wait for their own input. Flush immediately
            min_user->flush_input();
        }
    } else { // No users with lower a input_id
        if (from->info.input_authority == CLIENT) {
            for (auto& u : user_list) {
                if (u->id == from->id) continue;
                u->flush_input();
            }
        } else {
            for (auto& u : user_list) {
                u->flush_input();
            }
        }
    }
}
