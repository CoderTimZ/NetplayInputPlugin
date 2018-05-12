#include "stdafx.h"

#include "util.h"
#include "client_dialog.h"
#include "resource.h"

#define WM_TASK 0x8000

using namespace std;

client_dialog::client_dialog(HMODULE hmod, HWND main_window)
    : hmod(hmod), main_window(main_window), h_rich(LoadLibrary(L"Riched20.dll")), hwndDlg(NULL), thread([=] { gui_thread(); }) {
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

    if (h_rich) {
        FreeLibrary(h_rich);
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

void client_dialog::status(const string& text) {
    unique_lock<mutex> lock(mut);
    if (destroyed) return;

    PostMessage(hwndDlg, WM_TASK, (WPARAM) new function<void(void)>([=] {
        HWND output_box = GetDlgItem(hwndDlg, IDC_OUTPUT_RICHEDIT);
        SendMessage(output_box, WM_SETREDRAW, FALSE, NULL);

        bool at_bottom = scroll_at_bottom();

        append_timestamp();

        CHARFORMAT2 format;
        format.cbSize = sizeof(format);
        format.dwMask = CFM_COLOR | CFM_BOLD;
        format.crTextColor = RGB(0, 0, 255);
        format.dwEffects = CFE_BOLD;
        SendMessage(output_box, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&format);
        insert_text(text);

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
        HWND output_box = GetDlgItem(hwndDlg, IDC_OUTPUT_RICHEDIT);
        SendMessage(output_box, WM_SETREDRAW, FALSE, NULL);

        bool at_bottom = scroll_at_bottom();

        append_timestamp();

        CHARFORMAT2 format;
        format.cbSize = sizeof(format);
        format.dwMask = CFM_COLOR | CFM_BOLD;
        format.crTextColor = RGB(255, 0, 0);
        format.dwEffects = CFE_BOLD;
        SendMessage(output_box, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM) &format);
        insert_text(text);

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
        HWND output_box = GetDlgItem(hwndDlg, IDC_OUTPUT_RICHEDIT);
        SendMessage(output_box, WM_SETREDRAW, FALSE, NULL);

        bool at_bottom = scroll_at_bottom();

        append_timestamp();

        CHARFORMAT2 format;
        format.cbSize = sizeof(format);
        format.dwMask = CFM_COLOR | CFM_BOLD;
        format.crTextColor = RGB(0, 0, 0);
        format.dwEffects = CFE_BOLD;
        SendMessage(output_box, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&format);
        insert_text(name + ":");

        format.cbSize = sizeof(format);
        format.dwMask = CFM_COLOR | CFM_BOLD;
        format.crTextColor = RGB(0, 0, 0);
        format.dwEffects = 0;
        SendMessage(output_box, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&format);
        insert_text(" " + message);

        if (at_bottom) {
            scroll_to_bottom();
        }

        alert_user(false);

        SendMessage(output_box, WM_SETREDRAW, TRUE, NULL);
        RedrawWindow(output_box, NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
    }), NULL);
}

void client_dialog::update_user_list(const map<uint32_t, user_data>& users) {
    unique_lock<mutex> lock(mut);
    if (destroyed) return;

    PostMessage(hwndDlg, WM_TASK, (WPARAM) new function<void(void)>([=] {
        HWND list_box = GetDlgItem(hwndDlg, IDC_USER_LIST);

        SendMessage(list_box, WM_SETREDRAW, FALSE, NULL);

        ListBox_ResetContent(list_box);
        for (auto& e : users) {
            const user_data& data = e.second;
            string text = "[";
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (i > 0) {
                    text += " ";
                }
                int local_port = data.control_map.to_local(i);
                if (local_port >= 0) {
                    text += to_string(i + 1);
                    switch (data.controllers[local_port].Plugin) {
                        case PLUGIN_MEMPAK: text += "M"; break;
                        case PLUGIN_RUMBLE_PAK: text += "R"; break;
                        case PLUGIN_TANSFER_PAK: text += "T"; break;
                        default: text += " ";
                    }
                } else {
                    text += "- ";
                }
            }
            text += "] ";
            text += data.name;
            if (!isnan(data.latency)) {
                text += " (" + to_string((int)(data.latency * 1000)) + " ms)";
            }
            
            ListBox_InsertString(list_box, -1, utf8_to_wstring(text).c_str());
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
            string text = e.first;
            server_list.push_back(text);
            switch ((int)e.second) {
                case -3: text += " (Wrong Version)"; break;
                case -2: text += " (Error)"; break;
                default:
                    if (e.second >= 0) {
                        text += " (" + to_string((int)(e.second * 1000)) + " ms)";
                    }
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

    RECT statRect;
    POINT tl, br;
    GetWindowRect(GetDlgItem(hwndDlg, IDC_OUTPUT_RICHEDIT), &statRect);
    tl.x = statRect.left;
    tl.y = statRect.top;
    br.x = statRect.right;
    br.y = statRect.bottom;
    ScreenToClient(hwndDlg, &tl);
    ScreenToClient(hwndDlg, &br);

    SetWindowPos(GetDlgItem(hwndDlg, IDC_OUTPUT_RICHEDIT), NULL, tl.x + 1, tl.y + 1, br.x - tl.x - 2, br.y - tl.y - 2, 0);
    CreateWindow(WC_STATIC, nullptr, SS_BLACKFRAME | WS_CHILD | WS_VISIBLE, tl.x, tl.y, br.x - tl.x, br.y - tl.y, hwndDlg, (HMENU)IDC_STATIC, hmod, 0);

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
    if (GetScrollInfo(GetDlgItem(hwndDlg, IDC_OUTPUT_RICHEDIT), SB_VERT, &psi) && psi.nMax - (int)psi.nPage > psi.nTrackPos) {
        return false;
    }

    return true;
}

void client_dialog::scroll_to_bottom() {
    SendMessage(GetDlgItem(hwndDlg, IDC_OUTPUT_RICHEDIT), WM_VSCROLL, MAKELONG(SB_BOTTOM, 0), 0);
}

void client_dialog::select_end() {
    CHARRANGE cr;
    cr.cpMin = -1;
    cr.cpMax = -1;
    SendMessage(GetDlgItem(hwndDlg, IDC_OUTPUT_RICHEDIT), EM_EXSETSEL, 0, (LPARAM) &cr);
}

void client_dialog::insert_text(const string& text) {
    SendMessage(GetDlgItem(hwndDlg, IDC_OUTPUT_RICHEDIT), EM_REPLACESEL, 0, (LPARAM)utf8_to_wstring(text).c_str());
}

void client_dialog::append_timestamp() {
    time_t rawtime;
    time(&rawtime);

    auto timeinfo = localtime(&rawtime);

    char buffer[9];
    strftime(buffer, sizeof buffer, "%I:%M:%S", timeinfo);

    select_end();

    CHARFORMAT2 format;
    format.cbSize = sizeof(format);
    format.dwMask = CFM_COLOR | CFM_BOLD;
    format.crTextColor = RGB(128, 128, 128);
    format.dwEffects = 0;
    SendMessage(GetDlgItem(hwndDlg, IDC_OUTPUT_RICHEDIT), EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM) &format);

    if (SendMessage(GetDlgItem(hwndDlg, IDC_OUTPUT_RICHEDIT), WM_GETTEXTLENGTH, 0, 0) == 0) {
        insert_text("(" + string(buffer) + ") ");
    } else {
        insert_text("\n(" + string(buffer) + ") ");
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
                                dialog->message_handler("/join " + dialog->server_list[selection]);
                            }
                            break;
                    }
                    break;
                }
            }
            break;

        case WM_TASK:
            auto task = (function <void(void)>*) wParam;
            (*task)();
            delete task;
            return TRUE;
    }

    return FALSE;
}
