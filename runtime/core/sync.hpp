#pragma once
#include "../cfire_prelude.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <expected>

namespace cfire::sync::context {

struct ContextState {
    std::atomic<bool> cancelled{false};
    std::string reason{"cancelled"};
    mutable std::mutex mu{};
    mutable std::condition_variable cv{};
    std::vector<std::function<void()>> callbacks{};
    std::thread timer_thread{};
    ~ContextState() {
        if (timer_thread.joinable()) {
            timer_thread.detach();
        }
    }
};

class Context {
public:
    Context() : state_(std::make_shared<ContextState>()) {}
    explicit Context(std::shared_ptr<ContextState> state) : state_(std::move(state)) {}

    [[nodiscard]] bool is_cancelled() const noexcept {
        return state_ && state_->cancelled.load(std::memory_order_relaxed);
    }
    [[nodiscard]] bool done() const noexcept {
        return is_cancelled();
    }
    [[nodiscard]] std::string reason() const {
        if (!state_) return "";
        std::lock_guard<std::mutex> lk(state_->mu);
        return state_->reason;
    }
    void on_cancel(std::function<void()> cb) const {
        if (!state_ || !cb) return;
        std::lock_guard<std::mutex> lk(state_->mu);
        if (state_->cancelled.load(std::memory_order_relaxed)) {
            cb();
            return;
        }
        state_->callbacks.push_back(std::move(cb));
    }
    void wait() const {
        if (!state_) return;
        std::unique_lock<std::mutex> lk(state_->mu);
        state_->cv.wait(lk, [&] { return state_->cancelled.load(std::memory_order_relaxed); });
    }
    [[nodiscard]] bool wait_for(std::int64_t ms) const {
        if (!state_) return false;
        std::unique_lock<std::mutex> lk(state_->mu);
        return state_->cv.wait_for(lk, std::chrono::milliseconds(ms), [&] {
            return state_->cancelled.load(std::memory_order_relaxed);
        });
    }

    [[nodiscard]] auto raw_state() const noexcept -> const std::shared_ptr<ContextState>& {
        return state_;
    }

private:
    std::shared_ptr<ContextState> state_;
};

class CancelFunc {
public:
    CancelFunc() = default;
    explicit CancelFunc(std::function<void(std::string_view)> fn) : fn_(std::move(fn)) {}

    void operator()() const {
        if (fn_) fn_("cancelled");
    }
    void operator()(std::string_view reason) const {
        if (fn_) fn_(reason);
    }

private:
    std::function<void(std::string_view)> fn_;
};

[[nodiscard]] inline auto background() -> Context {
    return Context(std::make_shared<ContextState>());
}

[[nodiscard]] inline auto with_cancel(const Context& parent) -> std::pair<Context, CancelFunc> {
    auto child_state = std::make_shared<ContextState>();
    Context child(child_state);

    auto do_cancel = [child_state](std::string_view reason) {
        std::vector<std::function<void()>> to_run;
        {
            std::lock_guard<std::mutex> lk(child_state->mu);
            if (child_state->cancelled.exchange(true, std::memory_order_relaxed)) return;
            if (!reason.empty()) child_state->reason = std::string(reason);
            to_run = std::move(child_state->callbacks);
        }
        child_state->cv.notify_all();
        for (auto& cb : to_run) {
            if (cb) cb();
        }
    };

    parent.on_cancel([do_cancel, parent] {
        do_cancel(parent.reason());
    });

    CancelFunc fn([do_cancel](std::string_view r) { do_cancel(r); });
    return {child, fn};
}

[[nodiscard]] inline auto with_timeout(const Context& parent, std::int64_t ms) -> std::pair<Context, CancelFunc> {
    auto [child, cancel] = with_cancel(parent);
    if (ms <= 0) {
        cancel("deadline exceeded");
        return {child, cancel};
    }

    auto raw = child.raw_state();
    std::thread timer([raw, ms] {
        std::unique_lock<std::mutex> lk(raw->mu);
        if (raw->cv.wait_for(lk, std::chrono::milliseconds(ms), [&] {
            return raw->cancelled.load(std::memory_order_relaxed);
        })) {
            return;
        }
        std::vector<std::function<void()>> to_run;
        raw->cancelled.store(true, std::memory_order_relaxed);
        raw->reason = "deadline exceeded";
        to_run = std::move(raw->callbacks);
        lk.unlock();
        raw->cv.notify_all();
        for (auto& cb : to_run) {
            if (cb) cb();
        }
    });

    {
        std::lock_guard<std::mutex> lk(raw->mu);
        raw->timer_thread = std::move(timer);
    }

    return {child, cancel};
}

} // namespace cfire::sync::context

namespace cfire::context {
    using namespace cfire::sync::context;
}


namespace cfire::sync {

template <typename T>
class Channel {
public:
    Channel() : state_(std::make_shared<State>()) {}

    void send(T val) const {
        std::unique_lock<std::mutex> lk(state_->mu);
        state_->q.push(std::move(val));
        state_->cv.notify_one();
    }

    [[nodiscard]] auto recv() const -> std::optional<T> {
        std::unique_lock<std::mutex> lk(state_->mu);
        state_->cv.wait(lk, [&] { return !state_->q.empty() || state_->closed; });
        if (state_->q.empty()) {
            return std::nullopt;
        }
        T val = std::move(state_->q.front());
        state_->q.pop();
        return val;
    }

    [[nodiscard]] auto recv_ctx(const context::Context& ctx) const -> std::expected<T, cfire::Error> {
        std::unique_lock<std::mutex> lk(state_->mu);
        if (ctx.is_cancelled()) {
            return cfire::make_error("context cancelled: " + ctx.reason());
        }
        ctx.on_cancel([this] {
            state_->cv.notify_all();
        });
        state_->cv.wait(lk, [&] {
            return !state_->q.empty() || state_->closed || ctx.is_cancelled();
        });
        if (ctx.is_cancelled()) {
            return cfire::make_error("context cancelled: " + ctx.reason());
        }
        if (state_->q.empty()) {
            return cfire::make_error("channel closed");
        }
        T val = std::move(state_->q.front());
        state_->q.pop();
        return val;
    }

    void close() const {
        std::unique_lock<std::mutex> lk(state_->mu);
        state_->closed = true;
        state_->cv.notify_all();
    }

private:
    struct State {
        std::mutex mu{};
        std::condition_variable cv{};
        std::queue<T> q{};
        bool closed = false;
    };
    std::shared_ptr<State> state_;
};

template <typename T>
[[nodiscard]] inline auto make_channel() -> Channel<T> {
    return Channel<T>{};
}

} // namespace cfire::sync

