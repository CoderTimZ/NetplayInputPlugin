#pragma once

#include "stdafx.h"

constexpr static uint32_t PROTOCOL_VERSION = 34;

enum PACKET_TYPE : uint8_t {
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
    CONTROLLER_MAP,
    INPUT_DATA,
    INPUT_FILL,
    FRAME,
    SYNC_REQ,
    SYNC_RES,
    HIA
};

enum MESSAGE_TYPE : int32_t {
    ERROR_MESSAGE = -2,
    INFO_MESSAGE  = -1
};

// http://en64.shoutwiki.com/wiki/ROM
enum COUNTRY_CODE : char {
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

struct rom_info {
    uint32_t crc1 = 0;
    uint32_t crc2 = 0;
    std::string name = "";
    uint8_t country_code = 0;
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
        result.reserve(result.length() + 18);
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

double timestamp();
std::string endpoint_to_string(const asio::ip::tcp::endpoint& endpoint);
void log(const std::string& message);
void log(std::ostream& stream, const std::string& message);
#ifdef __GNUC__
void print_stack_trace();
#endif