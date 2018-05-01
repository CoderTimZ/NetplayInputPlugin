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
        my_data.reserve(my_data.size() + str.size());

        for (size_t i = 0; i < str.size(); i++) {
            write<uint8_t>(str[i]);
        }
    }

    void write(const packet& p) {
        write(p.data());
    }

    template<typename T> void write(const T value) {
        static_assert(std::is_integral<T>::value || std::is_enum<T>::value, "Integral or enum required");
        for (int i = sizeof value - 1; i >= 0; --i) {
            my_data.push_back((static_cast<unsigned>(value) >> (8 * i)) & 0xFF);
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

    std::string& read(std::string& str) {
        for (size_t i = 0; i < str.size(); i++) {
            str[i] = read<char>();
        }
        return str;
    }

    template<typename T> T read() {
        static_assert(std::is_integral<T>::value || std::is_enum<T>::value, "Integral or enum required");
        assert(sizeof(T) <= my_data.size() - read_pos);

        T value = 0;
        for (int i = sizeof value - 1; i >= 0; --i) {
            value |= static_cast<T>(my_data[read_pos++]) << (8 * i);
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
