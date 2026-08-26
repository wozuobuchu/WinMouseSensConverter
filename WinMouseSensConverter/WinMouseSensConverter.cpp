#include "WinMouseSensConverter.hpp"

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;

    (void)ui::enable_process_dpi_awareness();
    HWND hwnd = ui::create_main_window(hInstance);

    if (hwnd == nullptr || sync::sts_.stop_requested()) {
        sync::sts_.request_stop();
        return 1;
    }

    ShowWindow(hwnd, nCmdShow == 0 ? SW_SHOWDEFAULT : nCmdShow);
    UpdateWindow(hwnd);

    MSG msg{0};
    int exit_code = 0;
    while (!sync::sts_.stop_requested()) {
        const BOOL message_result = GetMessageW(&msg, nullptr, 0, 0);
        if (message_result == -1) {
            exit_code = 1;
            break;
        }
        if (message_result == 0) {
            exit_code = static_cast<int>(msg.wParam);
            break;
        }

        if (!ui::preprocess_modeless_dialog_message(hwnd, msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        // Attribute the pending movement to the state that was active before
        // processing a new F1 key-down event at this polling boundary.
        bool changed = main_loop::pull_msg_mouse();
        changed = main_loop::pull_msg_kbd() || changed;
        if (changed) ui::request_redraw(hwnd);
    }

    sync::sts_.request_stop();

    return exit_code;
}
