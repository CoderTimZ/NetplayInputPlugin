#pragma once

#include "stdafx.h"

template<class T> class blocking_queue {
    public:
        size_t size() {
            std::unique_lock<std::mutex> lock(mut);

            return my_queue.size();
        }

        void push(const T& element) {
            {
                std::unique_lock<std::mutex> lock(mut);

                if (error_flag) {
                    throw my_error;
                }

                my_queue.push_back(element);
            }

            ready.notify_one();
        }

        const T pop() {
            std::unique_lock<std::mutex> lock(mut);

            ready.wait(lock, [=] { return !my_queue.empty() || error_flag; });

            if (error_flag) {
                throw my_error;
            }

            T element = my_queue.front();
            my_queue.pop_front();

            return element;
        }

        void error(const std::exception& e) {
            {
                std::unique_lock<std::mutex> lock(mut);

                my_queue.clear();
                my_error = e;
                error_flag = true;
            }

            ready.notify_one();
        }

    private:
        std::mutex mut;
        std::condition_variable ready;
        std::list<T> my_queue;
        std::exception my_error;
        bool error_flag = false;
};
