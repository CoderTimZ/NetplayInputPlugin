#pragma once

#include "stdafx.h"
#include "packet.h"

constexpr static uint32_t PROTOCOL_VERSION = 44;
constexpr static uint32_t INPUT_HISTORY_LENGTH = 10;

enum packet_type : uint8_t {
    VERSION,
    JOIN,
    ACCEPT,
    PATH,
    PING,
    PONG,
    QUIT,
    NAME,
    LATENCY,
    MESSAGE,
    LAG,
    AUTOLAG,
    CONTROLLERS,
    START,
    GOLF,
    INPUT_MAP,
    INPUT_DATA,
    INPUT_UPDATE,
    FRAME,
    REQUEST_AUTHORITY,
    DELEGATE_AUTHORITY
};

enum pak_type : int {
    NONE     = 1,
    MEMORY   = 2,
    RUMBLE   = 3,
    TRANSFER = 4
};

enum message_type : uint32_t {
    ERROR_MSG = 0xFFFFFFFE,
    INFO_MSG  = 0xFFFFFFFF
};

// http://en64.shoutwiki.com/wiki/ROM
enum country_code : char {
    UNKNOWN             = '\0',
    BETA                = '7',
    ASIAN               = 'A',
    BRAZILIAN           = 'B',
    CHINESE             = 'C',
    GERMAN              = 'D',
    NORTH_AMERICAN      = 'E',
    FRENCH              = 'F',
    GATEWAY_64_NTSC     = 'G',
    DUTCH               = 'H',
    ITALIAN             = 'I',
    JAPANESE            = 'J',
    KOREAN              = 'K',
    GATEWAY_64_PAL      = 'L',
    CANADIAN            = 'N',
    EUROPEAN_BASIC_SPEC = 'P',
    SPANISH             = 'S',
    AUSTRALIAN          = 'U',
    SCANDINAVIAN        = 'W',
    EUROPEAN_X          = 'X',
    EUROPEAN_Y          = 'Y'
};

class service_wrapper {
public:
    service_wrapper() : work(service), thread([&] { service.run(); }) {}

    template<typename F> auto run(F&& f) {
        std::packaged_task<decltype(f())(void)> task(f);
        service.post([&] { task(); });
        return task.get_future().get();
    }

    void stop() {
        service.stop();
        thread.join();
    }

    asio::io_service service;
    asio::io_service::work work;
    std::thread thread;
};

struct input_map {
    constexpr static uint16_t IDENTITY_MAP = 0x8421;

    uint16_t bits;

    input_map() : bits(0) { }
    input_map(uint16_t bits) : bits(bits) { }

    bool operator==(const input_map& rhs) const {
        return bits == rhs.bits;
    }

    bool empty() const {
        return bits == 0;
    }

    bool get(uint8_t src, uint8_t dst) const {
        if (src >= 4 || dst >= 4) return false;
        return bits & (1 << (src * 4 + dst));
    }

    void set(uint8_t src, uint8_t dst) {
        if (src >= 4 || dst >= 4) return;
        bits |= (1 << (src * 4 + dst));
    }

    void clear() {
        bits = 0;
    }
};

template<>
inline packet& packet::write<input_map>(input_map map) {
    write(map.bits);
    return *this;
}

template<>
inline input_map packet::read<input_map>() {
    return input_map(read<uint16_t>());
}

struct input_data {
    constexpr static size_t SIZE = 18;

    std::array<uint32_t, 4> data;
    input_map map;

    operator bool() const {
        return data[0] || data[1] || data[2] || data[3];
    }

    uint32_t& operator[](size_t i) {
        return data.at(i);
    }

    const uint32_t& operator[](size_t i) const {
        return data.at(i);
    }
};

template<>
inline packet& packet::write<input_data>(input_data input) {
    write(input.data[0]);
    write(input.data[1]);
    write(input.data[2]);
    write(input.data[3]);
    write(input.map);
    return *this;
}

template<>
inline input_data packet::read<input_data>() {
    input_data input;
    input.data[0] = read<uint32_t>();
    input.data[1] = read<uint32_t>();
    input.data[2] = read<uint32_t>();
    input.data[3] = read<uint32_t>();
    input.map = read<input_map>();
    return input;
}

struct rom_info {
    uint32_t crc1 = 0;
    uint32_t crc2 = 0;
    std::string name = "";
    char country_code = 0;
    uint8_t version = 0;

    operator bool() const {
        return crc1 && crc2;
    }

    operator std::string() const {
        return to_string();
    }

