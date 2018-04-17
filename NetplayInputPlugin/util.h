#pragma once

#include <string>

std::wstring widen(const std::string& s);
std::string narrow(const std::wstring& ws);
