#pragma once

#include <list>
#include <boost/thread.hpp>

#include "message_exception.h"

template<class T> class blocking_queue {
    public:
        blocking_queue() : stop(false), e("No errors.") { }

        void push(const T& element) {
            boost::mutex::scoped_lock lock(mut);

            if (stop) {
                throw e;
            }

            my_queue.push_back(element);

            ready.notify_one();
        }

        const T pop() {
            boost::mutex::scoped_lock lock(mut);

            while (!stop && my_queue.empty()) {
                ready.wait(lock);
            }

            if (stop) {
                throw e;
            }

            T element = my_queue.front();
            my_queue.pop_front();

            return element;
        }

        void interrupt(const message_exception& e) {
            boost::mutex::scoped_lock lock(mut);

            if (!stop) {
                stop = true;
                this->e = e;

                ready.notify_one();
            }
        }

    private:
        std::list<T> my_queue;
        bool stop;
        boost::mutex mut;
        boost::condition_variable ready;
        message_exception e;
};
