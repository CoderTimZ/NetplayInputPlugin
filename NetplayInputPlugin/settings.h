#pragma once

#include "stdafx.h"

class settings {
    public:
        settings(const std::string& settings_path);
        const std::string& get_name() const;
        const std::string& get_plugin_dll() const;
        void set_plugin_dll(const std::string& plugin_dll);
        void set_name(const std::string& name);
        void save();
    protected:
    private:
        settings();

        std::string settings_path;
        std::string plugin_dll;
        std::string name;
};
