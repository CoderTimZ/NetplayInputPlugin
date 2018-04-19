#include <cstdint>
#include <sstream>
#include <boost/numeric/conversion/cast.hpp>
#include <boost/tokenizer.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <windows.h>
#include <Windowsx.h>
#include <Richedit.h>
#include <Uxtheme.h>

#include "client_dialog.h"
#include "resource.h"
#include "client.h"
#include "game.h"

using namespace boost::asio;
using namespace std;

client_dialog::client_dialog(HMODULE hmod, game& my_game) : hmod(hmod), my_game(my_game), h_rich(LoadLibrary(L"Riched20.dll")), hwndDlg(NULL), initialized(2), thread() {
    thread = boost::thread(boost::bind(&client_dialog::gui_thread, this));
    initialized.wait();
}

client_dialog::~client_dialog() {
    {
        boost::mutex::scoped_lock lock(mut);

        if (hwndDlg != NULL) {
            SendMessage(hwndDlg, WM_COMMAND, IDC_DESTROY_BUTTON, 0);
        }
    }

    thread.join();

    if (h_rich != NULL) {
        FreeLibrary(h_rich);
    }
}

void client_dialog::status(const wstring& text) {
    boost::mutex::scoped_lock lock(mut);

    if (hwndDlg == NULL) {
        return;
    }

    bool at_bottom = scroll_at_bottom();

    append_timestamp();

    CHARFORMAT2 format;
    format.cbSize = sizeof(format);
    format.dwMask = CFM_COLOR | CFM_BOLD | CFM_SIZE;
    format.crTextColor = RGB(0, 128, 0);
    format.yHeight = 10*20;
    format.dwEffects = CFE_BOLD;
    SendMessage(GetDlgItem(hwndDlg, IDC_OUTPUT_RICHEDIT), EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM) &format);
    insert_text(text);

    if (at_bottom) {
        scroll_to_bottom();
    }

    alert_user();
}

void client_dialog::error(const wstring& text) {
    boost::mutex::scoped_lock lock(mut);

    if (hwndDlg == NULL) {
        return;
    }

    bool at_bottom = scroll_at_bottom();

    append_timestamp();

    CHARFORMAT2 format;
    format.cbSize = sizeof(format);
    format.dwMask = CFM_COLOR | CFM_BOLD | CFM_SIZE;
    format.crTextColor = RGB(255, 0, 0);
    format.yHeight = 10*20;
    format.dwEffects = CFE_BOLD;
    SendMessage(GetDlgItem(hwndDlg, IDC_OUTPUT_RICHEDIT), EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM) &format);
    insert_text(text);

    if (at_bottom) {
        scroll_to_bottom();
    }

    alert_user();
}

void client_dialog::chat(const wstring& name, const wstring& message) {
    boost::mutex::scoped_lock lock(mut);

    if (hwndDlg == NULL) {
        return;
    }

    bool at_bottom = scroll_at_bottom();

    append_timestamp();

    CHARFORMAT2 format;
    format.cbSize = sizeof(format);
    format.dwMask = CFM_COLOR | CFM_BOLD | CFM_SIZE;
    format.crTextColor = RGB(0, 0, 0);
    format.yHeight = 10*20;
    format.dwEffects = CFE_BOLD;
    SendMessage(GetDlgItem(hwndDlg, IDC_OUTPUT_RICHEDIT), EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM) &format);
    insert_text(name + L":");

    format.cbSize = sizeof(format);
    format.dwMask = CFM_COLOR | CFM_BOLD | CFM_SIZE;
    format.crTextColor = RGB(0, 0, 0);
    format.yHeight = 10*20;
    format.dwEffects = !CFE_BOLD;
    SendMessage(GetDlgItem(hwndDlg, IDC_OUTPUT_RICHEDIT), EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM) &format);
    insert_text(L" " + message);

    if (at_bottom) {
        scroll_to_bottom();
    }

    alert_user();
}

