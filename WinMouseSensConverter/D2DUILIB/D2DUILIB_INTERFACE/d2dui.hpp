#pragma once

#ifndef D2DUI_HPP_
#define D2DUI_HPP_

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace d2dui {

    using Microsoft::WRL::ComPtr;

    struct D2duiColor {
        UINT32 rgb = 0;
        float alpha = 1.0f;

        friend bool operator==(const D2duiColor&, const D2duiColor&) = default;
    };

    struct D2duiTextStyle {
        std::wstring font_family = L"Segoe UI";
        std::wstring locale = L"en-us";
        float font_size = 14.0f;
        DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
        DWRITE_FONT_STYLE style = DWRITE_FONT_STYLE_NORMAL;
        DWRITE_FONT_STRETCH stretch = DWRITE_FONT_STRETCH_NORMAL;
        DWRITE_TEXT_ALIGNMENT text_alignment = DWRITE_TEXT_ALIGNMENT_CENTER;
        DWRITE_PARAGRAPH_ALIGNMENT paragraph_alignment = DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
        DWRITE_WORD_WRAPPING wrapping = DWRITE_WORD_WRAPPING_NO_WRAP;

        friend bool operator==(const D2duiTextStyle&, const D2duiTextStyle&) = default;
    };

    class D2duiContext final {
    public:
        D2duiContext() = default;
        D2duiContext(const D2duiContext&) = delete;
        D2duiContext& operator=(const D2duiContext&) = delete;
        D2duiContext(D2duiContext&&) = delete;
        D2duiContext& operator=(D2duiContext&&) = delete;

        ~D2duiContext() {
            shutdown();
        }

        HRESULT initialize(HWND hwnd, UINT dpi = USER_DEFAULT_SCREEN_DPI) noexcept {
            if (hwnd == nullptr || !IsWindow(hwnd)) return E_INVALIDARG;
            shutdown();
            hwnd_ = hwnd;
            dpi_ = dpi == 0 ? USER_DEFAULT_SCREEN_DPI : dpi;

            D2D1_FACTORY_OPTIONS options{};
            HRESULT result = D2D1CreateFactory(
                D2D1_FACTORY_TYPE_SINGLE_THREADED,
                __uuidof(ID2D1Factory),
                &options,
                reinterpret_cast<void**>(d2d_factory_.GetAddressOf()));
            if (FAILED(result)) {
                shutdown();
                return result;
            }

            result = DWriteCreateFactory(
                DWRITE_FACTORY_TYPE_SHARED,
                __uuidof(IDWriteFactory),
                reinterpret_cast<IUnknown**>(write_factory_.GetAddressOf()));
            if (FAILED(result)) shutdown();
            return result;
        }

        void shutdown() noexcept {
            in_frame_ = false;
            discard_device_resources();
            text_formats_.clear();
            write_factory_.Reset();
            d2d_factory_.Reset();
            hwnd_ = nullptr;
            dpi_ = USER_DEFAULT_SCREEN_DPI;
        }

        void set_dpi(UINT dpi) noexcept {
            dpi_ = dpi == 0 ? USER_DEFAULT_SCREEN_DPI : dpi;
            if (render_target_ != nullptr) {
                render_target_->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
            }
        }

        HRESULT begin_frame(D2duiColor clear_color) noexcept {
            if (in_frame_) return D2DERR_WRONG_STATE;
            const HRESULT result = ensure_device_resources();
            if (result != S_OK) {
                if (FAILED(result)) discard_device_resources();
                return result;
            }

            render_target_->BeginDraw();
            render_target_->SetTransform(D2D1::Matrix3x2F::Identity());
            render_target_->Clear(D2D1::ColorF(clear_color.rgb, clear_color.alpha));
            in_frame_ = true;
            return S_OK;
        }

        HRESULT end_frame() noexcept {
            if (!in_frame_ || render_target_ == nullptr) return D2DERR_WRONG_STATE;
            in_frame_ = false;
            const HRESULT result = render_target_->EndDraw();
            if (result == D2DERR_RECREATE_TARGET) discard_device_resources();
            return result;
        }

        void discard_device_resources() noexcept {
            brushes_.clear();
            render_target_.Reset();
        }

        [[nodiscard]] bool in_frame() const noexcept { return in_frame_; }
        [[nodiscard]] HWND hwnd() const noexcept { return hwnd_; }
        [[nodiscard]] UINT dpi() const noexcept { return dpi_; }
        [[nodiscard]] ID2D1HwndRenderTarget* render_target() const noexcept { return render_target_.Get(); }
        [[nodiscard]] IDWriteFactory* write_factory() const noexcept { return write_factory_.Get(); }
        [[nodiscard]] size_t brush_cache_size() const noexcept { return brushes_.size(); }
        [[nodiscard]] size_t text_format_cache_size() const noexcept { return text_formats_.size(); }

        [[nodiscard]] D2D1_SIZE_F size() const noexcept {
            return render_target_ == nullptr ? D2D1::SizeF() : render_target_->GetSize();
        }

        HRESULT get_brush(D2duiColor color, ID2D1SolidColorBrush** brush) noexcept {
            if (brush == nullptr) return E_POINTER;
            *brush = nullptr;
            if (render_target_ == nullptr) return D2DERR_WRONG_STATE;
            for (auto& entry : brushes_) {
                if (entry.color == color) {
                    *brush = entry.brush.Get();
                    return S_OK;
                }
            }

            BrushEntry entry{};
            entry.color = color;
            HRESULT result = render_target_->CreateSolidColorBrush(
                D2D1::ColorF(color.rgb, color.alpha), entry.brush.GetAddressOf());
            if (FAILED(result)) return result;
            *brush = entry.brush.Get();
            try {
                brushes_.push_back(std::move(entry));
            } catch (const std::bad_alloc&) {
                *brush = nullptr;
                return E_OUTOFMEMORY;
            } catch (...) {
                *brush = nullptr;
                return E_FAIL;
            }
            return S_OK;
        }

        HRESULT get_text_format(const D2duiTextStyle& style, IDWriteTextFormat** format) noexcept {
            if (format == nullptr) return E_POINTER;
            *format = nullptr;
            if (write_factory_ == nullptr || style.font_family.empty() || style.locale.empty() || style.font_size <= 0.0f) return E_INVALIDARG;
            for (auto& entry : text_formats_) {
                if (entry.style == style) {
                    *format = entry.format.Get();
                    return S_OK;
                }
            }

            TextFormatEntry entry{};
            try {
                entry.style = style;
            } catch (const std::bad_alloc&) {
                return E_OUTOFMEMORY;
            } catch (...) {
                return E_FAIL;
            }
            HRESULT result = write_factory_->CreateTextFormat(
                style.font_family.c_str(),
                nullptr,
                style.weight,
                style.style,
                style.stretch,
                style.font_size,
                style.locale.c_str(),
                entry.format.GetAddressOf());
            if (FAILED(result)) return result;
            result = entry.format->SetTextAlignment(style.text_alignment);
            if (SUCCEEDED(result)) result = entry.format->SetParagraphAlignment(style.paragraph_alignment);
            if (SUCCEEDED(result)) result = entry.format->SetWordWrapping(style.wrapping);
            if (FAILED(result)) return result;
            *format = entry.format.Get();
            try {
                text_formats_.push_back(std::move(entry));
            } catch (const std::bad_alloc&) {
                *format = nullptr;
                return E_OUTOFMEMORY;
            } catch (...) {
                *format = nullptr;
                return E_FAIL;
            }
            return S_OK;
        }

    private:
        struct BrushEntry {
            D2duiColor color{};
            ComPtr<ID2D1SolidColorBrush> brush;
        };

        struct TextFormatEntry {
            D2duiTextStyle style{};
            ComPtr<IDWriteTextFormat> format;
        };

        HRESULT ensure_device_resources() noexcept {
            if (hwnd_ == nullptr || d2d_factory_ == nullptr) return E_UNEXPECTED;
            RECT client{};
            if (!GetClientRect(hwnd_, &client)) {
                const DWORD error = GetLastError();
                return error == ERROR_SUCCESS ? E_FAIL : HRESULT_FROM_WIN32(error);
            }
            const UINT width = static_cast<UINT>(std::max<LONG>(0, client.right - client.left));
            const UINT height = static_cast<UINT>(std::max<LONG>(0, client.bottom - client.top));
            if (width == 0 || height == 0) return S_FALSE;

            if (render_target_ != nullptr) {
                render_target_->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
                const D2D1_SIZE_U current = render_target_->GetPixelSize();
                return current.width == width && current.height == height
                    ? S_OK
                    : render_target_->Resize(D2D1::SizeU(width, height));
            }

            const D2D1_RENDER_TARGET_PROPERTIES target_properties = D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_UNKNOWN),
                static_cast<float>(dpi_),
                static_cast<float>(dpi_));
            const D2D1_HWND_RENDER_TARGET_PROPERTIES hwnd_properties = D2D1::HwndRenderTargetProperties(
                hwnd_, D2D1::SizeU(width, height), D2D1_PRESENT_OPTIONS_NONE);
            const HRESULT result = d2d_factory_->CreateHwndRenderTarget(
                target_properties, hwnd_properties, render_target_.GetAddressOf());
            if (FAILED(result)) return result;
            render_target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            render_target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
            return S_OK;
        }

        HWND hwnd_ = nullptr;
        UINT dpi_ = USER_DEFAULT_SCREEN_DPI;
        bool in_frame_ = false;
        ComPtr<ID2D1Factory> d2d_factory_;
        ComPtr<IDWriteFactory> write_factory_;
        ComPtr<ID2D1HwndRenderTarget> render_target_;
        std::vector<BrushEntry> brushes_;
        std::vector<TextFormatEntry> text_formats_;
    };

} // namespace d2dui

#endif // D2DUI_HPP_
