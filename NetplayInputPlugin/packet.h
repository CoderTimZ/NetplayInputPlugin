#pragma once

#include "stdafx.h"

class packet : public std::vector<uint8_t> {
public:
    packet() { }
    packet(size_t size) : std::vector<uint8_t>(size) { }

    template<typename T>
    packet& write(T value) {
        typedef typename std::make_unsigned<T>::type unsigned_t;
        helper<unsigned_t>::write(*this, reinterpret_cast<unsigned_t&>(value));
        return *this;
    }

    template<typename T>
    packet& write_var(T value) {
        typedef typename std::make_unsigned<T>::type unsigned_t;
        auto v = reinterpret_cast<unsigned_t&>(value);
        for (; v > 0b01111111; v >>= 7) {
            push_back(static_cast<uint8_t>(v | 0b10000000));
        }
        push_back(static_cast<uint8_t>(v));
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

    packet& transpose(size_t rows, size_t cols) {
        if (rows == 0 && cols == 0) {
            return *this;
        } else if (rows == 0) {
            rows = size() / cols;
        } else if (cols == 0) {
            cols = size() / rows;
        }
        if (rows * cols > size()) {
            throw std::exception();
        }
        auto copy = *this;
        for (size_t i = 0; i < rows; ++i) {
            for (size_t j = 0; j < cols; ++j) {
                (*this)[j * rows + i] = copy[i * cols + j];
            }
        }
        return *this;
    }

    packet& write_rle(const std::vector<uint8_t>& v) {
        std::vector<uint8_t>::const_iterator end_it;
        for (auto raw_it = v.begin(); raw_it < v.end(); raw_it = end_it) {
            std::vector<uint8_t>::const_iterator run_it;
            size_t raw, run;
            for (run_it = raw_it, raw = 0; run_it < v.end(); run_it = end_it, raw += run) {
                for (end_it = run_it + 1, run = 1; end_it < v.end(); ++end_it, ++run) {
                    if (*end_it != *run_it) break;
                }
                if (run == 1) {
                    for (; end_it < v.end(); ++end_it, ++run) {
                        if (*end_it != static_cast<uint8_t>(*run_it + run)) break;
                    }
                }
                if (run >= 4 || run >= 2 && raw == 0) {
                    break;
                }
            }
            if (raw > 0) { // Raw Data
                write_var((raw << 2) | 0);
                for (auto it = raw_it; it < run_it; ++it) {
                    write(*it);
                }
            }
            if (run >= 4 || run >= 2 && raw == 0) {
                if (*run_it == *(run_it + 1)) { // Run
                    if (*run_it == 0) { // Zero Run
                        write_var((run << 2) | 1);
                    } else { // Non-Zero Run
                        write_var((run << 2) | 2);
                        write(*run_it);
                    }
                } else { // Sequence
                    write_var((run << 2) | 3);
                    write(*run_it);
                }
            }
        }
        write_var(0); // End

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
        uint8_t byte, shift = 0;
        T result = 0;

        do {
            byte = read<uint8_t>();
            result |= static_cast<T>(byte & 0b01111111) << shift;
            shift += 7;
        } while (byte & 0b10000000);

        return result;
    }

    packet& read(packet& packet) {
        packet.resize(read_var<size_t>());
        for (size_t i = 0; i < packet.size(); i++) {
            packet[i] = at(pos++);
        }
        return packet;
    }

    std::string& read(std::string& string) {
        string.resize(read_var<size_t>());
        for (size_t i = 0; i < string.length(); i++) {
            string[i] = at(pos++);
        }
        return string;
    }

    packet read_rle() {
        packet result;

        while (auto value = read_var<size_t>()) {
            auto type = value & 0b11;
            auto size = value >>= 2;
            switch (type) {
                case 0: { // Raw Data
                    for (size_t i = 0; i < size; i++) {
                        result.write(read<uint8_t>());
                    }
                    break;
                }

                case 1: { // Zero Run
                    for (size_t i = 0; i < size; i++) {
                        result.write<uint8_t>(0);
                    }
                    break;
                }

                case 2: { // Non-Zero Run
                    auto value = read<uint8_t>();
                    for (size_t i = 0; i < size; i++) {
                        result.write(value);
                    }
                    break;
                }

                case 3 : { // Sequence
                    auto value = read<uint8_t>();
                    for (size_t i = 0; i < size; i++) {
                        result.write(value++);
                    }
                    break;
                }
            }
        }

        return result;
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
