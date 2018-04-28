#pragma once

#include "stdafx.h"

#include "message_exception.h"

template<class T> class blocking_queue {
    public:
        blocking_queue() : stopped(false), e("No errors.") { }

        size_t size() {
            std::unique_lock<std::mutex> lock(mut);

            if (stopped) throw e;
            return my_queue.size();
        }

        void push(const T& element) {
            std::unique_lock<std::mutex> lock(mut);

            if (stopped) throw e;
            my_queue.push_back(element);

            ready.notify_one();
        }

        const T pop() {
            std::unique_lock<std::mutex> lock(mut);

            ready.wait(lock, [=] { return !my_queue.empty() || stopped; });

            if (stopped) throw e;

            T element = my_queue.front();
            my_queue.pop_front();

            return element;
        }

        void interrupt(const message_exception& e) {
            std::unique_lock<std::mutex> lock(mut);

            if (!stopped) {
                stopped = true;
                this->e = e;
                ready.notify_one();
            }
        }

    private:
        std::list<T> my_queue;
        std::mutex mut;
        std::condition_variable ready;
        bool stopped;
        message_exception e;
};
