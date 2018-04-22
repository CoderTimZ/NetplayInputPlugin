#include "util.h"

using namespace std;

wstring widen(const string& s) {
    wstring ws;
    ws.assign(s.begin(), s.end());
    return ws;
}

string narrow(const wstring& ws) {
    string s;
    s.assign(ws.begin(), ws.end());
    return s;
}

uint64_t get_time() {
    static struct freq {
        LARGE_INTEGER value;
        freq() { QueryPerformanceFrequency(&value); }
    } freq;
    
    LARGE_INTEGER time;
    QueryPerformanceCounter(&time);
    return time.QuadPart * 1000 / freq.value.QuadPart;
}