void client_dialog::update_user_list(const map<uint32_t, wstring>& names, const map<uint32_t, uint32_t>& pings) {
    boost::mutex::scoped_lock lock(mut);

    if (hwndDlg == NULL) {
        return;
    }

    HWND list_box = GetDlgItem(hwndDlg, IDC_USER_LIST);

    SendMessage(list_box, WM_SETREDRAW, FALSE, NULL);

    int selection = ListBox_GetCurSel(list_box);

    ListBox_ResetContent(list_box);
    for (map<uint32_t, wstring>::const_iterator it = names.begin(); it != names.end(); ++it) {
        wstring entry = it->second;
        auto ping = pings.find(it->first);
        if (ping != pings.end()) {
            std::wstringstream ss;
            ss << std::fixed << std::setprecision(ping->second < 10000 ? 1 : 0) << (ping->second / 1000.0);
            entry += L" (" + ss.str() + L" ms)";
        }
        ListBox_InsertString(list_box, -1, entry.c_str());
    }

    ListBox_SetCurSel(list_box, selection);

    SendMessage(list_box, WM_SETREDRAW, TRUE, NULL);
}

void client_dialog::gui_thread() {
    hwndDlg = CreateDialogParam(hmod, MAKEINTRESOURCE(IDD_NETPLAY_DIALOG), NULL, &DialogProc, (LPARAM) this);

    initialized.wait();

    MSG message;
    while (GetMessage(&message, NULL, 0, 0) > 0) {
        if (!IsDialogMessage(hwndDlg, &message)) {
            TranslateMessage(&message);
            DispatchMessage(&message);
        }
    }

    boost::mutex::scoped_lock lock(mut);

    hwndDlg = NULL;
}

void client_dialog::process_command(const wstring& command) {
    my_game.process_command(command);
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

void client_dialog::insert_text(const wstring& text) {
    SendMessage(GetDlgItem(hwndDlg, IDC_OUTPUT_RICHEDIT), EM_REPLACESEL, 0, (LPARAM) text.c_str());
}

void client_dialog::append_timestamp() {
    using namespace boost::gregorian;
    using namespace boost::posix_time;

    string time = to_simple_string(second_clock::local_time());
    time = time.substr(12);
    wstring wtime;
    wtime.assign(time.begin(), time.end());

    select_end();

    CHARFORMAT2 format;
    format.cbSize = sizeof(format);
    format.dwMask = CFM_COLOR | CFM_BOLD | CFM_SIZE;
    format.crTextColor = RGB(128, 128, 128);
    format.yHeight = 10*20;
    format.dwEffects = !CFE_BOLD;
    SendMessage(GetDlgItem(hwndDlg, IDC_OUTPUT_RICHEDIT), EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM) &format);

    if (SendMessage(GetDlgItem(hwndDlg, IDC_OUTPUT_RICHEDIT), WM_GETTEXTLENGTH, 0, 0) == 0) {
        insert_text(L"(" + wtime + L") ");
    } else {
        insert_text(L"\n(" + wtime + L") ");
    }
}

void client_dialog::alert_user() {
    WINDOWPLACEMENT wp;
    wp.length = sizeof(wp);
    GetWindowPlacement(hwndDlg, &wp);

    if (wp.showCmd == SW_SHOWMINIMIZED) {
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
            SetWindowTheme(GetDlgItem(hwndDlg, IDC_USER_LIST), L"", L"");
            return TRUE;

        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        case WM_COMMAND:
            switch (wParam) {
                case IDC_DESTROY_BUTTON:
                    DestroyWindow(hwndDlg);
                    return TRUE;

                case IDCANCEL:
                    ShowWindow(hwndDlg, SW_MINIMIZE);
                    return TRUE;

                case IDOK:
                    if (GetFocus() == GetDlgItem(hwndDlg, IDC_INPUT_RICHEDIT)) {
                        wchar_t buffer[1024];
                        GETTEXTEX gte;
                        gte.cb = sizeof(buffer);
                        gte.flags = GT_DEFAULT;
                        gte.codepage = 1200; // Unicode
                        gte.lpDefaultChar = NULL;
                        gte.lpUsedDefChar = NULL;

                        if (SendMessage(GetDlgItem(hwndDlg, IDC_INPUT_RICHEDIT), EM_GETTEXTEX, (WPARAM) &gte, (LPARAM) buffer)) {
                            client_dialog* d = (client_dialog*) GetProp(hwndDlg, L"client_dialog");
                            d->process_command(buffer);

                            SETTEXTEX ste;
                            ste.flags = ST_DEFAULT;
                            ste.codepage = 1200; // Unicode
                            SendMessage(GetDlgItem(hwndDlg, IDC_INPUT_RICHEDIT), EM_SETTEXTEX, (WPARAM) &ste, (LPARAM) L"");
                        }
                    }
                    return TRUE;
            }
            break;
    }

    return FALSE;
}
