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

void client_dialog::update_user_list(const vector<string>& lines) {
    unique_lock<mutex> lock(mut);
    if (destroyed) return;

    PostMessage(hwndDlg, WM_TASK, (WPARAM) new function<void(void)>([=] {
        HWND list_box = GetDlgItem(hwndDlg, IDC_USER_LIST);

        SendMessage(list_box, WM_SETREDRAW, FALSE, NULL);
        ListBox_ResetContent(list_box);
        for (auto& line : lines) {
            ListBox_InsertString(list_box, -1, utf8_to_wstring(line).c_str());
        }
        SendMessage(list_box, WM_SETREDRAW, TRUE, NULL);

        RedrawWindow(list_box, NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
    }), NULL);
}

void client_dialog::update_server_list(const map<string, double>& servers) {
    unique_lock<mutex> lock(mut);
    if (destroyed) return;

    PostMessage(hwndDlg, WM_TASK, (WPARAM) new function<void(void)>([=] {
        HWND list_box = GetDlgItem(hwndDlg, IDC_SERVER_LIST);

        SendMessage(list_box, WM_SETREDRAW, FALSE, NULL);

        ListBox_ResetContent(list_box);
        server_list.clear();
        for (auto& e : servers) {
            auto text = e.first;
            auto ping = e.second;
            server_list.push_back(text);
            switch ((int)ping) {
                case SERVER_STATUS_PENDING: break;
                case SERVER_STATUS_ERROR: text += " (Ping Error)"; break;
                case SERVER_STATUS_VERSION_MISMATCH: text += " (Wrong Version)"; break;
                case SERVER_STATUS_OUTDATED_CLIENT: text += " (Outdated Client)"; break;
                case SERVER_STATUS_OUTDATED_SERVER: text += " (Outdated Server)"; break;
                default: text += " (" + to_string(static_cast<int>(ping * 1000)) + " ms)"; break;
            }

            ListBox_InsertString(list_box, -1, utf8_to_wstring(text).c_str());
        }

        SendMessage(list_box, WM_SETREDRAW, TRUE, NULL);
        RedrawWindow(list_box, NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
    }), NULL);
}

void client_dialog::gui_thread() {
    HINSTANCE user32 = GetModuleHandle(L"User32.dll");
    if (user32) {
        auto SetThreadDpiAwarenessContext = (int(__stdcall *)(int)) GetProcAddress(user32, "SetThreadDpiAwarenessContext");
        if (SetThreadDpiAwarenessContext) {
            SetThreadDpiAwarenessContext(-2);
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
        case WM_INITDIALOG:
            SetProp(hwndDlg, L"client_dialog", (void*) lParam);
            return TRUE;

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

                case IDC_SERVER_LIST: {
                    switch (HIWORD(wParam)) {
                        case LBN_DBLCLK:
                            auto dialog = (client_dialog*)GetProp(hwndDlg, L"client_dialog");
                            if (dialog) {
                                int selection = ListBox_GetCurSel(GetDlgItem(hwndDlg, IDC_SERVER_LIST));
                                if (selection < 0 || selection >= (int)dialog->server_list.size()) break;
#ifdef DEBUG
                                dialog->message_handler("/join " + dialog->server_list[selection] + "/test");
#else
                                dialog->message_handler("/join " + dialog->server_list[selection]);
#endif
                            }
                            break;
                    }
                    break;
                }
            }
            break;

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
