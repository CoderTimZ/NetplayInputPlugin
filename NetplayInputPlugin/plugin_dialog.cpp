#include "plugin_dialog.h"
#include "resource.h"

#include <windowsx.h>

using namespace std;

plugin_dialog::plugin_dialog(HMODULE this_dll, HWND parent, const wstring& search_location, const wstring& plugin_dll, HWND main_window)
    : search_location(search_location), plugin_dll(plugin_dll), main_window(main_window), plugin(NULL) {
    dialog = this;
    ok = (DialogBox(this_dll, MAKEINTRESOURCE(IDD_SELECT_PLUGIN_DIALOG), parent, &DialogProc) == IDOK);
}

plugin_dialog::~plugin_dialog() {
    dialog = NULL;
}

const wstring& plugin_dialog::get_plugin_dll() {
    return plugin_dll;
}

bool plugin_dialog::ok_clicked() {
    return ok;
}

void plugin_dialog::populate_plugin_map() {
    wstring search_path = search_location + L"*.dll";

    WIN32_FIND_DATA findFileData;
    HANDLE hFind;
    if ((hFind = FindFirstFile(search_path.c_str(), &findFileData)) != INVALID_HANDLE_VALUE) {
        do {
            if (!(findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                wstring plugin_path = search_location + findFileData.cFileName;

                try {
                    input_plugin plugin(plugin_path);
                    plugins[plugin.PluginInfo.Name] = findFileData.cFileName;
                } catch(const exception&) { }
            }
        } while (FindNextFile(hFind, &findFileData));
    }
}

void plugin_dialog::populate_combo(HWND combo) {
    int index = -1;

    int i = 0;
    for (auto it = plugins.begin(); it != plugins.end(); it++) {
        string plugin_ascii = it->first;
        wstring plugin;
        plugin.assign(plugin_ascii.begin(), plugin_ascii.end());
        ComboBox_AddString(combo, plugin.c_str());

        if (it->second == plugin_dll) {
            index = i;
        }

        i++;
    }

    if (index != -1) {
        ComboBox_SetCurSel(combo, index);
    }
}

void plugin_dialog::combo_selection_changed(HWND hwndDlg) {
    if (plugin != NULL) {
        delete plugin;
        plugin = NULL;
    }

    HWND combo  = GetDlgItem(hwndDlg, IDC_COMBO_PLUGINS);
    HWND about  = GetDlgItem(hwndDlg, IDC_ABOUT_BUTTON);
    HWND test   = GetDlgItem(hwndDlg, IDC_TEST_BUTTON);
    HWND config = GetDlgItem(hwndDlg, IDC_CONFIG_BUTTON);
    HWND ok     = GetDlgItem(hwndDlg, IDOK);

    wchar_t plugin_name_unicode[PLUGIN_NAME_LENGTH];
    ComboBox_GetText(combo, plugin_name_unicode, PLUGIN_NAME_LENGTH);

    wstring pnu = plugin_name_unicode;
    string pn;
    pn.assign(pnu.begin(), pnu.end());

    wstring plugin_dll = plugins[pn];

    try {
        plugin = new input_plugin(search_location + plugin_dll);

        plugin->InitiateControllers0100(main_window, plugin->controls);

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
                    wchar_t plugin_name_unicode[PLUGIN_NAME_LENGTH];
                    ComboBox_GetText(GetDlgItem(hwndDlg, IDC_COMBO_PLUGINS), plugin_name_unicode, PLUGIN_NAME_LENGTH);

                    wstring pnu = plugin_name_unicode;
                    string pn;
                    pn.assign(pnu.begin(), pnu.end());

                    dialog->plugin_dll = dialog->plugins[pn];

                    if (dialog->plugin != NULL) {
                        delete dialog->plugin;
                        dialog->plugin = NULL;
                    }

                    EndDialog(hwndDlg, IDOK);
                    return TRUE;
                }

                case IDCANCEL:
                    if (dialog->plugin != NULL) {
                        delete dialog->plugin;
                        dialog->plugin = NULL;
                    }

                    EndDialog(hwndDlg, IDCANCEL);
                    return TRUE;
            }
            break;
    }

    return FALSE;
}
