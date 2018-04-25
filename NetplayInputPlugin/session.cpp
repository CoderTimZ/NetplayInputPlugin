#include "session.h"
#include "client_server_common.h"
#include "util.h"

using namespace std;
using namespace boost::asio;

session::session(shared_ptr<server> my_server, uint32_t id)
    : socket(my_server->io_s), my_server(my_server), id(id), controllers(MAX_PLAYERS) { }

void session::handle_error(const boost::system::error_code& error) {
    if (error == error::operation_aborted) {
        return;
    }

    my_server->remove_session(id);
}

void session::stop() {
    boost::system::error_code error;
    socket.shutdown(ip::tcp::socket::shutdown_both, error);
    socket.close(error);
}

uint32_t session::get_id() const {
    return id;
}

bool session::is_player() {
    return in_buttons.size() > 0;
}

const vector<controller::CONTROL>& session::get_controllers() const {
    return controllers;
}

const string& session::get_name() const {
    return name;
}

int32_t session::get_latency() const {
    return latency_history.empty() ? -1 : latency_history.front();
}

int32_t session::get_average_latency() const {
    auto size = latency_history.size();
    if (size == 0) return -1;
    int32_t sum = 0;
    for (auto latency : latency_history) {
        sum += latency;
    }
    return sum / size;
}

void session::send_controller_range(uint8_t player_index, uint8_t player_count) {
    this->player_index = player_index;
    in_buttons.resize(player_count);

    send(packet() << PLAYER_RANGE << player_index << player_count);
}

void session::send_controllers(const vector<controller::CONTROL>& controllers) {
    assert(controllers.size() == MAX_PLAYERS);

    int total_count = 0;
    for (size_t i = 0; i < controllers.size(); i++) {
        if (controllers[i].Present) {
            total_count++;
        }
    }

    output_buttons.resize(total_count);

    packet p;
    p << CONTROLLERS;
    for (auto& c : controllers) {
        p << c.Plugin << c.Present << c.RawData;
    }
    send(p);
}

void session::send_start_game() {
    send(packet() << START);
}

void session::send_name(uint32_t id, const string& name) {
    packet p;
    p << NAME;
    p << id;
    p << (uint8_t) name.size();
    p << name;

    send(p);
}

void session::send_ping(uint64_t time) {
    send(packet() << PING << time);
}

void session::send_departure(uint32_t id) {
    send(packet() << QUIT << id);
}

void session::send_message(int32_t id, const string& message) {
    send(packet() << MESSAGE << id << (uint16_t)message.size() << message);
}

void session::next_packet() {
    auto self(shared_from_this());
    auto p = make_shared<packet>(sizeof(uint32_t));
    async_read(socket, buffer(p->data()), [=](const boost::system::error_code& error, size_t transferred) {
        if (error) return handle_error(error);
        auto packet_size = p->read<uint32_t>();
        if (packet_size == 0) return self->next_packet();

        auto p = make_shared<packet>(packet_size);
        async_read(socket, buffer(p->data()), [=](const boost::system::error_code& error, size_t transferred) {
            if (error) return handle_error(error);

            auto command = p->read<uint8_t>();
            switch (command) {
                case VERSION: {
                    auto protocol_version = p->read<uint32_t>();
                    if (protocol_version != PROTOCOL_VERSION) {
                        stop();
                    }
                    break;
                }

                case PONG: {
                    auto timestamp = p->read<uint64_t>();
                    latency_history.push_back((uint32_t)(my_server->get_time() - timestamp) / 2);
                    while (latency_history.size() > 4) latency_history.pop_front();
                    break;
                }

                case CONTROLLERS: {
                    for (auto& c : controllers) {
                        *p >> c.Plugin >> c.Present >> c.RawData;
                    }
                    break;
                }

                case NAME: {
                    auto name_length = p->read<uint8_t>();
                    string name(name_length, ' ');
                    p->read(name);
                    my_server->send_name(id, name);
                    self->name = name;
                    break;
                }

                case MESSAGE: {
                    auto message_length = p->read<uint16_t>();
                    string message(message_length, ' ');
                    p->read(message);
                    my_server->send_message(id, message);
                    break;
                }

                case LAG: {
                    auto lag = p->read<uint8_t>();
                    my_server->send_lag(id, lag);
                    break;
                }

                case AUTOLAG: {
                    my_server->autolag = !my_server->autolag;
                    if (my_server->autolag) {
                        my_server->send_message(-1, "Automatic lag is ENABLED");
                    } else {
                        my_server->send_message(-1, "Automatic lag is DISABLED");
                    }
                    break;
                }

                case START: {
                    my_server->send_start_game();
                    break;
                }

                case INPUT_DATA: {
                    auto frame = p->read<uint32_t>();
                    for (auto& i : in_buttons) {
                        *p >> i.Value;
                    }
                    my_server->send_input(id, player_index, frame, in_buttons);
                    if (frame_history.empty() || frame > get<0>(frame_history.back())) {
                        auto time = my_server->get_time();
                        frame_history.push_back(make_tuple(frame, time));
                        while (get<1>(frame_history.front()) <= time - 1000) {
                            frame_history.pop_front();
                        }
                    }
                    break;
                }
            }

            self->next_packet();
        });
    });
}

void session::send_lag(uint8_t lag) {
    send(packet() << LAG << lag);
}

void session::send_protocol_version() {
    send(packet() << VERSION << PROTOCOL_VERSION);
}

void session::send(const packet& p) {
    output_queue.push_back(p);
    flush();
}

void session::flush() {
    if (output_buffer.empty() && !output_queue.empty()) {
        do {
            output_buffer << output_queue.front().size() << output_queue.front();
            output_queue.pop_front();
        } while (!output_queue.empty());

        auto self(shared_from_this());
        async_write(socket, buffer(output_buffer.data()), [=](const boost::system::error_code& error, size_t transferred) {
            output_buffer.clear();
            if (error) return handle_error(error);
            self->flush();
        });
    }
}

void session::send_input(uint8_t player_index, const vector<controller::BUTTONS>& input) {
    for (size_t i = 0; i < input.size(); i++) {
        output_buttons[player_index + i].push_back(input[i]);
    }

    if (ready_to_send_input()) {
        send_input();
    }
}

bool session::is_me(uint32_t player) {
    return player_index <= player && player < player_index + in_buttons.size();
}

bool session::ready_to_send_input() {
    for (size_t i = 0; i < output_buttons.size(); i++) {
        if (!is_me(i) && output_buttons[i].empty()) {
            return false;
        }
    }

    return true;
}

void session::send_input() {
    packet p;
    p << INPUT_DATA;
    for (size_t i = 0; i < output_buttons.size(); i++) {
        if (!is_me(i)) {
            p << output_buttons[i].front().Value;
            output_buttons[i].pop_front();
        }
    }

    send(p);
}

uint32_t session::get_fps() {
    return frame_history.size();
}