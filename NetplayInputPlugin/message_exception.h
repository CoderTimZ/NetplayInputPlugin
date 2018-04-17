#pragma once

#include <exception>
#include <string>

class message_exception : public std::exception {
    public:
        message_exception(const std::string& message) throw ();
        message_exception(const std::exception& e) throw ();
        virtual ~message_exception() throw ();
        virtual const char* what() const throw();
    protected:
    private:
        message_exception();

        std::string message;
};
