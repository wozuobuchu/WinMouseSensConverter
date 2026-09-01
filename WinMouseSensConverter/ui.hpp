#pragma once

#ifndef UI_HPP_
#define UI_HPP_

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "config.hpp"

#include <Windows.h>

namespace ui {

    // Creates the application's main window. The window owns its UI resources.
    HWND create_main_window(HINSTANCE instance, config::UserConfig& user_config) noexcept;

    // Routes keyboard navigation to any open modeless dialog.
    bool preprocess_modeless_dialog_message(HWND main_window, MSG& message) noexcept;

    // Marks changed UI data dirty and renders it only on the UI timer tick.
    void finish_main_loop_iteration(HWND main_window, const MSG& message, bool content_changed) noexcept;

} // namespace ui

#endif // UI_HPP_
