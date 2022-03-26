#include "stdafx.h"

#include "util.h"
#include "client_dialog.h"
#include "resource.h"

#define WM_TASK 0x8000

using namespace std;

client_dialog::client_dialog(HMODULE hmod, HWND main_window)
    : hmod(hmod), main_window(main_window), hwndDlg(NULL), thread([=] { gui_thread(); }) {
    initialized.get_future().get();
}

client_dialog::~client_dialog() {
    {
        unique_lock<mutex> lock(mut);
        if (!destroyed) {
            PostMessage(hwndDlg, WM_TASK, (WPARAM) new function<void(void)>([=] { DestroyWindow(hwndDlg); }), NULL);
        }
    }

    if (thread.get_id() != this_thread::get_id()) {
        thread.join();
    } else {
        thread.detach();
    }
}

void client_dialog::set_message_handler(function<void(string)> message_handler) {
    this->message_handler = message_handler;
}

void client_dialog::set_close_handler(function<void(void)> close_handler) {
    this->close_handler = close_handler;
}

void client_dialog::minimize() {
    unique_lock<mutex> lock(mut);
    if (destroyed) return;

    PostMessage(hwndDlg, WM_TASK, (WPARAM) new function<void(void)>([=] {
        ShowWindow(hwndDlg, SW_MINIMIZE);
    }), NULL);
}

void client_dialog::destroy() {
    unique_lock<mutex> lock(mut);
    if (destroyed) return;

    PostMessage(hwndDlg, WM_TASK, (WPARAM) new function<void(void)>([=] {
        DestroyWindow(hwndDlg);
    }), NULL);
}

