#pragma once

#include "stdafx.h"

class packet {
public:
    std::vector<uint8_t> my_data;

    packet() { }
    packet(uint32_t size) : my_data(size) { }

    template<typename T> void write(const std::vector<T>& data) {
        my_data.reserve(my_data.size() + sizeof(T) * data.size());

        for (size_t i = 0; i < data.size(); i++) {
            write(data[i]);
        }
    }

    void write(const std::string& str) {
        auto length = (uint16_t)std::min(str.length(), (size_t)0xFFFF);
        my_data.reserve(my_data.size() + 2 + length);
        write(length);
        for (size_t i = 0; i < length; i++) {
            write<char>(str[i]);
        }
    }

    void write(const packet& p) {
        write(p.data());
    }

    template<typename T> void write(const T value) {
        auto bytes = (uint8_t*)&value;
        for (int i = 0; i < sizeof(T); ++i) {
            my_data.push_back(bytes[i]);
        }
    }

    template<typename T> packet& operator<<(const T& data) {
        write(data);

        return *this;
    }

    template<typename T> packet& operator+(const T& data) {
        return packet(*this) << data;
    }

    template<typename T> std::vector<T>& read(std::vector<T>& data) {
        for (size_t i = 0; i < data.size(); i++) {
            data[i] = read<T>();
        }
        return data;
    }

    std::string read() {
        std::string str;
        read(str);
        return str;
    }

    std::string& read(std::string& str) {
        auto length = read<uint16_t>();
        str.resize(length);
        for (size_t i = 0; i < length; i++) {
            str[i] = read<char>();
        }
        return str;
    }

    template<typename T> T read() {
        assert(sizeof(T) <= my_data.size() - read_pos);

        T value;
        auto bytes = (uint8_t*)&value;
        for (int i = 0; i < sizeof(T); ++i) {
            bytes[i] = my_data[read_pos++];
        }
        return value;
    }

    template<typename T> packet& operator>>(T& data) {
        data = read<T>();

        return *this;
    }

    std::vector<uint8_t>& data() {
        return my_data;
    }

    const std::vector<uint8_t>& data() const {
        return my_data;
    }

    bool empty() const {
        return data().empty();
    }

    uint32_t size() const {
        return (uint32_t)my_data.size();
    }

    uint32_t bytes_remaining() const {
        return (uint32_t)(my_data.size() - read_pos);
    }

    void clear() {
        return data().clear();
    }

private:
    size_t read_pos = 0;
};
