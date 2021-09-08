#include "stdafx.h"

#include "settings.h"
#include "util.h"

using namespace std;

settings::settings(const string& settings_path) : settings_path(settings_path) {
    wchar_t plugin_dll[MAX_PATH];
    GetPrivateProfileString(L"plugin", L"dll", L"PJ64_NRage.dll", plugin_dll, MAX_PATH, utf8_to_wstring(settings_path).c_str());
    this->plugin_dll = wstring_to_utf8(plugin_dll);

    wchar_t name[256];
    GetPrivateProfileString(L"user", L"name", L"Anonymous", name, 256, utf8_to_wstring(settings_path).c_str());
    this->name = wstring_to_utf8(name);
}

const string& settings::get_plugin_dll() const {
    return plugin_dll;
}

const string& settings::get_name() const {
    return name;
}

void settings::set_plugin_dll(const string& plugin_dll) {
    this->plugin_dll = plugin_dll;
}

void settings::set_name(const string& name) {
    this->name = name;
}

void settings::save() {
    WritePrivateProfileString(L"plugin", L"dll", utf8_to_wstring(plugin_dll).c_str(), utf8_to_wstring(settings_path).c_str());
    WritePrivateProfileString(L"user",   L"name", utf8_to_wstring(name).c_str(), utf8_to_wstring(settings_path).c_str());
}
