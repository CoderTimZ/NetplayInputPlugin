#pragma once

#include <string>
#include <windows.h>

std::wstring widen(const std::string& s);
std::string narrow(const std::wstring& ws);
uint64_t get_time();
