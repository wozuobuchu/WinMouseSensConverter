#pragma once

#ifndef UI_HPP_
#define UI_HPP_

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

namespace ui {

    // Must run before the first top-level window is created.
    bool enable_process_dpi_awareness() noexcept;

    // Creates the application's main window. The window owns its UI resources.
    HWND create_main_window(HINSTANCE instance) noexcept;

    // Invalidates the client area without erasing the Direct2D back buffer.
    void request_redraw(HWND hwnd) noexcept;

} // namespace ui

#endif // UI_HPP_
