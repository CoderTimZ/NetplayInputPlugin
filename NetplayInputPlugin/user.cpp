#include "stdafx.h"

#include "user.h"
#include "common.h"
#include "util.h"

using namespace std;
using namespace asio;

uint32_t user::next_id = 0;

user::user(shared_ptr<io_service> io_service, shared_ptr<server> server)
    : connection(io_service), my_server(server), id(++next_id) { }

void user::set_room(room_ptr my_room) {
    this->my_room = my_room;

    send(pout.reset() << PATH << ("/" + my_room->get_id()));
}

bool user::joined() {
    return (bool)my_room;
}

void user::handle_error(const error_code& error) {
    if (joined()) {
        log("(" + my_room->get_id() + ") " + name + " (" + address + ") disconnected");
        my_room->on_user_quit(shared_from_this());
    }
}

uint32_t user::get_id() const {
    return id;
}

bool user::is_player() const {
    return !is_spectator();
}

bool user::is_spectator() const {
    return my_controller_map.empty();
}

const array<controller, 4>& user::get_controllers() const {
    return controllers;
}

const string& user::get_name() const {
    return name;
}

double user::get_latency() const {
    return latency_history.empty() ? NAN : latency_history.front();
}

double user::get_median_latency() const {
    if (latency_history.empty()) return NAN;
    vector<double> lat(latency_history.begin(), latency_history.end());
    sort(lat.begin(), lat.end());
    return lat[lat.size() / 2];
}

double user::get_fps() {
    if (frame_history.empty() || frame_history.front() == frame_history.back()) {
        return NAN;
    } else {
        return (frame_history.size() - 1) / (frame_history.back() - frame_history.front());
    }
}

