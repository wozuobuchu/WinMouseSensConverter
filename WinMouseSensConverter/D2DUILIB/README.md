# D2DUILIB

`D2DUILIB` is a Windows C++20 header-only UI component library built on Direct2D and DirectWrite. It has no dependency on WinMouseSensConverter configuration, resources, or runtime state.

## Integration

Copy the complete `D2DUILIB` directory into another Windows desktop project. Include the required component headers and `D2DUILIB_INTERFACE/d2dui_system_render.hpp`, compile as C++20, and link `user32.lib`, `d2d1.lib`, and `dwrite.lib` from the Windows SDK.

Create one `d2dui::D2duiContext` for each HWND. The context owns the factories, HWND render target, shared brushes, and shared text formats. Call `begin_frame` once, draw one or more render queues, and call `end_frame` once:

```cpp
#include "D2DUILIB_COMPONENT/d2dui_status_bar.hpp"
#include "D2DUILIB_INTERFACE/d2dui_system_render.hpp"

d2dui::D2duiContext context;
context.initialize(hwnd, dpi);

auto common = std::make_shared<d2dui::D2duiSystemRender>();
auto page = std::make_shared<d2dui::D2duiSystemRender>();
auto status = std::make_shared<d2dui::D2duiStatusBar>();
common->register_component(status);
status->set_checked(true);

if (context.begin_frame({0xF4F7FB, 1.0f}) == S_OK) {
    const HRESULT common_result = common->draw(context);
    const HRESULT page_result = SUCCEEDED(common_result) ? page->draw(context) : common_result;
    const HRESULT end_result = context.end_frame();
    // Handle page_result and end_result here.
}
```

`D2duiSystemRender` stores `std::shared_ptr<D2duiComponentsBase>` entries. Create components with `std::make_shared`, retain the typed pointer externally, and pass it to `register_component`, which returns `void` and rejects null pointers with `std::invalid_argument`. `unregister_component` accepts a shared pointer and removes the first entry with the same object address, returning `true` on removal or `false` for null or absent objects. Duplicate registrations append separate entries. Unregistering, clearing, or destroying a queue releases only its ownership: external shared pointers keep components alive, and components are destroyed when their last shared owner releases them. Queue growth does not invalidate component pointers.

The component base interface provides `get_bounds()`, `resize()`, and `draw()` for layout and rendering. Each render queue draws all registered components in registration order and returns immediately if a component fails to draw. The host selects which queues to draw and updates component state through setters such as `set_checked()`.

## Mouse interaction

Include `D2DUILIB_INTERFACE/d2dui_mouse_event_analyser.hpp` and create one `d2dui::MouseEventAnalyser` per window on the window's UI thread. Keep it alive until its callbacks return. The analyser owns its active render queues through `std::shared_ptr<D2duiSystemRender>`; the view keeps shared pointers to those same objects for drawing. Renderers own their component registrations. There are no back-references from renderers to the analyser and no application configuration or mode types in this API.

| Public interface | Contract |
| --- | --- |
| `MouseEventAnalyser(HWND, UINT dpi)` | Bind a live HWND and positive DPI. Null HWND or zero DPI throws `std::invalid_argument`. The analyser cannot be copied or moved. |
| `bool set_renderers(std::vector<std::shared_ptr<D2duiSystemRender>>)` | Set queues in dispatch order. An empty list is valid; null or duplicate renderers throw `std::invalid_argument` without changing the current list. Allocations may throw before the list changes. An unchanged list is a no-op. |
| `MessageResult process_window_message(UINT, WPARAM, LPARAM) noexcept` | Forward window messages, including lifecycle messages. The result contains `consumed`, `result`, `callbacks_invoked`, and `redraw_requested`. Return `result` from the window procedure only when `consumed` is true; otherwise continue normal host/default handling. |
| `void set_dpi(UINT dpi) noexcept` | Update conversion for subsequent mouse messages when the host synchronizes DPI explicitly. Zero is ignored. Does not invoke callbacks, cancel capture, or reinterpret already sampled DIP positions. |
| `bool tick() noexcept` | Emit continuous hover/hold callbacks from the host timer. Does nothing while inactive, minimized, in a menu or sizing loop, or after window destruction. |
| `bool redraw_requested() const noexcept` | Read the redraw request from the latest operation without changing it. |
| Destructor | Release mouse capture, tracking, and shared ownership without invoking component callbacks. |

