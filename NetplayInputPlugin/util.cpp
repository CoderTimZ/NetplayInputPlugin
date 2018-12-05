#include "stdafx.h"

#include "util.h"

using namespace std;

wstring utf8_to_wstring(const string& str) {
    return wstring_convert<codecvt_utf8<wchar_t>>().from_bytes(str);
}

std::string wstring_to_utf8(const wstring& str) {
    return wstring_convert<codecvt_utf8<wchar_t>>().to_bytes(str);
}
