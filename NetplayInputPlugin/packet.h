#pragma once

#include <vector>
#include <cstdint>

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
            static_assert(std::is_integral<T>::value, "Integral required.");
            static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8, "1, 2, 4, or 8 byte value required.");

            if (sizeof(T) == 1) {
                my_data.push_back(static_cast<uint8_t>(value));
            } else if (sizeof(T) == 2) {
                my_data.push_back((static_cast<uint16_t>(value) >> 0x08) & 0xFF);
                my_data.push_back((static_cast<uint16_t>(value) >> 0x00) & 0xFF);
            } else if (sizeof(T) == 4) {
                my_data.push_back((static_cast<uint32_t>(value) >> 0x18) & 0xFF);
                my_data.push_back((static_cast<uint32_t>(value) >> 0x10) & 0xFF);
                my_data.push_back((static_cast<uint32_t>(value) >> 0x08) & 0xFF);
                my_data.push_back((static_cast<uint32_t>(value) >> 0x00) & 0xFF);
            } else if (sizeof(T) == 8) {
                my_data.push_back((static_cast<uint64_t>(value) >> 0x38) & 0xFF);
                my_data.push_back((static_cast<uint64_t>(value) >> 0x30) & 0xFF);
                my_data.push_back((static_cast<uint64_t>(value) >> 0x28) & 0xFF);
                my_data.push_back((static_cast<uint64_t>(value) >> 0x20) & 0xFF);
                my_data.push_back((static_cast<uint64_t>(value) >> 0x18) & 0xFF);
                my_data.push_back((static_cast<uint64_t>(value) >> 0x10) & 0xFF);
                my_data.push_back((static_cast<uint64_t>(value) >> 0x08) & 0xFF);
                my_data.push_back((static_cast<uint64_t>(value) >> 0x00) & 0xFF);
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
            static_assert(std::is_integral<T>::value, "Integral required.");
            static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8, "1, 2, 4, or 8 byte value required.");
            assert(sizeof(T) <= my_data.end() - read_pos);
            
            T value = 0;

            if (sizeof(T) == 1) {
                value = static_cast<T>(*read_pos++);
            } else if (sizeof(T) == 2) {
                value |= static_cast<uint16_t>(*read_pos++) << 0x08;
                value |= static_cast<uint16_t>(*read_pos++) << 0x00;
            } else if (sizeof(T) == 4) {
                value |= static_cast<uint32_t>(*read_pos++) << 0x18;
                value |= static_cast<uint32_t>(*read_pos++) << 0x10;
                value |= static_cast<uint32_t>(*read_pos++) << 0x08;
                value |= static_cast<uint32_t>(*read_pos++) << 0x00;
            } else if (sizeof(T) == 8) {
                value |= static_cast<uint64_t>(*read_pos++) << 0x38;
                value |= static_cast<uint64_t>(*read_pos++) << 0x30;
                value |= static_cast<uint64_t>(*read_pos++) << 0x28;
                value |= static_cast<uint64_t>(*read_pos++) << 0x20;
                value |= static_cast<uint64_t>(*read_pos++) << 0x18;
                value |= static_cast<uint64_t>(*read_pos++) << 0x10;
                value |= static_cast<uint64_t>(*read_pos++) << 0x08;
                value |= static_cast<uint64_t>(*read_pos++) << 0x00;
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

        void clear() {
            return data().clear();
        }

    private:
        std::vector<uint8_t>::const_iterator read_pos = my_data.begin();
};
