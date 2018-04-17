#include "message_exception.h"

using namespace std;

message_exception::message_exception(const string& message) throw () {
    this->message = message;
}

message_exception::message_exception(const exception& e) throw () {
    this->message = e.what();
}

message_exception::~message_exception() throw () { }

const char* message_exception::what() const throw() {
    return message.c_str();
}
