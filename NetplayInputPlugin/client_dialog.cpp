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

        SendMessage(list, WM_SETREDRAW, FALSE, 0);

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

        SendMessage(list, WM_SETREDRAW, TRUE, 0);
        RedrawWindow(list, NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
    }), NULL);
}

void client_dialog::update_server_list(const map<string, double>& servers) {
    unique_lock<mutex> lock(mut);
    if (destroyed) return;

    PostMessage(hwndDlg, WM_TASK, (WPARAM) new function<void(void)>([=] {
        HWND list = GetDlgItem(hwndDlg, IDC_SERVER_LIST);

        SendMessage(list, WM_SETREDRAW, FALSE, 0);

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
            size_t index = e.first.find('|');
            string address = e.first.substr(0, index);
            server_list.push_back(address);
            StringCbCopy(text, sizeof(text), utf8_to_wstring(address).c_str());
            ListView_SetItemText(list, i, 0, text);
            if (index != string::npos) {
                index++;
                StringCbCopy(text, sizeof(text), utf8_to_wstring(e.first.substr(index, e.first.find('|', index))).c_str());
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

        SendMessage(list, WM_SETREDRAW, TRUE, 0);
        RedrawWindow(list, NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
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

    CreateDialogParam(hmod, MAKEINTRESOURCE(IDD_NETPLAY_DIALOG), NULL, &DialogProc, (LPARAM) this);
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

void client_dialog::set_window_scale(HWND hwnd, const float_rect& scale) {
    window_layout layout;
    layout.hwnd = hwnd;
    GetWindowRect(hwnd, &layout.initial);
    MapWindowPoints(NULL, hwndDlg, (LPPOINT)&layout.initial, sizeof(RECT) / sizeof(POINT));
    layout.scale = scale;
    window_layouts.push_back(layout);
}

void client_dialog::set_column_scale(HWND hwnd, const std::vector<int>& widths) {
    column_layout layout;
    layout.hwnd = hwnd;
    layout.widths = widths;
    column_layouts.push_back(layout);
}

void client_dialog::scale_windows() {
    RedrawWindow(hwndDlg, NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);

    RECT rect;
    GetClientRect(hwndDlg, &rect);
    int dx = rect.right - initial_rect.right;
    int dy = rect.bottom - initial_rect.bottom;
    for (auto& layout : window_layouts) {
        int dl = (int)roundf(dx * layout.scale.l);
        int dt = (int)roundf(dy * layout.scale.t);
        int dr = (int)roundf(dx * layout.scale.r);
        int db = (int)roundf(dy * layout.scale.b);
        MoveWindow(
            layout.hwnd,
            layout.initial.left + dl,
            layout.initial.top + dt,
            layout.initial.right + dr - layout.initial.left - dl,
            layout.initial.bottom + db - layout.initial.top - dt,
            TRUE
        );
    }

    SendMessage(hwndDlg, WM_SETREDRAW, TRUE, NULL);
    RedrawWindow(hwndDlg, NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
}

void client_dialog::scale_columns() {
    for (auto& layout : column_layouts) {
        SendMessage(layout.hwnd, WM_SETREDRAW, FALSE, 0);
        RECT rect;
        GetClientRect(layout.hwnd, &rect);
        float scale = (float)rect.right / std::accumulate(layout.widths.begin(), layout.widths.end(), 0);
        for (size_t i = 0; i < layout.widths.size(); i++) {
            ListView_SetColumnWidth(layout.hwnd, i, roundf(layout.widths[i] * scale));
        }
        ListView_SetColumnWidth(layout.hwnd, layout.widths.size() - 1, LVSCW_AUTOSIZE_USEHEADER);
        SendMessage(layout.hwnd, WM_SETREDRAW, TRUE, 0);
        RedrawWindow(layout.hwnd, NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
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

void client_dialog::join_server(int index) {
    if (index < 0 || index >= (int)server_list.size()) return;
    if (!message_handler) return;
#ifdef DEBUG
    message_handler("/join " + server_list[index] + "/test");
#else
    message_handler("/join " + server_list[index]);
#endif
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
            dialog->hwndDlg = hwndDlg;
            SetProp(hwndDlg, L"client_dialog", dialog);

            GetClientRect(hwndDlg, &dialog->initial_rect);
            dialog->set_window_scale(GetDlgItem(hwndDlg, IDC_OUTPUT_EDIT), { 0.0f, 0.6f, 1.0f, 1.0f });
            dialog->set_window_scale(GetDlgItem(hwndDlg, IDC_INPUT_EDIT), { 0.0f, 1.0f, 1.0f, 1.0f });
            dialog->set_window_scale(GetDlgItem(hwndDlg, IDC_USER_LIST), { 0.0f, 0.0f, 0.6f, 0.6f });
            dialog->set_window_scale(GetDlgItem(hwndDlg, IDC_SERVER_LIST), { 0.6f, 0.0f, 1.0f, 0.6f });

            LVCOLUMN column;
            ZeroMemory(&column, sizeof(column));
            column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

            HWND user_view = GetDlgItem(hwndDlg, IDC_USER_LIST);
            ListView_SetExtendedListViewStyle(user_view, LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT);
            column.pszText = (LPWSTR)L"#";
            ListView_InsertColumn(user_view, 0, &column);
            column.pszText = (LPWSTR)L"A";
            ListView_InsertColumn(user_view, 1, &column);
            column.pszText = (LPWSTR)L"Name";
            ListView_InsertColumn(user_view, 2, &column);
            column.pszText = (LPWSTR)L"1P";
            ListView_InsertColumn(user_view, 3, &column);
            column.pszText = (LPWSTR)L"2P";
            ListView_InsertColumn(user_view, 4, &column);
            column.pszText = (LPWSTR)L"3P";
            ListView_InsertColumn(user_view, 5, &column);
            column.pszText = (LPWSTR)L"4P";
            ListView_InsertColumn(user_view, 6, &column);
            column.pszText = (LPWSTR)L"Lag";
            ListView_InsertColumn(user_view, 7, &column);
            column.pszText = (LPWSTR)L"Ping";
            ListView_InsertColumn(user_view, 8, &column);
            dialog->set_column_scale(user_view, { 22, 22, 120, 32, 32, 32, 32, 36, 52 });

            HWND server_view = GetDlgItem(hwndDlg, IDC_SERVER_LIST);
            ListView_SetExtendedListViewStyle(server_view, LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT);
            column.pszText = (LPWSTR)L"Server";
            ListView_InsertColumn(server_view, 0, &column);
            column.pszText = (LPWSTR)L"Location";
            ListView_InsertColumn(server_view, 1, &column);
            column.pszText = (LPWSTR)L"Ping";
            ListView_InsertColumn(server_view, 2, &column);
            dialog->set_column_scale(server_view, { 120, 96, 72 });

            dialog->scale_columns();

            return TRUE;
        }

        case WM_SIZE: {
            auto dialog = (client_dialog*)GetProp(hwndDlg, L"client_dialog");
            if (dialog) {
                dialog->scale_windows();
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
                    auto dialog = (client_dialog*)GetProp(hwndDlg, L"client_dialog");
                    if (dialog && dialog->message_handler) {
                        HWND control = GetFocus();
                        if (control == GetDlgItem(hwndDlg, IDC_INPUT_EDIT)) {
                            wchar_t buffer[1024];
                            Edit_GetText(control, buffer, 1024);
                            string message = wstring_to_utf8(buffer);
                            if (message.empty()) return TRUE;
                            dialog->message_handler(wstring_to_utf8(buffer));
                            Edit_SetText(control, L"");
                            return TRUE;
                        } else if (control == GetDlgItem(hwndDlg, IDC_SERVER_LIST)) {
                            dialog->join_server(ListView_GetSelectionMark(control));
                            return TRUE;
                        }
                    }
                }
            }
            break;

        case WM_NOTIFY: {
            LPNMHDR nm = (LPNMHDR)lParam;
            switch (nm->idFrom) {
                case IDC_SERVER_LIST: {
                    switch (nm->code) {
                        case NM_DBLCLK: {
                            LPNMITEMACTIVATE nmia = (LPNMITEMACTIVATE)lParam;
                            auto dialog = (client_dialog*)GetProp(hwndDlg, L"client_dialog");
                            if (dialog) {
                                dialog->join_server(nmia->iItem);
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
