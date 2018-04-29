#pragma once

#include "stdafx.h"

template<class T> class blocking_queue {
    public:
        void push(const T& element) {
            {
                std::unique_lock<std::mutex> lock(mut);

                if (error) throw *error;
                my_queue.push_back(element);
            }

            ready.notify_one();
        }

        const T pop() {
            std::unique_lock<std::mutex> lock(mut);

            ready.wait(lock, [=] { return !my_queue.empty() || error; });

            if (error) throw *error;

            T element = my_queue.front();
            my_queue.pop_front();

            return element;
        }

        void interrupt(const std::runtime_error& e) {
            {
                std::unique_lock<std::mutex> lock(mut);
                error = make_unique<std::runtime_error>(e);
            }

            ready.notify_all();
        }

        void reset() {
            std::unique_lock<std::mutex> lock(mut);
            my_queue.clear();
            error.reset();
        }

    private:
        std::list<T> my_queue;
        std::unique_ptr<std::runtime_error> error;
        std::mutex mut;
        std::condition_variable ready;
};
