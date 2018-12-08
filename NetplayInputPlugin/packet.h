#pragma once

#include "stdafx.h"

class packet : public std::vector<uint8_t> {
public:
    template<typename T> packet& write(T value) {
        typedef typename std::make_unsigned<T>::type unsigned_t;
        return helper<unsigned_t>::write(*this, static_cast<unsigned_t>(value));
    }

    template<typename T> packet& write_var(T value) {
        typedef typename std::make_unsigned<T>::type unsigned_t;
        auto u = static_cast<unsigned_t>(value);
        for (; u > 0x7F; u >>= 7) {
            push_back(static_cast<uint8_t>(u | 0x80));
        }
        push_back(static_cast<uint8_t>(u));
        return *this;
    }

    packet& write(const packet& packet) {
        write_var(packet.size());
        insert(end(), packet.begin(), packet.end());
        return *this;
    }

    packet& write(const std::string& string) {
        write_var(string.length());
        insert(end(), string.begin(), string.end());
        return *this;
    }

    template<typename T> packet& operator<<(const T& value) {
        return write(value);
    }

    template<typename T> T read() {
        typedef typename std::make_unsigned<T>::type unsigned_t;
        return static_cast<T>(helper<unsigned_t>::read(*this));
    }

    template<typename T> T read_var() {
        T value = 0;
        for (int i = 0;; i++) {
            auto b = read<uint8_t>();
            value |= static_cast<T>(b & 0x7F) << (7 * i);
            if (b <= 0x7F) break;
        };
        return value;
    }

    std::string& read(std::string& string) {
        string.resize(read_var<size_t>());
        for (size_t i = 0; i < string.length(); i++) {
            string[i] = at(pos++);
        }
        return string;
    }

    std::string read() {
        std::string string;
        read(string);
        return string;
    }

    template<typename T> packet& operator>>(T& value) {
        value = read<T>();
        return *this;
    }

    size_t available() const {
        return pos > size() ? 0 : size() - pos;
    }
    
    packet& reset(size_t size = 0) {
        pos = 0;
        resize(size);
        if (capacity() > 256) {
            shrink_to_fit();
        }
        return *this;
    }

    void swap(packet& other) {
        std::vector<uint8_t>::swap(other);
        std::swap(pos, other.pos);
    }

private:
    size_t pos = 0;

    template<typename T, size_t b = sizeof(T) - 1>
    struct helper {
        static packet& write(packet& p, T value) {
            p.push_back(static_cast<uint8_t>(value >> (8 * b)));
            return helper<T, b - 1>::write(p, value);
        }

        static T read(packet& p) {
            return (static_cast<T>(p.at(p.pos++)) << (8 * b)) | helper<T, b - 1>::read(p);
        }
    };

    template<typename T>
    struct helper<T, 0> {
        static packet& write(packet& p, T value) {
            p.push_back(static_cast<uint8_t>(value));
            return p;
        }

        static T read(packet& p) {
            return static_cast<T>(p.at(p.pos++));
        }
    };
};

template<> inline packet& packet::write<bool>(bool value) {
    return write<uint8_t>(value);
}

template<> inline packet& packet::write<float>(float value) {
    return write<uint32_t>(*(uint32_t*)&value);
}

template<> inline packet& packet::write<double>(double value) {
    return write(*(uint64_t*)&value);
}

template<> inline bool packet::read<bool>() {
    return read<uint8_t>();
}

template<> inline float packet::read<float>() {
    auto value = read<uint32_t>();
    return *(float*)&value;
}

template<> inline double packet::read<double>() {
    auto value = read<uint64_t>();
    return *(double*)&value;
}
