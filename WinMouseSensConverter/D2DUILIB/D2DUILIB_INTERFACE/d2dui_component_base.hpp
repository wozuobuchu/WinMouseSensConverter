#pragma once

#ifndef D2DUI_COMPONENT_BASE_HPP_
#define D2DUI_COMPONENT_BASE_HPP_

#include <Windows.h>

#include <concepts>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <utility>

#include "d2dui.hpp"

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

        // Register a mouse event handler for the component.
        template <D2duiMouseEvent Event, typename Handler>
            requires ValidD2duiMouseEvent<Event> && std::invocable<Handler&, const D2duiMouseEventParam&>
        void register_mouse_event_handler(Handler&& handler) {
            mouse_event_handlers_[static_cast<int32_t>(Event)] = std::forward<Handler>(handler);
        }

        // Unregister a mouse event handler for the component.
        template <D2duiMouseEvent Event>
            requires ValidD2duiMouseEvent<Event>
        bool unregister_mouse_event_handler() {
            return mouse_event_handlers_.erase(static_cast<int32_t>(Event)) != 0;
        }

    protected:
        D2D1_RECT_F bounds_{};
        float scale_ = 1.0f;
        bool dirty_ = true;

    private:
        std::unordered_map<int32_t, std::function<void(const D2duiMouseEventParam&)>> mouse_event_handlers_;
    };

} // namespace d2dui

#endif // D2DUI_COMPONENT_BASE_HPP_