    std::string to_string() const {
        static constexpr char HEX[] = "0123456789ABCDEF";

        std::string result = name;
        result += '-';
        for (int i = 0; i < 8; i++) {
            result += HEX[(crc1 >> ((i ^ 7) * 4)) & 0xF];
        }
        result += '-';
        for (int i = 0; i < 8; i++) {
            result += HEX[(crc2 >> ((i ^ 7) * 4)) & 0xF];
        }
        return result;
    }
};

template<>
inline packet& packet::write<rom_info>(rom_info info) {
    write(info.crc1);
    write(info.crc2);
    write(info.name);
    write(info.country_code);
    write(info.version);
    return *this;
}

template<>
inline rom_info packet::read<rom_info>() {
    rom_info info;
    info.crc1 = read<uint32_t>();
    info.crc2 = read<uint32_t>();
    info.name = read<std::string>();
    info.country_code = read<char>();
    info.version = read<uint8_t>();
    return info;
}

struct controller {
    int present = 0;
    int raw_data = 0;
    int plugin = pak_type::NONE;
};

template<>
inline packet& packet::write<controller>(controller c) {
    write(c.present);
    write(c.raw_data);
    write(c.plugin);
    return *this;
}

template<>
inline controller packet::read<controller>() {
    controller c;
    c.present = read<int>();
    c.raw_data = read<int>();
    c.plugin = read<pak_type>();
    return c;
}

struct user_info {
    uint32_t id = 0xFFFFFFFF;
    uint32_t authority = 0xFFFFFFFF;
    std::string name;
    rom_info rom;
    uint8_t lag = 5;
    double latency = NAN;
    std::array<controller, 4> controllers;
    input_map map;
    bool manual_map = false;

    input_data input = input_data();
    input_data pending = input_data();
    std::list<input_data> input_queue;
    std::list<input_data> input_history;
    uint32_t input_id = 0;
    bool has_authority = false;

    bool add_input_history(uint32_t input_id, const input_data& input) {
        if (input_id != this->input_id) return false;
        input_history.push_back(input);
        while (input_history.size() > INPUT_HISTORY_LENGTH) {
            input_history.pop_front();
        }
        this->input_id++;
        return true;
    }
};

template<>
inline packet& packet::write<user_info>(user_info info) {
    write(info.id);
    write(info.authority);
    write(info.name);
    write(info.rom);
    write(info.lag);
    write(info.latency);
    write(info.controllers[0]);
    write(info.controllers[1]);
    write(info.controllers[2]);
    write(info.controllers[3]);
    write(info.map);
    write(info.manual_map);
    return *this;
}

template<>
inline user_info packet::read<user_info>() {
    user_info info;
    info.id = read<uint32_t>();
    info.authority = read<uint32_t>();
    info.name = read<std::string>();
    info.rom = read<rom_info>();
    info.lag = read<uint8_t>();
    info.latency = read<double>();
    info.controllers[0] = read<controller>();
    info.controllers[1] = read<controller>();
    info.controllers[2] = read<controller>();
    info.controllers[3] = read<controller>();
    info.map = read<input_map>();
    info.manual_map = read<bool>();
    return info;
}

template<typename InternetProtocol>
std::string endpoint_to_string(const asio::ip::basic_endpoint<InternetProtocol>& endpoint, bool include_port = false) {
    std::string result;
    if (endpoint.address().is_v6()) {
        auto v6_address = endpoint.address().to_v6();
        if (v6_address.is_v4_mapped()) {
            result = make_address_v4(asio::ip::v4_mapped, v6_address).to_string();
        } else {
            result = "[" + v6_address.to_string() + "]";
        }
    } else {
        result = endpoint.address().to_string();
    }
    if (include_port) {
        result += ":" + std::to_string(endpoint.port());
    }
    return result;
}

struct ci_less {
    struct nocase_compare {
        bool operator()(const unsigned char& c1, const unsigned char& c2) const {
            return std::tolower<char>(c1, std::locale::classic()) < std::tolower<char>(c2, std::locale::classic());
        }
    };

    bool operator()(const std::string& s1, const std::string& s2) const {
        return std::lexicographical_compare(s1.begin(), s1.end(), s2.begin(), s2.end(), nocase_compare());
    }
};

double timestamp();
void log(const std::string& message);
void log(std::ostream& stream, const std::string& message);
std::string& ltrim(std::string& str);
std::string& rtrim(std::string& str);
std::string& trim(std::string& str);
#ifdef __GNUC__
void print_stack_trace();
#endif