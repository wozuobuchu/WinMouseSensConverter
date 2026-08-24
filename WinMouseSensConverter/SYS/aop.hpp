#ifndef _AOP_HPP
#define _AOP_HPP

#pragma once

#include <chrono>
#include <concepts>
#include <condition_variable>
#include <mutex>
#include <utility>

namespace aop {

    template <typename T>
    class LockBox {
    public:
        class LockProxy {
        public:
            LockProxy(const LockProxy&) = delete;
            LockProxy& operator=(const LockProxy&) = delete;

            LockProxy(LockProxy&& other) noexcept : lock_(std::move(other.lock_)), obj_(std::exchange(other.obj_, nullptr)) {}

            LockProxy& operator=(LockProxy&& other) noexcept {
                if (this != &other) {
                    lock_ = std::move(other.lock_);
                    obj_ = std::exchange(other.obj_, nullptr);
                }
                return *this;
            }

            T* operator->() noexcept { return obj_; }

            T& operator*() noexcept { return *obj_; }

            const T* operator->() const noexcept { return obj_; }

            const T& operator*() const noexcept { return *obj_; }

            template <typename Predicate>
            void wait(std::condition_variable& cv, Predicate&& predicate) {
                if (!lock_.owns_lock()) return;
                cv.wait(lock_, std::forward<Predicate>(predicate));
            }

            template <typename Rep, typename Period, typename Predicate>
            bool wait_for(std::condition_variable& cv, const std::chrono::duration<Rep, Period>& timeout, Predicate&& predicate) {
                if (!lock_.owns_lock()) return false;
                return cv.wait_for(lock_, timeout, std::forward<Predicate>(predicate));
            }

            template <typename Clock, typename Duration, typename Predicate>
            bool wait_until(std::condition_variable& cv, const std::chrono::time_point<Clock, Duration>& timeout_time, Predicate&& predicate) {
                if (!lock_.owns_lock()) return false;
                return cv.wait_until(lock_, timeout_time, std::forward<Predicate>(predicate));
            }

        private:
            friend class LockBox;

            explicit LockProxy(LockBox& box) : lock_(box.mtx_), obj_(&box.obj_) {}

            std::unique_lock<std::mutex> lock_;
            T* obj_ = nullptr;
        };

        LockBox() = default;

        template <typename... Args> requires (sizeof...(Args) > 0 && std::constructible_from<T, Args...>)
        explicit LockBox(Args&&... args) : obj_(std::forward<Args>(args)...) {}

        LockBox(const LockBox&) = delete;
        LockBox(LockBox&&) = delete;
        LockBox& operator=(const LockBox&) = delete;
        LockBox& operator=(LockBox&&) = delete;

        [[nodiscard]] LockProxy acquire_lock() {
            return LockProxy(*this);
        }

    private:
        std::mutex mtx_;
        T obj_;
    };

    template <typename T>
    using LockGuard = typename LockBox<T>::LockProxy;

} // namespace aop

#endif // !_AOP_HPP
