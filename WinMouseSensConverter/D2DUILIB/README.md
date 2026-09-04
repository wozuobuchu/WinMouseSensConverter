# D2DUILIB

`D2DUILIB` is a Windows C++20 header-only UI component library built on Direct2D and DirectWrite. It has no dependency on WinMouseSensConverter configuration, resources, or runtime state.

## Integration

Copy the complete `D2DUILIB` directory into another Windows desktop project. Include the required component headers and `D2DUILIB_INTERFACE/d2dui_system_render.hpp`, compile as C++20, and link `d2d1.lib` plus `dwrite.lib` from the Windows SDK.

Create one `d2dui::D2duiContext` for each HWND. The context owns the factories, HWND render target, shared brushes, and shared text formats. Call `begin_frame` once, draw one or more render queues, and call `end_frame` once:

```cpp
#include "D2DUILIB_COMPONENT/d2dui_status_bar.hpp"
#include "D2DUILIB_INTERFACE/d2dui_system_render.hpp"

d2dui::D2duiContext context;
context.initialize(hwnd, dpi);

d2dui::D2duiSystemRender common;
d2dui::D2duiSystemRender page;
auto& status = common.emplace_component<d2dui::D2duiStatusBar>();

if (context.begin_frame({0xF4F7FB, 1.0f}) == S_OK) {
    const HRESULT common_result = common.draw(context);
    const HRESULT page_result = SUCCEEDED(common_result) ? page.draw(context) : common_result;
    const HRESULT end_result = context.end_frame();
    // Handle page_result and end_result here.
}
```

`D2duiSystemRender` owns registered components with `std::unique_ptr`. `register_component` and `emplace_component` return references to the stored objects. Those references remain valid across render-queue vector reallocations, but become invalid when the component is unregistered, the queue is cleared, or the renderer is destroyed.

The component `on_click()` interface defines behavior only. D2DUILIB does not perform hit testing, coordinate checks, or mouse-message routing. The host decides when to invoke it.

## Resource lifetime

- Reuse one context across render queues that draw into the same window.
- Render queues never call BeginDraw, Clear, or EndDraw.
- Window resizing reuses the HWND render target through `Resize`.
- Device loss clears target-dependent brushes and the render target while preserving DirectWrite resources and component layouts.
- Components update text layouts only when their text, style, suffix, or bounds change.

All implementation is inline in the headers. Component headers include their direct dependencies and can be included independently without an application umbrella header.