void user::process_packet() {
    auto self(shared_from_this());
    receive([=](packet& pin) {
        if (pin.empty()) return self->process_packet();

        try {
            switch (pin.read<PACKET_TYPE>()) {
                case JOIN: {
                    if (joined()) break;
                    auto protocol_version = pin.read<uint32_t>();
                    if (protocol_version != PROTOCOL_VERSION) {
                        return close();
                    }
                    string room = pin.read();
                    if (!room.empty() && room[0] == '/') {
                        room = room.substr(1);
                    }
                    pin.read(self->name);
                    log(self->name + " (" + address + ") joined");
                    for (auto& c : controllers) {
                        pin >> c.plugin >> c.present >> c.raw_data;
                    }
                    my_server->on_user_join(shared_from_this(), room);
                    break;
                }

                case PING: {
                    pout.reset() << PONG;
                    while (pin.available()) {
                        pout << pin.read<uint8_t>();
                    }
                    send(pout);
                    break;
                }

                case PONG: {
                    latency_history.push_back(timestamp() - pin.read<double>());
                    while (latency_history.size() > 7) latency_history.pop_front();
                    break;
                }

                case CONTROLLERS: {
                    if (!joined()) break;
                    for (auto& c : controllers) {
                        pin >> c.plugin >> c.present >> c.raw_data;
                    }
                    if (!my_room->started) {
                        my_room->update_controller_map();
                    }
                    my_room->send_controllers();
                    break;
                }

                case NAME: {
                    if (!joined()) break;
                    string old_name = self->name;
                    pin.read(self->name);
                    log("(" + my_room->get_id() + ") " + old_name + " is now " + self->name);
                    for (auto& u : my_room->users) {
                        u->send_name(id, name);
                    }
                    break;
                }

                case MESSAGE: {
                    if (!joined()) break;
                    string message = pin.read();
                    auto self(shared_from_this());
                    for (auto& u : my_room->users) {
                        if (u == self) continue;
                        u->send_message(get_id(), message);
                    }
                    break;
                }

                case LAG: {
                    if (!joined()) break;
                    auto lag = pin.read<uint8_t>();
                    my_room->send_lag(id, lag);
                    break;
                }

                case AUTOLAG: {
                    if (!joined()) break;
                    auto value = pin.read<int8_t>();
                    if (value == (int8_t)my_room->autolag) break;

                    if (value == 0) {
                        my_room->autolag = false;
                    } else if (value == 1) {
                        my_room->autolag = true;
                    } else {
                        my_room->autolag = !my_room->autolag;
                    }
                    if (my_room->autolag) {
                        my_room->send_status("Automatic Lag is enabled");
                    } else {
                        my_room->send_status("Automatic Lag is disabled");
                    }
                    break;
                }

                case START: {
                    if (!joined()) break;
                    log("(" + my_room->get_id() + ") " + self->name + " started the game");
                    my_room->on_game_start();
                    break;
                }

                case INPUT_DATA: {
                    if (!joined()) break;
                    input_received++;
                    current_input.resize(pin.available());
                    pin.read(current_input);
                    if (!my_room->hia) {
                        pout.reset() << INPUT_DATA << id << current_input;
                        for (auto& u : my_room->users) {
                            if (u == self) continue;
                            u->send_input(*this, pout);
                        }
                    }
                    break;
                }

                case INPUT_FILL: {
                    if (!joined()) break;
                    pin >> input_received;
                    pout.reset() << INPUT_FILL << id << input_received;
                    for (auto& u : my_room->users) {
                        if (u->id == id) continue;
                        u->send(pout);
                    }
                    break;
                }

                case FRAME: {
                    auto ts = timestamp();
                    frame_history.push_back(ts);
                    while (frame_history.front() <= ts - 2.0) {
                        frame_history.pop_front();
                    }
                    break;
                }

                case CONTROLLER_MAP: {
                    if (!joined()) break;
                    controller_map map(pin.read<uint16_t>());
                    pout.reset() << CONTROLLER_MAP << id << map.bits;
                    for (auto& u : my_room->users) {
                        if (u->id == id) continue;
                        u->send(pout);
                    }
                    my_controller_map = map;
                    manual_map = true;
                    break;
                }

                case GOLF: {
                    my_room->golf = pin.read<bool>();
                    for (auto& u : my_room->users) {
                        if (u->id == id) continue;
                        u->send(pin);
                    }
                    break;
                }

                case SYNC_REQ: {
                    auto sync_id = pin.read<uint32_t>();
                    pout.reset() << SYNC_REQ << id << sync_id;
                    for (auto& u : my_room->users) {
                        if (u->id == id) continue;
                        u->send(pout);
                    }
                    break;
                }

                case SYNC_RES: {
                    auto user_id = pin.read<uint32_t>();
                    auto user = my_room->get_user(user_id);
                    if (!user) break;
                    auto sync_id = pin.read<uint32_t>();
                    auto frame = pin.read<uint32_t>();
                    user->send(pout.reset() << SYNC_RES << id << sync_id << frame);
                    break;
                }

                case HIA: {
                    auto hia = std::min(240u, pin.read_var<uint32_t>());
                    if (!my_room->started || my_room->hia && hia) {
                        my_room->hia = hia;
                        for (auto& u : my_room->users) {
                            u->send_hia(hia);
                        }
                    }
                    break;
                }
            }

            self->process_packet();
        } catch (...) {
            self->close();
        }
    });
}

void user::send_protocol_version() {
    send(pout.reset() << VERSION << PROTOCOL_VERSION);
}

void user::send_accept() {
    send(pout.reset() << ACCEPT << id);
}

void user::send_join(uint32_t user_id, const string& name) {
    send(pout.reset() << JOIN << user_id << name);
}

void user::send_start_game() {
    send(pout.reset() << START);
}

void user::send_name(uint32_t user_id, const string& name) {
    send(pout.reset() << NAME << user_id << name);
}

void user::send_ping() {
    send(pout.reset() << PING << timestamp());
}

void user::send_quit(uint32_t id) {
    send(pout.reset() << QUIT << id);
}

void user::send_message(int32_t id, const string& message) {
    send(pout.reset() << MESSAGE << id << message);
}

void user::send_status(const string& message) {
    send_message(STATUS_MESSAGE, message);
}

void user::send_error(const string& message) {
    send_message(ERROR_MESSAGE, message);
}

void user::send_lag(uint8_t lag) {
    send(pout.reset() << LAG << lag);
}

void user::send_input(const user& user, const packet& p) {
    send(p, false);
    if (my_room->hia) return;
    for (auto& u : my_room->users) {
        if (u->id == id) continue;
        if (u->is_spectator()) continue;
        if (u->input_received < user.input_received) return;
    }
    flush();
}

void user::send_hia(uint32_t hia) {
    pout.reset() << HIA;
    pout.write_var(hia);
    send(pout);
}