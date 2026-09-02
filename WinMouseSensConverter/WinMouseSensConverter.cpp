#include "WinMouseSensConverter.hpp"

#include "config.hpp"

#include <CommCtrl.h>

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;

    INITCOMMONCONTROLSEX common_controls{};
    common_controls.dwSize = sizeof(common_controls);
    common_controls.dwICC = ICC_STANDARD_CLASSES;
    (void)InitCommonControlsEx(&common_controls);

    config::UserConfig user_config = config::load_or_create();
    public_data::current_mode_ = user_config.mode;
    HWND hwnd = ui::create_main_window(hInstance, user_config);

    if (hwnd == nullptr || sync::sts_.stop_requested()) {
        sync::sts_.request_stop();
        return 1;
    }

    ShowWindow(hwnd, nCmdShow == 0 ? SW_SHOWDEFAULT : nCmdShow);

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

        // Redraw the UI only on the UI timer tick, and only if the content has changed since the last redraw.
        ui::finish_main_loop_iteration(hwnd, msg);
    }

    sync::sts_.request_stop();

    (void)config::save(user_config);

    return exit_code;
}
