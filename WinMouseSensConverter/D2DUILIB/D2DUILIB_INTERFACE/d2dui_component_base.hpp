#pragma once

#ifndef D2DUI_COMPONENT_BASE_HPP_
#define D2DUI_COMPONENT_BASE_HPP_

#include "d2dui.hpp"

#include <Windows.h>

#include <concepts>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>
#include <type_traits>


namespace d2dui {

    // Mouse event distributor for D2duiComponentsBase.
    // Use VK * 3 to find the corresponding D2duiMouseEvent.
    enum class D2duiMouseEvent : int32_t {
        MOUSE_HOVER_ENTER,
        MOUSE_HOVER_ON,
        MOUSE_HOVER_LEAVE,

        MOUSE_LEFT_CLICK_ENTER,
        MOUSE_LEFT_CLICK_ON,
        MOUSE_LEFT_CLICK_LEAVE,

        MOUSE_RIGHT_CLICK_ENTER,
        MOUSE_RIGHT_CLICK_ON,
        MOUSE_RIGHT_CLICK_LEAVE,

        // Useless for this component, keep VK_CANCEL here for converting int16_t vkey to D2duiMouseEvent.
        VK_CANCEL_PLACEHOLDER_01,
        VK_CANCEL_PLACEHOLDER_02,
        VK_CANCEL_PLACEHOLDER_03,

        MOUSE_MID_CLICK_ENTER,
        MOUSE_MID_CLICK_ON,
        MOUSE_MID_CLICK_LEAVE,

        MOUSE_X1_CLICK_ENTER,
        MOUSE_X1_CLICK_ON,
        MOUSE_X1_CLICK_LEAVE,

        MOUSE_X2_CLICK_ENTER,
        MOUSE_X2_CLICK_ON,
        MOUSE_X2_CLICK_LEAVE,

        MOUSE_CANCEL, // Interaction aborted; never commit a click on cancellation.
        SIZE
    };

    template <D2duiMouseEvent Event>
    concept ValidD2duiMouseEvent =
        static_cast<int32_t>(Event) >= 0 &&
        static_cast<int32_t>(Event) < static_cast<int32_t>(D2duiMouseEvent::SIZE);

    struct D2duiMouseEventParam {
        D2D1_POINT_2F position;
        int16_t vk;
        int16_t down;
        int32_t reserved;
    };

    class D2duiComponentsBase {
    public:
        D2duiComponentsBase() = default;
        D2duiComponentsBase(const D2duiComponentsBase&) = delete;
        D2duiComponentsBase& operator=(const D2duiComponentsBase&) = delete;
        D2duiComponentsBase(D2duiComponentsBase&&) = default;
        D2duiComponentsBase& operator=(D2duiComponentsBase&&) = default;
        virtual ~D2duiComponentsBase() = default;

        // Necessary interface for derived classes to implement.
        [[nodiscard]] virtual const D2D1_RECT_F& get_bounds() const noexcept = 0;
        virtual void resize(const D2D1_RECT_F& bounds, float scale) noexcept = 0;
        virtual HRESULT draw(D2duiContext& context) noexcept = 0;

        // Register a mouse event handler for the component. The handler must return
        // bool (true requests a redraw); void-returning handlers are rejected by the concept constraint and fail to compile.
        template <D2duiMouseEvent Event, typename Handler> requires
            ValidD2duiMouseEvent<Event> && 
            std::invocable<Handler&, const D2duiMouseEventParam&> && 
            std::convertible_to<std::invoke_result_t<Handler&, const D2duiMouseEventParam&>, bool>
        void register_mouse_event_handler(Handler&& handler) noexcept {
            mouse_event_handlers_.insert_or_assign(static_cast<int32_t>(Event), std::forward<Handler>(handler));
        }

        // Unregister a mouse event handler for the component.
        template <D2duiMouseEvent Event>
            requires ValidD2duiMouseEvent<Event>
        bool unregister_mouse_event_handler() noexcept {
            return mouse_event_handlers_.erase(static_cast<int32_t>(Event)) != 0;
        }

        // Invoke the registered mouse event handler for the component.
        bool respond_mouse_event(const D2duiMouseEvent event, const D2duiMouseEventParam param, bool* redraw_requested = nullptr) noexcept {
            const auto handler = mouse_event_handlers_.find(static_cast<int32_t>(event));
            if (handler == mouse_event_handlers_.end()) {
                return false;
            }

            try {
                // The handler must not unregister or replace its own event during
                // invocation: `handler` iterates the map that owns this callable, so
                // mutating that entry destroys the object currently executing.
                const bool changed = handler->second(param);
                if (redraw_requested != nullptr) *redraw_requested |= changed;
            } catch (...) {
                return false;
            }
            
            return true;
        }

    protected:
        D2D1_RECT_F bounds_{};
        float scale_ = 1.0f;
        bool dirty_ = true;

    private:
        std::unordered_map<int32_t, std::function<bool(const D2duiMouseEventParam&)>> mouse_event_handlers_;

    };

} // namespace d2dui

#endif // D2DUI_COMPONENT_BASE_HPP_
