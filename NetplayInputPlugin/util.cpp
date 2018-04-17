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
