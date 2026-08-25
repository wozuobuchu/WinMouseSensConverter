#include "WinMouseSensConverter.hpp"

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow) {

    std::pair<WNDCLASSEX*, HWND> rrt = ui::register_main_ui(hInstance, hPrevInstance, lpCmdLine, nCmdShow);

    if (sync::sts_.stop_requested()) return 1;

    ShowWindow(rrt.second, SW_SHOWDEFAULT);
    UpdateWindow(rrt.second);

    MSG msg{ 0 };
    while (GetMessage(&msg, NULL, 0, 0) && (!sync::sts_.stop_requested())) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        main_loop::pull_msg_kbd();
        main_loop::pull_msg_mouse();
    }

    sync::sts_.request_stop();

    return 0;
}