`tick()`, `set_renderers()`, and `MessageResult::callbacks_invoked` report whether a component callback completed successfully, including callbacks from drained nested messages. Callback execution and redraw requests are independent: a handler returning `bool` requests a redraw only when it returns `true`; existing `void` handlers conservatively request a redraw after successful execution. For window messages, use `MessageResult::redraw_requested` to mark the UI dirty. After `tick()` or `set_renderers()`, read `redraw_requested()` immediately; it includes requests from drained nested callbacks. The next top-level message, tick, or renderer-list update resets this flag. `respond_mouse_event` still returns callback completion and optionally ORs redraw requests into its third `bool*` argument. The analyser never renders, invalidates a window, creates a timer, or reads a live cursor position. Component bounds must be current DIPs before forwarding input and before calling `tick()`; no Direct2D render target is needed for input.

### Connecting a window

The following fragments use the shared `common` and `page` queues above. Store the analyser in the window state, using `std::optional<d2dui::MouseEventAnalyser>` if that state is created before the HWND:

```cpp
#include "D2DUILIB_INTERFACE/d2dui_mouse_event_analyser.hpp"
#include <optional>

// Window state members:
std::optional<d2dui::MouseEventAnalyser> mouse;
bool redraw_dirty = true;

// Once the HWND and its DPI are available:
mouse.emplace(hwnd, dpi);
mouse->set_renderers({common, page});

// At the start of the window procedure, after updating input layout:
const auto input = mouse->process_window_message(message, wparam, lparam);
if (input.redraw_requested) redraw_dirty = true;
if (input.consumed) return input.result;
// Continue host/default processing for messages not consumed.

// In the host's chosen timer handler, after updating layout:
(void)mouse->tick();
if (mouse->redraw_requested()) redraw_dirty = true;
// Draw only in the host's central, timer-gated main-loop path.

// When changing pages, also select this same page for subsequent drawing:
if (mouse->set_renderers({common, next_page})) redraw_dirty = true;
```

Forward `WM_ACTIVATE`, `WM_CAPTURECHANGED`, `WM_CANCELMODE`, menu-loop and sizing-loop entry/exit, `WM_SIZE`, `WM_DPICHANGED`, `WM_DPICHANGED_BEFOREPARENT`, `WM_DPICHANGED_AFTERPARENT`, and window destruction messages as well as mouse messages. Lifecycle messages generally continue to the host/default procedure. Destruction messages cancel interaction and unbind the HWND before host resources are torn down. Update the host's layout and rendering-context DPI separately on `WM_DPICHANGED`; the analyser updates its own coordinate conversion from the same message and leaves the suggested `RECT` to the host.

For Per-Monitor DPI V2 child windows, the analyser queries `GetDpiForWindow` on both `WM_DPICHANGED_BEFOREPARENT` and `WM_DPICHANGED_AFTERPARENT`. These messages carry no DPI in `wParam` and no suggested rectangle in `lParam`; the after-parent notification refreshes the final window DPI. All three DPI notifications return `consumed == false` and invoke no callbacks so the host can update layout, resources, and its dirty flag. A failed DPI query (zero) preserves the previous conversion factor.

When a host already manages DPI through its own notification path, it can synchronize the analyser explicitly:

```cpp
mouse->set_dpi(new_dpi);       // Also used internally by the DPI message handlers.
context.set_dpi(new_dpi);     // Host rendering resources and layout remain separate.
// Update component DIP bounds here, then mark the host UI dirty.
redraw_dirty = true;
```

DPI updates take effect for subsequent mouse messages. Already queued mouse events and the last sampled DIP position retain their original coordinates, including ticks before the next move/down/up. This prevents a reentrant DPI update from changing coordinates halfway through an event's component callbacks. Updates preserve hover and button capture and never commit or cancel a click.

