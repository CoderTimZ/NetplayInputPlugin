#pragma once

#include "stdafx.h"

struct uri {
    std::string scheme;
    std::string host;
    uint16_t port = 0;
    std::string path = "/";

    uri(const std::string& uri) {
        std::string str = uri;
        size_t index = str.find("://");
        if (index != std::string::npos) {
            scheme = str.substr(0, index);
            str = str.substr(index + 3);
        }
        index = str.find("/");
        if (index != std::string::npos) {
            path = str.substr(index);
            str = str.substr(0, index);
        }
        index = str.rfind(":");
        if (index != std::string::npos) {
            port = std::stoi(str.substr(index + 1));
            str = str.substr(0, index);
        }
        host = str;
    }
};