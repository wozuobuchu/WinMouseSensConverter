#ifndef _UI_HPP_
#define _UI_HPP_

#include "resource.hpp"
#include <Windows.h>
#include <CommCtrl.h>
#include <commdlg.h>

#include <algorithm>
#include <string>
#include <vector>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <format>
#include <iostream>
#include <thread>
#include <stop_token>
#include <mutex>
#include <shared_mutex>
#include <functional>
#include <memory>
#include <cstring>
#include <atomic>
#include <cmath>
#include <sstream>
#include <exception>
#include <utility>

#include "sync.hpp"

#include "SYS/aop.hpp"
#include "SYS/fps.hpp"
#include "SYS/low_latency_keyboard.hpp"

#pragma comment(lib, "Comctl32.lib")

#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace ui_args {
    inline constexpr int32_t UI_DEF_WIDTH = 1280;
    inline constexpr int32_t UI_DEF_HEIGHT = 720;
    inline constexpr int32_t UI_MIN_WIDTH = 640;
    inline constexpr int32_t UI_MIN_HEIGHT = 360;
}

namespace ui {

    inline void QUIT_PROG() {
        PostQuitMessage(0);
        sync::sts_.request_stop();
    }

    LRESULT CALLBACK windowproc_main(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        switch (uMsg) {

            case WM_SYSCOMMAND: {
                if (wParam == SC_CLOSE) {
                    QUIT_PROG();
                    return 0;
                }
                break;
            }

            case WM_GETMINMAXINFO: {
                LPMINMAXINFO lpMinMaxInfo = (LPMINMAXINFO)lParam;
                lpMinMaxInfo->ptMinTrackSize.x = ui_args::UI_MIN_WIDTH;
                lpMinMaxInfo->ptMinTrackSize.y = ui_args::UI_MIN_HEIGHT;
                return 0;
            }

            case WM_CREATE: {

                return 0;
            }

            case WM_COMMAND: {
                switch (LOWORD(wParam)) {

                    default: {
                        break;
                    }
                }
                return 0;
            }

            case WM_SIZE: {
                if (wParam == SIZE_MINIMIZED) return 0;
                KillTimer(hwnd, 9999);
                SetTimer(hwnd, 9999, 30, NULL);
                return 0;
            }

            case WM_TIMER: {
                if (wParam == 9999) {
                    KillTimer(hwnd, 9999);

                    return 0;
                }
                return 0;
            }

            case WM_DESTROY: {
                QUIT_PROG();
                return 0;
            }

            default: {
                break;
            }

        }

        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    std::pair<WNDCLASSEX*, HWND> register_main_ui(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow) {
        (void) hInstance;
        (void) hPrevInstance;
        (void) lpCmdLine;
        (void) nCmdShow;

        INITCOMMONCONTROLSEX icex{};
        icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
        icex.dwICC = ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
        InitCommonControlsEx(&icex);

        WNDCLASSEX* wndclass_main = new WNDCLASSEX();
        std::memset(wndclass_main, 0, sizeof(WNDCLASSEX));

        std::pair<WNDCLASSEX*, HWND> ret{nullptr, 0};

        wndclass_main->cbSize = sizeof(WNDCLASSEX);
        wndclass_main->style = NULL;
        wndclass_main->lpfnWndProc = windowproc_main;
        wndclass_main->cbClsExtra = NULL;
        wndclass_main->cbWndExtra = NULL;
        wndclass_main->hInstance = hInstance;
        wndclass_main->hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_WINMOUSESENSCONVERTER));
        wndclass_main->hCursor = LoadCursor(NULL, IDC_ARROW);
        wndclass_main->hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wndclass_main->lpszMenuName = NULL;
        wndclass_main->lpszClassName = TEXT("MainUIWindowClass");
        wndclass_main->hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_SMALL));

        if (!RegisterClassEx(wndclass_main)) {
            sync::sts_.request_stop();
            return ret;
        }

        HWND hwnd = CreateWindowEx(
            WS_EX_CLIENTEDGE,
            wndclass_main->lpszClassName,
            TEXT("WinMouseSensConverter"),
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            CW_USEDEFAULT, CW_USEDEFAULT,
            1920, 1080,
            NULL,
            NULL,
            hInstance,
            NULL
        );
        if (hwnd == NULL) {
            sync::sts_.request_stop();
            return ret;
        }

        ret.first = wndclass_main;
        ret.second = hwnd;

        return ret;
    }

}

#endif // !_UI_HPP_