void client_dialog::info(const string& text) {
    unique_lock<mutex> lock(mut);
    if (destroyed) return;

    PostMessage(hwndDlg, WM_TASK, (WPARAM) new function<void(void)>([=] {
        HWND output_box = GetDlgItem(hwndDlg, IDC_OUTPUT_EDIT);
        SendMessage(output_box, WM_SETREDRAW, FALSE, NULL);

        bool at_bottom = scroll_at_bottom();

        append_timestamp();

        insert_text("[INFO] " + text);

        if (at_bottom) {
            scroll_to_bottom();
        }

        alert_user(false);

        SendMessage(output_box, WM_SETREDRAW, TRUE, NULL);
        RedrawWindow(output_box, NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
    }), NULL);
}

void client_dialog::error(const string& text) {
    unique_lock<mutex> lock(mut);
    if (destroyed) return;

    PostMessage(hwndDlg, WM_TASK, (WPARAM) new function<void(void)>([=] {
        HWND output_box = GetDlgItem(hwndDlg, IDC_OUTPUT_EDIT);
        SendMessage(output_box, WM_SETREDRAW, FALSE, NULL);

        bool at_bottom = scroll_at_bottom();

        append_timestamp();

        insert_text("[ERROR] " + text);

        if (at_bottom) {
            scroll_to_bottom();
        }

        alert_user(true);

        SendMessage(output_box, WM_SETREDRAW, TRUE, NULL);
        RedrawWindow(output_box, NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
    }), NULL);
}

void client_dialog::message(const string& name, const string& message) {
    unique_lock<mutex> lock(mut);
    if (destroyed) return;

    PostMessage(hwndDlg, WM_TASK, (WPARAM) new function<void(void)>([=] {
        HWND output_box = GetDlgItem(hwndDlg, IDC_OUTPUT_EDIT);
        SendMessage(output_box, WM_SETREDRAW, FALSE, NULL);

        bool at_bottom = scroll_at_bottom();

        append_timestamp();

        insert_text(name + ": " + message);

        if (at_bottom) {
            scroll_to_bottom();
        }

        alert_user(false);

        SendMessage(output_box, WM_SETREDRAW, TRUE, NULL);
        RedrawWindow(output_box, NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
    }), NULL);
}

void client_dialog::update_user_list(const vector<vector<string>>& lines) {
    unique_lock<mutex> lock(mut);
    if (destroyed) return;

    PostMessage(hwndDlg, WM_TASK, (WPARAM) new function<void(void)>([=] {
        HWND list = GetDlgItem(hwndDlg, IDC_USER_LIST);

        LVITEM item;
        memset(&item, 0, sizeof(item));

        size_t count = ListView_GetItemCount(list);
        while (count < lines.size()) {
            item.mask = LVIF_TEXT;
            item.cchTextMax = 0;
            item.iItem = 0;
            item.iSubItem = 0;
            item.pszText = 0;
            ListView_InsertItem(list, &item);
            count++;
        }
        while (count > lines.size()) {
            ListView_DeleteItem(list, 0);
            count--;
        }

        wchar_t text[256];
        for (size_t i = 0; i < lines.size(); i++) {
            for (size_t j = 0; j < lines[i].size(); j++) {
                StringCbCopy(text, sizeof(text), utf8_to_wstring(lines[i][j]).c_str());
                ListView_SetItemText(list, i, j, text);
            }
        }

    }), NULL);
}

void client_dialog::update_server_list(const map<string, double>& servers) {
    unique_lock<mutex> lock(mut);
    if (destroyed) return;

    PostMessage(hwndDlg, WM_TASK, (WPARAM) new function<void(void)>([=] {
        HWND list = GetDlgItem(hwndDlg, IDC_SERVER_LIST);

        LVITEM item;
        memset(&item, 0, sizeof(item));

        size_t count = ListView_GetItemCount(list);
        while (count < servers.size()) {
            item.mask = LVIF_TEXT;
            item.cchTextMax = 0;
            item.iItem = 0;
            item.iSubItem = 0;
            item.pszText = 0;
            ListView_InsertItem(list, &item);
            count++;
        }
        while (count > servers.size()) {
            ListView_DeleteItem(list, 0);
        }

        wchar_t text[256];
        int i = 0;
        server_list.clear();
        for (auto& e : servers) {
            size_t split = e.first.find('|');
            string server = e.first.substr(0, split);
            server_list.push_back(server);
            StringCbCopy(text, sizeof(text), utf8_to_wstring(server).c_str());
            ListView_SetItemText(list, i, 0, text);
            if (split != string::npos) {
                StringCbCopy(text, sizeof(text), utf8_to_wstring(e.first.substr(split + 1)).c_str());
                ListView_SetItemText(list, i, 1, text);
            }
            switch ((int)e.second) {
                case SERVER_STATUS_PENDING: StringCbCopy(text, sizeof(text), L""); break;
                case SERVER_STATUS_ERROR: StringCbCopy(text, sizeof(text), L"(Failure)"); break;
                case SERVER_STATUS_VERSION_MISMATCH: StringCbCopy(text, sizeof(text), L"(Wrong Version)"); break;
                case SERVER_STATUS_OUTDATED_CLIENT: StringCbCopy(text, sizeof(text), L"(Outdated Client)"); break;
                case SERVER_STATUS_OUTDATED_SERVER: StringCbCopy(text, sizeof(text), L"(Outdated Server)"); break;
                default: StringCbCopy(text, sizeof(text), utf8_to_wstring(to_string(static_cast<int>(e.second * 1000)) + " ms").c_str()); break;
            }
            ListView_SetItemText(list, i, 2, text);
            i++;
        }
    }), NULL);
}

void client_dialog::gui_thread() {
    HINSTANCE user32 = GetModuleHandle(L"User32.dll");
    if (user32) {
        auto SetThreadDpiAwarenessContext = (int(__stdcall *)(int)) GetProcAddress(user32, "SetThreadDpiAwarenessContext");
        if (SetThreadDpiAwarenessContext) {
            SetThreadDpiAwarenessContext(-2);
        }
        auto GetDpiForSystem = (UINT(__stdcall*)()) GetProcAddress(user32, "GetDpiForSystem");
        if (GetDpiForSystem) {
            dpi = GetDpiForSystem();
        }
    }

    hwndDlg = CreateDialogParam(hmod, MAKEINTRESOURCE(IDD_NETPLAY_DIALOG), NULL, &DialogProc, (LPARAM) this);
    vector<wchar_t> buf(GetWindowTextLength(hwndDlg) + 1);
    GetWindowText(hwndDlg, &buf[0], static_cast<int>(buf.size()));
    original_title = wstring_to_utf8(&buf[0]);

    buf.resize(GetWindowTextLength(main_window) + 1);
    GetWindowText(main_window, &buf[0], static_cast<int>(buf.size()));
    project64z = (wstring_to_utf8(&buf[0]).find("Project64z") != string::npos);

    RECT main_rect, my_rect;
    GetWindowRect(main_window, &main_rect);
    GetWindowRect(hwndDlg, &my_rect);
    SetWindowPos(
        hwndDlg,
        HWND_TOP,
        (main_rect.left + main_rect.right) / 2 - (my_rect.right - my_rect.left) / 2,
        (main_rect.top + main_rect.bottom) / 2 - (my_rect.bottom - my_rect.top) / 2,
        my_rect.right - my_rect.left,
        my_rect.bottom - my_rect.top,
        0
    );

    initialized.set_value(true);

    MSG message;
    while (GetMessage(&message, NULL, 0, 0) > 0) {
        if (!IsDialogMessage(hwndDlg, &message)) {
            TranslateMessage(&message);
            DispatchMessage(&message);
        }
    }
}

bool client_dialog::scroll_at_bottom() {
    SCROLLINFO psi;
    psi.cbSize = sizeof(psi);
    psi.fMask = SIF_ALL;
    if (GetScrollInfo(GetDlgItem(hwndDlg, IDC_OUTPUT_EDIT), SB_VERT, &psi) && psi.nMax - (int)psi.nPage > psi.nTrackPos) {
        return false;
    }

    return true;
}

void client_dialog::scroll_to_bottom() {
    SendMessage(GetDlgItem(hwndDlg, IDC_OUTPUT_EDIT), WM_VSCROLL, MAKELONG(SB_BOTTOM, 0), 0);
}

void client_dialog::select_end() {
    int outLength = GetWindowTextLength(GetDlgItem(hwndDlg, IDC_OUTPUT_EDIT));
    SendMessage(GetDlgItem(hwndDlg, IDC_OUTPUT_EDIT), EM_SETSEL, outLength, outLength);
}

void client_dialog::insert_text(const string& text) {
    SendMessage(GetDlgItem(hwndDlg, IDC_OUTPUT_EDIT), EM_REPLACESEL, 0, (LPARAM)utf8_to_wstring(text).c_str());
}

void client_dialog::append_timestamp() {
    time_t rawtime;
    time(&rawtime);

    auto timeinfo = localtime(&rawtime);

    char buffer[9];
    strftime(buffer, sizeof buffer, "%I:%M:%S", timeinfo);

    select_end();

    if (GetWindowTextLength(GetDlgItem(hwndDlg, IDC_OUTPUT_EDIT)) == 0) {
        insert_text("(" + string(buffer) + ") ");
    } else {
        insert_text("\r\n(" + string(buffer) + ") ");
    }
}

void client_dialog::alert_user(bool force) {
    WINDOWPLACEMENT wp;
    if (!force) {
        wp.length = sizeof(wp);
        GetWindowPlacement(hwndDlg, &wp);
    }

    if (force || wp.showCmd == SW_SHOWMINIMIZED) {
        FLASHWINFO fwi;
        fwi.cbSize = sizeof(fwi);
        fwi.hwnd = hwndDlg;
        fwi.dwFlags = FLASHW_TIMERNOFG | FLASHW_TRAY;
        fwi.uCount = (UINT) -1;
        fwi.dwTimeout = 0;
        FlashWindowEx(&fwi);
    }
}

HWND client_dialog::get_emulator_window() {
    return main_window;
}

bool client_dialog::is_emulator_project64z() {
    return project64z;
}

INT_PTR CALLBACK client_dialog::DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_INITDIALOG: {
            auto dialog = (client_dialog*)lParam;
            SetProp(hwndDlg, L"client_dialog", dialog);

            GetClientRect(hwndDlg, &dialog->window_rect);
            GetWindowRect(GetDlgItem(hwndDlg, IDC_OUTPUT_EDIT), &dialog->output_rect);
            GetWindowRect(GetDlgItem(hwndDlg, IDC_INPUT_EDIT), &dialog->input_rect);
            GetWindowRect(GetDlgItem(hwndDlg, IDC_USER_LIST), &dialog->user_list_rect);
            GetWindowRect(GetDlgItem(hwndDlg, IDC_SERVER_LIST), &dialog->server_list_rect);
            MapWindowPoints(NULL, hwndDlg, (LPPOINT)(&dialog->output_rect), (sizeof(RECT) / sizeof(POINT)));
            MapWindowPoints(NULL, hwndDlg, (LPPOINT)(&dialog->input_rect), (sizeof(RECT) / sizeof(POINT)));
            MapWindowPoints(NULL, hwndDlg, (LPPOINT)(&dialog->user_list_rect), (sizeof(RECT) / sizeof(POINT)));
            MapWindowPoints(NULL, hwndDlg, (LPPOINT)(&dialog->server_list_rect), (sizeof(RECT) / sizeof(POINT)));

            double scale = (dialog->dpi ? dialog->dpi / 96.0 : 1.0);

            HWND user_view = GetDlgItem(hwndDlg, IDC_USER_LIST);

            ListView_SetExtendedListViewStyle(user_view, LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT);

            LVCOLUMN column;
            ZeroMemory(&column, sizeof(column));
            column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

            column.pszText = (LPWSTR)L"#";
            column.cx = (int)(24 * scale);
            ListView_InsertColumn(user_view, 0, &column);

            column.pszText = (LPWSTR)L"Name";
            column.cx = (int)(104 * scale);
            ListView_InsertColumn(user_view, 1, &column);

            column.pszText = (LPWSTR)L"Ping";
            column.cx = (int)(48 * scale);
            ListView_InsertColumn(user_view, 2, &column);

            column.pszText = (LPWSTR)L"Lag";
            column.cx = (int)(36 * scale);
            ListView_InsertColumn(user_view, 3, &column);

            column.pszText = (LPWSTR)L"1P";
            column.cx = (int)(32 * scale);
            ListView_InsertColumn(user_view, 4, &column);

            column.pszText = (LPWSTR)L"2P";
            column.cx = (int)(32 * scale);
            ListView_InsertColumn(user_view, 5, &column);

            column.pszText = (LPWSTR)L"3P";
            column.cx = (int)(32 * scale);
            ListView_InsertColumn(user_view, 6, &column);

            column.pszText = (LPWSTR)L"4P";
            column.cx = (int)(32 * scale);
            ListView_InsertColumn(user_view, 7, &column);

            HWND server_view = GetDlgItem(hwndDlg, IDC_SERVER_LIST);

            ListView_SetExtendedListViewStyle(server_view, LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT);

            column.pszText = (LPWSTR)L"Server";
            column.cx = (int)(128 * scale);
            ListView_InsertColumn(server_view, 0, &column);

            column.pszText = (LPWSTR)L"Location";
            column.cx = (int)(112 * scale);
            ListView_InsertColumn(server_view, 1, &column);

            column.pszText = (LPWSTR)L"Ping";
            column.cx = (int)(96 * scale);
            ListView_InsertColumn(server_view, 2, &column);

            return TRUE;
        }

        case WM_SIZE: {
            auto dialog = (client_dialog*)GetProp(hwndDlg, L"client_dialog");
            if (dialog) {
                int x, y, w, h;
                RECT rect;
                GetClientRect(hwndDlg, &rect);

                x = dialog->output_rect.left;
                y = dialog->output_rect.top;
                w = dialog->output_rect.right - dialog->output_rect.left;
                h = dialog->output_rect.bottom - dialog->output_rect.top;
                w += rect.right - dialog->window_rect.right;
                h += rect.bottom - dialog->window_rect.bottom;
                MoveWindow(GetDlgItem(hwndDlg, IDC_OUTPUT_EDIT), x, y, w, h, TRUE);

                x = dialog->input_rect.left;
                y = dialog->input_rect.top;
                w = dialog->input_rect.right - dialog->input_rect.left;
                h = dialog->input_rect.bottom - dialog->input_rect.top;
                w += rect.right - dialog->window_rect.right;
                y += rect.bottom - dialog->window_rect.bottom;
                MoveWindow(GetDlgItem(hwndDlg, IDC_INPUT_EDIT), x, y, w, h, TRUE);

                x = dialog->user_list_rect.left;
                y = dialog->user_list_rect.top;
                w = dialog->user_list_rect.right - dialog->user_list_rect.left;
                h = dialog->user_list_rect.bottom - dialog->user_list_rect.top;
                x += rect.right - dialog->window_rect.right;
                h += rect.bottom - dialog->window_rect.bottom;
                MoveWindow(GetDlgItem(hwndDlg, IDC_USER_LIST), x, y, w, h, TRUE);

                x = dialog->server_list_rect.left;
                y = dialog->server_list_rect.top;
                w = dialog->server_list_rect.right - dialog->server_list_rect.left;
                h = dialog->server_list_rect.bottom - dialog->server_list_rect.top;
                x += rect.right - dialog->window_rect.right;
                y += rect.bottom - dialog->window_rect.bottom;
                MoveWindow(GetDlgItem(hwndDlg, IDC_SERVER_LIST), x, y, w, h, TRUE);
            }
            return 0;
        }

        case WM_CLOSE: {
            auto dialog = (client_dialog*)GetProp(hwndDlg, L"client_dialog");
            if (dialog) {
                if (dialog->close_handler) {
                    dialog->close_handler();
                } else {
                    DestroyWindow(hwndDlg);
                }
            }
            break;
        }

        case WM_DESTROY: {
            auto dialog = (client_dialog*)GetProp(hwndDlg, L"client_dialog");
            if (dialog) {
                unique_lock<mutex> lock(dialog->mut);
                dialog->destroyed = true;
            }
            PostQuitMessage(0);
            break;
        }

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDOK: {
                    wchar_t buffer[1024];
                    Edit_GetText(GetDlgItem(hwndDlg, IDC_INPUT_EDIT), buffer, 1024);
                    string message = wstring_to_utf8(buffer);
                    if (message.empty()) return TRUE;

                    auto dialog = (client_dialog*)GetProp(hwndDlg, L"client_dialog");
                    if (dialog) {
                        if (dialog->message_handler) {
                            dialog->message_handler(wstring_to_utf8(buffer));
                            Edit_SetText(GetDlgItem(hwndDlg, IDC_INPUT_EDIT), L"");
                        }
                    }
                    return TRUE;
                }
            }
            break;

        case WM_NOTIFY: {
            NMHDR* nm = (NMHDR*)lParam;
            switch (nm->idFrom) {
                case IDC_SERVER_LIST: {
                    switch (nm->code) {
                        case NM_DBLCLK: {
                            NMITEMACTIVATE* nmia = (NMITEMACTIVATE*)lParam;
                            auto dialog = (client_dialog*)GetProp(hwndDlg, L"client_dialog");
                            if (dialog) {
                                if (nmia->iItem < 0 || nmia->iItem >= (int)dialog->server_list.size()) {
                                    break;
                                }
#ifdef DEBUG
                                dialog->message_handler("/join " + dialog->server_list[nmia->iItem] + "/test");
#else
                                dialog->message_handler("/join " + dialog->server_list[nmia->iItem]);
#endif
                            }
                            break;
                        }
                    }
                    break;
                }
            }
            break;
        }

        case WM_CTLCOLORSTATIC:
            if ((HWND)lParam == GetDlgItem(hwndDlg, IDC_OUTPUT_EDIT)) {
                return (LRESULT)((HBRUSH)GetStockObject(WHITE_BRUSH));
            }
            break;

        case WM_TASK: {
            auto task = (function<void(void)>*) wParam;
            (*task)();
            delete task;
            return TRUE;
        }
    }

    return FALSE;
}
