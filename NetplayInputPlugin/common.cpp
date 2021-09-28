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

bool is_private_address(const asio::ip::address& address) {
    if (address.is_v4()) {
        return (address.to_v4().to_uint() & 0xFF000000) == 0x0A000000  // 10.0.0.0/8
            || (address.to_v4().to_uint() & 0xFFF00000) == 0xAC100000  // 172.16.0.0/12
            || (address.to_v4().to_uint() & 0xFFFF0000) == 0xC0A80000  // 192.168.0.0/16
            || (address.to_v4().to_uint() & 0xFFFF0000) == 0xA9FE0000; // 169.254.0.0/16
    } else if (address.is_v6()) {
        return (address.to_v6().is_link_local())
            || (address.to_v6().to_bytes()[0] & 0xFE) == 0xFC;         // fc00::/7
    }
    return false;
}

#ifdef __GNUC__
void print_stack_trace() {
    void *array[10];
    size_t size;
    size = backtrace(array, 10);
    backtrace_symbols_fd(array, size, STDERR_FILENO);
}
#endif
