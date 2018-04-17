#include "settings.h"

#include <windows.h>

using namespace std;

settings::settings(const wstring& settings_path) : settings_path(settings_path) {
    wchar_t plugin_dll[MAX_PATH];
    GetPrivateProfileString(L"plugin", L"dll", L"Jabo_DInput.dll", plugin_dll, MAX_PATH, settings_path.c_str());
    this->plugin_dll = plugin_dll;

    wchar_t name[256];
    GetPrivateProfileString(L"user", L"name", L"no-name", name, 256, settings_path.c_str());
    this->name = name;
}

const wstring& settings::get_plugin_dll() const {
    return plugin_dll;
}

const wstring& settings::get_name() const {
    return name;
}

void settings::set_plugin_dll(const wstring& plugin_dll) {
    this->plugin_dll = plugin_dll;
}

void settings::set_name(const wstring& name) {
    this->name = name;
}

void settings::save() {
    WritePrivateProfileString(L"plugin", L"dll",  plugin_dll.c_str(), settings_path.c_str());
    WritePrivateProfileString(L"user",   L"name", name.c_str(),       settings_path.c_str());
}
