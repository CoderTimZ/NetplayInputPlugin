#include "stdafx.h"

#include "common.h"

using namespace std;
using namespace asio;

double timestamp() {
    using namespace std::chrono;

    return duration_cast<microseconds>(high_resolution_clock::now().time_since_epoch()).count() / 1000000.0;
}

void log(const string& message) {
    log(cout, message);
}

void log(ostream& stream, const string& message) {
    static mutex mut;

    time_t rawtime;
    time(&rawtime);

    unique_lock<mutex> lk(mut);

    auto timeinfo = localtime(&rawtime);

    char timestr[26];
    strftime(timestr, sizeof timestr, "%F %T %z", timeinfo);

    stream << "(" << timestr << ") " << message << endl;
}

string& ltrim(string& str) {
    auto it = find_if(str.begin(), str.end(), [](char ch) { return !isspace<char>(ch, locale::classic()); });
    str.erase(str.begin(), it);
    return str;
}

string& rtrim(string& str) {
    auto it = find_if(str.rbegin(), str.rend(), [](char ch) { return !isspace<char>(ch, locale::classic()); });
    str.erase(it.base(), str.end());
    return str;
}

string& trim(std::string& str) {
    return ltrim(rtrim(str));
}

#ifdef __GNUC__
void print_stack_trace() {
    void *array[10];
    size_t size;
    size = backtrace(array, 10);
    backtrace_symbols_fd(array, size, STDERR_FILENO);
}
#endif
