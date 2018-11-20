#pragma once

#include "stdafx.h"

template<class T>
class blocking_queue {
    public:
        size_t size() {
            std::unique_lock<std::mutex> lock(mut);
            return my_queue.size();
        }

        void push(const T& element) {
            {
                std::unique_lock<std::mutex> lock(mut);
                my_queue.push_back(element);
            }

            ready.notify_one();
        }

        const T pop() {
            std::unique_lock<std::mutex> lock(mut);

            ready.wait(lock, [=] { return !my_queue.empty(); });

            T element = my_queue.front();
            my_queue.pop_front();

            return element;
        }

    private:
        std::mutex mut;
        std::condition_variable ready;
        std::list<T> my_queue;
};
