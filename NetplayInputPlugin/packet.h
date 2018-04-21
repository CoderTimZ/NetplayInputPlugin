#pragma once

#include <vector>
#include <stdint.h>

class packet {
    public:
        packet() { }

        packet(unsigned int size) : my_data(size) { }

        template<typename T> void write(const std::vector<T>& data) {
            my_data.reserve(my_data.size() + sizeof(T) * data.size());

            for (int i = 0; i < data.size(); i++) {
                write(data[i]);
            }
        }

        void write(const packet& p) {
            write(p.data());
        }

        template<typename T> void write(const T& data) {
            const uint8_t* data_array = (const uint8_t*) &data;

            for (int i = 0; i < sizeof(T); i++) {
                my_data.push_back(data_array[i]);
            }
        }

        template<typename T> packet& operator<<(const T& data) {
            write(data);

            return *this;
        }

        template<typename T> packet& operator+(const T& data) {
            return packet(*this) << data;
        }

        template<typename T> void read(T& data) {
            assert(sizeof(T) <= size());

            uint8_t* data_array = (uint8_t*) &data;

            for (int i = 0; i < sizeof(T); i++) {
                data_array[i] = my_data[i];
            }

            my_data.erase(my_data.begin(), my_data.begin() + sizeof(T));
        }

        template<typename T> packet& operator>>(T& data) {
            read(data);

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

        size_t size() const {
            return my_data.size();
        }

        void clear() {
            return data().clear();
        }

    private:
        std::vector<uint8_t> my_data;
};
