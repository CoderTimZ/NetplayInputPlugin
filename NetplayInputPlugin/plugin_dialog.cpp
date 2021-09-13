#include "stdafx.h"

#include "plugin_dialog.h"
#include "util.h"
#include "resource.h"

using namespace std;

plugin_dialog::plugin_dialog(HMODULE this_dll, HWND parent, const string& search_location, const string& plugin_dll, CONTROL_INFO control_info)
    : search_location(search_location), plugin_dll(plugin_dll), control_info(control_info) {
    dialog = this;
    ok = (DialogBox(this_dll, MAKEINTRESOURCE(IDD_SELECT_PLUGIN_DIALOG), parent, &DialogProc) == IDOK);
}

plugin_dialog::~plugin_dialog() {
    dialog = NULL;
}

const string& plugin_dialog::get_plugin_dll() {
    return plugin_dll;
}

bool plugin_dialog::ok_clicked() {
    return ok;
}

void plugin_dialog::populate_plugin_map() {
    string search_path = search_location + "*.dll";

    WIN32_FIND_DATA findFileData;
    HANDLE hFind;
    if ((hFind = FindFirstFile(utf8_to_wstring(search_path).c_str(), &findFileData)) != INVALID_HANDLE_VALUE) {
        do {
            if (!(findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                string plugin_path = search_location + wstring_to_utf8(findFileData.cFileName);

                try {
                    input_plugin plugin(plugin_path);
                    plugins[plugin.info.Name] = wstring_to_utf8(findFileData.cFileName);
                } catch(const exception&) { }
            }
        } while (FindNextFile(hFind, &findFileData));
    }
}

void plugin_dialog::populate_combo(HWND combo) {
    int index = -1;

    int i = 0;
    for (auto& e : plugins) {
        ComboBox_AddString(combo, utf8_to_wstring(e.first).c_str());

        if (e.second == plugin_dll) {
            index = i;
        }

        i++;
    }

    if (index != -1) {
        ComboBox_SetCurSel(combo, index);
    }
}

void plugin_dialog::combo_selection_changed(HWND hwndDlg) {
    plugin.reset();

    HWND combo  = GetDlgItem(hwndDlg, IDC_COMBO_PLUGINS);
    HWND about  = GetDlgItem(hwndDlg, IDC_ABOUT_BUTTON);
    HWND test   = GetDlgItem(hwndDlg, IDC_TEST_BUTTON);
    HWND config = GetDlgItem(hwndDlg, IDC_CONFIG_BUTTON);
    HWND ok     = GetDlgItem(hwndDlg, IDOK);

    wchar_t plugin_name_unicode[sizeof PLUGIN_INFO::Name];
    ComboBox_GetText(combo, plugin_name_unicode, sizeof PLUGIN_INFO::Name);

    string pnu = wstring_to_utf8(plugin_name_unicode);
    string pn;
    pn.assign(pnu.begin(), pnu.end());

    string plugin_dll = plugins[pn];

    try {
        plugin = make_shared<input_plugin>(search_location + plugin_dll);
        plugin->initiate_controllers(control_info);

        Button_Enable(ok,     TRUE);
        Button_Enable(about,  plugin->DllAbout  != NULL);
        Button_Enable(test,   plugin->DllTest   != NULL);
        Button_Enable(config, plugin->DllConfig != NULL);
    } catch (const exception&) {
        Button_Enable(ok,     FALSE);
        Button_Enable(about,  FALSE);
        Button_Enable(test,   FALSE);
        Button_Enable(config, FALSE);
    }
}

plugin_dialog* plugin_dialog::dialog = NULL;

/**** STATIC ****/

INT_PTR CALLBACK plugin_dialog::DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_INITDIALOG:
            dialog->populate_plugin_map();
            dialog->populate_combo(GetDlgItem(hwndDlg, IDC_COMBO_PLUGINS));
            dialog->combo_selection_changed(hwndDlg);
            return TRUE;

        case WM_COMMAND:
            switch (wParam) {
                case MAKEWPARAM(IDC_COMBO_PLUGINS, CBN_SELCHANGE):
                    dialog->combo_selection_changed(hwndDlg);
                    return TRUE;

                case IDC_TEST_BUTTON:
                    dialog->plugin->DllTest(hwndDlg);
                    return TRUE;

                case IDC_ABOUT_BUTTON:
                    dialog->plugin->DllAbout(hwndDlg);
                    return TRUE;

                case IDC_CONFIG_BUTTON:
                    dialog->plugin->DllConfig(hwndDlg);
                    return TRUE;

                case IDOK: {
                    wchar_t plugin_name_unicode[sizeof PLUGIN_INFO::Name];
                    ComboBox_GetText(GetDlgItem(hwndDlg, IDC_COMBO_PLUGINS), plugin_name_unicode, sizeof PLUGIN_INFO::Name);

                    string pnu = wstring_to_utf8(plugin_name_unicode);
                    string pn;
                    pn.assign(pnu.begin(), pnu.end());

                    dialog->plugin_dll = dialog->plugins[pn];
                    dialog->plugin.reset();

                    EndDialog(hwndDlg, IDOK);
                    return TRUE;
                }

                case IDCANCEL:
                    dialog->plugin.reset();
                    EndDialog(hwndDlg, IDCANCEL);
                    return TRUE;
            }
            break;
    }

    return FALSE;
}
