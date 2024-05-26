#pragma once
#include <coroutine>
#include <variant>

template <typename R>
struct result_t {
    constexpr result_t() = default;
    
    template <typename T>
    // co_return
    constexpr void return_value(T&& r) noexcept {
        set_value(std::forward<T>(r));
    }

    template <typename T>
    // co_yield
    constexpr auto yield_value(T&& r) noexcept {
        set_value(std::forward<T>(r));
        return std::suspend_always{};
    }

    template<typename T>
    constexpr void set_value(T&& r) {
        r_.template emplace<R>(std::forward<T>(r));
    }

    constexpr decltype(auto) result() const & {
        if (auto p = std::get_if<std::exception_ptr>(&r_)) {
            std::rethrow_exception(*p);
        }
        if(auto p = std::get_if<R>(&r_)) {
            return *p;
        }
        // painic
        std::terminate();
    }

    constexpr decltype(auto) result() const && {
        if (auto p = std::get_if<std::exception_ptr>(&r_)) {
            std::rethrow_exception(*p);
        }
        if(auto p = std::get_if<R>(&r_)) {
            return std::move(*p);
        }
        // painic
        std::terminate();
    }

    constexpr bool has_result() const {
        return r_.index() != 0;
    }

    void unhandled_exception() {
        r_ = std::current_exception();
    }
protected:
    std::variant<std::monostate, std::exception_ptr, R> r_;
};

template <>
struct result_t<void> {
    constexpr result_t() = default;
    
    // co_return
    void return_void() noexcept {
        
    }

    // co_yield
    constexpr auto yield_value() noexcept {
        return std::suspend_always{};
    }

    void result() {
        if (auto* p = std::get_if<std::exception_ptr>(&r_)) {
            std::rethrow_exception(*p);
        }
    }

    constexpr bool has_result() {
        return r_.index() != 0;
    }

    void unhandled_exception() {
        r_ = std::current_exception();
    }
private:
    std::variant<std::monostate, std::exception_ptr> r_;
};
