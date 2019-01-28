#pragma once

#include "stdafx.h"

class packet : public std::vector<uint8_t> {
public:
    template<typename T>
    packet& write(T value) {
        typedef typename std::make_unsigned<T>::type unsigned_t;
        helper<unsigned_t>::write(*this, reinterpret_cast<unsigned_t&>(value));
        return *this;
    }

    template<typename T>
    packet& write_var(T value) {
        typedef typename std::make_unsigned<T>::type unsigned_t;
        auto u = reinterpret_cast<unsigned_t&>(value);
        for (; u >= 0x7F; u >>= 7) {
            push_back(static_cast<uint8_t>(u | 0x80));
        }
        push_back(static_cast<uint8_t>(u));
        return *this;
    }

    template<typename T>
    packet& write(const std::vector<T>& vector) {
        for (const auto& e : vector) {
            write(e);
        }
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

    template<typename T>
    packet& operator<<(const T& value) {
        return write(value);
    }

    template<typename T>
    T read() {
        typedef typename std::make_unsigned<T>::type unsigned_t;
        auto value = helper<unsigned_t>::read(*this);
        return reinterpret_cast<T&>(value);
    }

    template<typename T>
    T read_var() {
        T value = 0;
        for (int s = 0;; s += 7) {
            auto b = read<uint8_t>();
            value |= static_cast<T>(b & 0x7F) << s;
            if (b <= 0x7F) break;
        };
        return value;
    }

    template<typename T>
    std::vector<T>& read(std::vector<T>& vector) {
        for (auto& e : vector) {
            e = read<T>();
        }
        return vector;
    }

    std::string& read(std::string& string) {
        string.resize(read_var<size_t>());
        for (size_t i = 0; i < string.length(); i++) {
            string[i] = at(pos++);
        }
        return string;
    }

    template<typename T>
    packet& operator>>(T& value) {
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

    template<typename T, size_t S = sizeof(T)>
    struct helper {
        inline static void write(packet& p, T value) {
            static_assert(S > 1, "Invalid size parameter");
            constexpr auto R = S / 2, L = S - R;
            helper<T, L>::write(p, value >> (R * 8));
            helper<T, R>::write(p, value);
        }

        inline static T read(packet& p) {
            static_assert(S > 1, "Invalid size parameter");
            constexpr auto R = S / 2, L = S - R;
            auto result = helper<T, L>::read(p) << (R * 8);
            return result | helper<T, R>::read(p);
        }
    };

    template<typename T>
    struct helper<T, 1> {
        inline static void write(packet& p, T value) { p.push_back(static_cast<uint8_t>(value)); }
        inline static T read(packet& p) { return static_cast<T>(p.at(p.pos++)); }
    };
};

template<>
inline packet& packet::write<bool>(bool value) {
    return write<uint8_t>(value);
}

template<>
inline packet& packet::write<float>(float value) {
    static_assert(sizeof(uint32_t) == sizeof(float), "sizeof(float) != sizeof(uint32_t)");
    return write<uint32_t>(reinterpret_cast<uint32_t&>(value));
}

template<>
inline packet& packet::write<double>(double value) {
    static_assert(sizeof(uint64_t) == sizeof(double), "sizeof(double) != sizeof(uint64_t)");
    return write<uint64_t>(reinterpret_cast<uint64_t&>(value));
}

template<>
inline bool packet::read<bool>() {
    return read<uint8_t>();
}

template<>
inline float packet::read<float>() {
    static_assert(sizeof(uint32_t) == sizeof(float), "sizeof(float) != sizeof(uint32_t)");
    auto value = read<uint32_t>();
    return reinterpret_cast<float&>(value);
}

template<>
inline double packet::read<double>() {
    static_assert(sizeof(uint64_t) == sizeof(double), "sizeof(double) != sizeof(uint64_t)");
    auto value = read<uint64_t>();
    return reinterpret_cast<double&>(value);
}

template<>
inline std::string packet::read<std::string>() {
    std::string string;
    read(string);
    return string;
}