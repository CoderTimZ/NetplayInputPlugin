#pragma once

#include "stdafx.h"

enum {
    ID_SYSTEM_LIMITFPS_ON = 4900,
    ID_SYSTEM_LIMITFPS_OFF
};

std::wstring utf8_to_wstring(const std::string& str);
std::string wstring_to_utf8(const std::wstring& str);
