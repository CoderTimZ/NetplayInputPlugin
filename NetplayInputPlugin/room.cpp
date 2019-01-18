#include "stdafx.h"

#include "room.h"
#include "user.h"
#include "common.h"

using namespace std;
using namespace asio;

room::room(const string& id, shared_ptr<server> my_server)
    : id(id), my_server(my_server), started(false), timer(*my_server->io_s) { }

const string& room::get_id() const {
    return id;
}

void room::close() {
    for (auto& u : users) {
        u->conn->close();
    }
    timer.cancel();
    if (!my_server.expired()) {
        my_server.lock()->on_room_close(shared_from_this());
    }
}

shared_ptr<user> room::get_user(uint32_t id) {
    auto it = find_if(begin(users), end(users), [&](auto& u) { return u->get_id() == id; });
    return it == end(users) ? nullptr : *it;
}

void room::on_user_join(shared_ptr<user> user) {
    if (started) {
        user->send_error("Game is already in progress");
        user->conn->close();
        return;
    }

    user->send_accept();

    for (auto& u : users) {
        u->send_join(user->get_id(), user->get_name());
    }
    users.push_back(user);
    log("[" + get_id() + "] " + user->name + " joined");
    user->set_room(shared_from_this());
    for (auto& u : users) {
        user->send_join(u->get_id(), u->get_name());
    }
    user->send_ping();

    if (!hia) {
        user->send_lag(lag);
    }
    
    update_controller_map();
    send_controllers();

    if (golf && !hia) {
        user->conn->send(pout.reset() << GOLF << golf, user->error_handler());
    }

    user->send_hia(hia);
}

void room::on_user_quit(shared_ptr<user> user) {
    auto it = find_if(begin(users), end(users), [&](auto& u) { return u == user; });
    if (it == end(users)) return;

    for (auto& u : users) {
        u->send_quit(user->get_id());
    }

    users.erase(it);
    log("[" + get_id() + "] " + user->name + " quit");

    if (started && user->is_player()) {
        close();
    } else if (users.empty()) {
        close();
    } else {
        update_controller_map();
        send_controllers();
    }
}


double room::get_latency() {
    double max1 = -INFINITY;
    double max2 = -INFINITY;
    for (auto& u : users) {
        if (u->is_player()) {
            auto latency = u->get_median_latency();
            if (latency > max1) {
                max2 = max1;
                max1 = latency;
            } else if (latency > max2) {
                max2 = latency;
            }
        }
    }
    return max(0.0, max1 + max2) / 2;
}

double room::get_fps() {
    for (auto& u : users) {
        if (u->is_player()) {
            return u->get_fps();
        }
    }

    return NAN;
}

void room::auto_adjust_lag() {
    double fps = get_fps();
    if (isnan(fps)) return;

    int ideal_lag = min((int)ceil(get_latency() * fps - 0.1), 255);
    if (ideal_lag < lag) {
        send_lag(-1, lag - 1);
    } else if (ideal_lag > lag) {
        send_lag(-1, lag + 1);
    }
}

void room::on_tick() {
    send_latencies();

    if (autolag && !hia) {
        auto_adjust_lag();
    }

    for (auto& u : users) {
        u->send_ping();
    }
}

void room::on_input_tick() {
    while (next_input_tick <= std::chrono::steady_clock::now()) {
        for (auto& p : users) {
            if (p->is_player()) {
                pout.reset() << INPUT_DATA << p->id << p->current_input;
                for (auto& u : users) {
                    u->send_input(*p, pout);
                }
            }
        }

        for (auto& u : users) {
            u->conn->flush(u->error_handler());
        }

        next_input_tick += 1000000000ns / hia;
    }

    timer.expires_at(next_input_tick);
    timer.async_wait([=](const error_code& error) {
        if (error == error::operation_aborted) {

        } else if (error) {
            log(cerr, error.message());
        } else {
            on_input_tick();
        }
    });
}

void room::on_game_start() {
    if (started) return;
    started = true;

    for (auto& u : users) {
        u->send_start_game();
    }

    if (hia) {
        next_input_tick = std::chrono::steady_clock::now();
        on_input_tick();
    }
}

void room::update_controller_map() {
    uint8_t dst_port = 0;
    for (auto& u : users) {
        if (u->manual_map) continue;
        u->my_controller_map.clear();
        const auto& src_controllers = u->get_controllers();
        for (uint8_t src_port = 0; src_port < 4 && dst_port < 4; src_port++) {
            if (src_controllers[src_port].present) {
                u->my_controller_map.set(src_port, dst_port++);
            }
        }
    }
}

void room::set_hia(uint32_t hia) {
    this->hia = hia;
}

void room::send_controllers() {
    pout.reset() << CONTROLLERS;
    for (auto& u : users) {
        pout << u->get_id();
        for (auto& c : u->controllers) {
            pout << c.plugin << c.present << c.raw_data;
        }
        pout << u->my_controller_map.bits;
    }

    for (auto& u : users) {
        u->conn->send(pout, u->error_handler());
    }
}

void room::send_info(const string& message) {
    for (auto& u : users) {
        u->send_info(message);
    }
}

void room::send_error(const string& message) {
    log("[" + get_id() + "] " + message);

    for (auto& u : users) {
        u->send_error(message);
    }
}

void room::send_lag(int32_t id, uint8_t lag) {
    this->lag = lag;

    string message = (id == -1 ? "The server" : get_user(id)->get_name()) + " set the lag to " + to_string(lag);

    double fps = get_fps();
    if (fps > 0) {
        double latency = lag / fps;
        message += " (" + to_string((int)(latency * 1000)) + " ms)";
    }

    for (auto& u : users) {
        if (u->get_id() != id) {
            u->send_lag(lag);
        }
        if (id >= 0) {
            u->send_info(message);
        }
    }
}

void room::send_latencies() {
    pout.reset() << LATENCY;
    for (auto& u : users) {
        pout << u->get_id() << u->get_latency();
    }
    for (auto& u : users) {
        u->conn->send(pout, u->error_handler(), false);
    }
}

size_t room::player_count() const {
    size_t count = 0;
    for (auto& u : users) {
        if (u->is_player()) {
            count++;
        }
    }
    return count;
}