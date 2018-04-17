#pragma once

#include <string>

class settings {
    public:
        settings(const std::wstring& settings_path);
        const std::wstring& get_name() const;
        const std::wstring& get_plugin_dll() const;
        void set_plugin_dll(const std::wstring& plugin_dll);
        void set_name(const std::wstring& name);
        void save();
    protected:
    private:
        settings();

        std::wstring settings_path;

        std::wstring plugin_dll;
        std::wstring name;
};