The application must enable Per-Monitor V2 awareness in its manifest or before creating its windows, as the standalone example does. See Microsoft's [WM_DPICHANGED](https://learn.microsoft.com/en-us/windows/win32/hidpi/wm-dpichanged), [before-parent](https://learn.microsoft.com/en-us/windows/win32/hidpi/wm-dpichanged-beforeparent), and [after-parent](https://learn.microsoft.com/en-us/windows/win32/hidpi/wm-dpichanged-afterparent) message contracts.

### Event and ownership semantics

- Client message coordinates are signed pixels converted using the current DPI. Move/down/up use that message's position; leave, tick, and cancellation retain the last position. Hit rectangles include their left/top edges and exclude right/bottom edges.
- Left, right, middle, X1, and X2 buttons are supported. Double-click messages are treated as down transitions; duplicate downs while a button is held are suppressed. Wheel messages are not button events. X-button messages are consumed with `TRUE`, even without a callback.
- Every hit component receives events in queue/registration order, including overlapping components. A down inside a component starts capture; its up is delivered even outside the component. Only `tick()` repeats hover/hold callbacks. `WM_MOUSELEAVE` ends hover but preserves an ongoing drag.
- Capture loss, deactivation, menu entry, interactive sizing, minimization, and window destruction emit `MOUSE_CANCEL` for captured buttons, never a release/commit. Normal final-button release relinquishes HWND capture without an extra cancellation. DPI changes do not cancel interaction.
- Changing queues cancels only outgoing queues and retains common queues' interaction state and the physical-button baseline. New queues cannot inherit a held button. Releasing external pointers does not destroy a renderer while the analyser still owns it; removing it from the list releases that ownership after cancellation and any in-flight dispatch finish.
- Nested input is FIFO across all queues. Cancellation and queue changes immediately invalidate the old pass and clear stale queued input. Component additions wait for the next event, even in a later queue; removals stop responding immediately. Each duplicate component registration has independent interaction state. Component self-removal and callback self-unregistration/replacement are safe.
- Keep the analyser and host state alive until callbacks return. Avoid ownership cycles when registering callbacks: capture a component or renderer through `weak_ptr` if the callback would otherwise own its own registration. A component removed from a queue is not kept alive by cached interaction state.

### Compilable example and migration

[examples/mouse_events.cpp](examples/mouse_events.cpp) is a standalone Win32 example with shared common/page queues, a page selector, independent page controls, DPI layout, timer-driven input, and drawing centralized outside the window procedure. From the `D2DUILIB` directory in an **x64 Native Tools Command Prompt**, build it with:

```bat
cl /nologo /std:c++20 /EHsc /utf-8 /DUNICODE /D_UNICODE examples\mouse_events.cpp /Fe:mouse_events.exe /link /SUBSYSTEM:WINDOWS user32.lib d2d1.lib dwrite.lib
```

The automatic-test project compiles this same source with `D2DUI_EXAMPLE_COMPILE_ONLY`, excluding its entry point; it does not run the example or require elevation.

The former renderer `dispatch_mouse_events` / `cancel_mouse_events` methods and public normalized-input types have been removed. Replace stack/value renderer owners with `make_shared`, change their calls to `->`, and pass them to `set_renderers`. Replace application mouse adapters and per-queue input loops with window-message forwarding and `tick()`. Queue changes and forwarded lifecycle messages handle cancellation; the public `set_dpi` method supports explicit host DPI synchronization; there is no public raw dispatch, cancel, or attach/detach API. The unused live-cursor polling helpers have also been removed.

## Resource lifetime

- Reuse one context across render queues that draw into the same window.
- Render queues never call BeginDraw, Clear, or EndDraw.
- Window resizing reuses the HWND render target through `Resize`.
- Device loss clears target-dependent brushes and the render target while preserving DirectWrite resources and component layouts.
- Components update text layouts only when their text, style, suffix, or bounds change.

All implementation is inline in the headers. Component headers include their direct dependencies and can be included independently without an application umbrella header.
