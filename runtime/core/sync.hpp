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
    std::uint64_t next_cb_id = 1;
    std::vector<std::pair<std::uint64_t, std::function<void()>>> callbacks{};
    std::thread timer_thread{};
    ~ContextState() {
        {
            std::lock_guard<std::mutex> lk(mu);
            cancelled.store(true, std::memory_order_relaxed);
        }
        cv.notify_all();
        if (timer_thread.joinable()) {
            if (timer_thread.get_id() == std::this_thread::get_id()) {
                timer_thread.detach();
            } else {
                timer_thread.join();
            }
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
    auto on_cancel(std::function<void()> cb) const -> std::uint64_t {
        if (!state_ || !cb) return 0;
        std::lock_guard<std::mutex> lk(state_->mu);
        if (state_->cancelled.load(std::memory_order_relaxed)) {
            cb();
            return 0;
        }
        std::uint64_t id = state_->next_cb_id++;
        state_->callbacks.emplace_back(id, std::move(cb));
        return id;
    }
    void remove_cancel_callback(std::uint64_t id) const {
        if (!state_ || id == 0) return;
        std::lock_guard<std::mutex> lk(state_->mu);
        auto& cbs = state_->callbacks;
        cbs.erase(std::remove_if(cbs.begin(), cbs.end(),
            [id](const auto& kv) { return kv.first == id; }), cbs.end());
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
        std::vector<std::pair<std::uint64_t, std::function<void()>>> to_run;
        {
            std::lock_guard<std::mutex> lk(child_state->mu);
            if (child_state->cancelled.exchange(true, std::memory_order_relaxed)) return;
            if (!reason.empty()) child_state->reason = std::string(reason);
            to_run = std::move(child_state->callbacks);
        }
        child_state->cv.notify_all();
        for (auto& [id, cb] : to_run) {
            if (cb) cb();
        }
    };

    (void)parent.on_cancel([do_cancel, parent] {
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
        std::vector<std::pair<std::uint64_t, std::function<void()>>> to_run;
        raw->cancelled.store(true, std::memory_order_relaxed);
        raw->reason = "deadline exceeded";
        to_run = std::move(raw->callbacks);
        lk.unlock();
        raw->cv.notify_all();
        for (auto& [id, cb] : to_run) {
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
    explicit Channel(std::size_t capacity = 0) : state_(std::make_shared<State>()) {
        state_->capacity = capacity;
    }

    void send(T val) const {
        std::unique_lock<std::mutex> lk(state_->mu);
        if (state_->capacity > 0) {
            state_->cv.wait(lk, [&] {
                return state_->q.size() < state_->capacity || state_->closed;
            });
        }
        if (state_->closed) return;
        state_->q.push(std::move(val));
        state_->cv.notify_all();
    }

    [[nodiscard]] auto recv() const -> std::optional<T> {
        std::unique_lock<std::mutex> lk(state_->mu);
        state_->cv.wait(lk, [&] { return !state_->q.empty() || state_->closed; });
        if (state_->q.empty()) {
            return std::nullopt;
        }
        T val = std::move(state_->q.front());
        state_->q.pop();
        state_->cv.notify_all();
        return val;
    }

    [[nodiscard]] auto try_recv() const -> std::optional<T> {
        std::unique_lock<std::mutex> lk(state_->mu);
        if (state_->q.empty()) {
            return std::nullopt;
        }
        T val = std::move(state_->q.front());
        state_->q.pop();
        state_->cv.notify_all();
        return val;
    }

    [[nodiscard]] auto recv_timeout(std::int64_t ms) const -> std::optional<T> {
        std::unique_lock<std::mutex> lk(state_->mu);
        if (!state_->cv.wait_for(lk, std::chrono::milliseconds(ms), [&] {
            return !state_->q.empty() || state_->closed;
        })) {
            return std::nullopt;
        }
        if (state_->q.empty()) {
            return std::nullopt;
        }
        T val = std::move(state_->q.front());
        state_->q.pop();
        state_->cv.notify_all();
        return val;
    }

    [[nodiscard]] auto len() const -> std::size_t {
        std::unique_lock<std::mutex> lk(state_->mu);
        return state_->q.size();
    }

    [[nodiscard]] auto size() const -> std::size_t {
        return len();
    }

    [[nodiscard]] auto recv_ctx(const context::Context& ctx) const -> std::expected<T, cfire::Error> {
        std::unique_lock<std::mutex> lk(state_->mu);
        if (ctx.is_cancelled()) {
            return cfire::make_error("context cancelled: " + ctx.reason());
        }
        std::weak_ptr<State> weak_state = state_;
        std::uint64_t cb_id = ctx.on_cancel([weak_state] {
            if (auto st = weak_state.lock()) {
                st->cv.notify_all();
            }
        });
        auto cb_guard = cfire::scope_exit([&] { ctx.remove_cancel_callback(cb_id); });
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
        state_->cv.notify_all();
        return val;
    }

    void close() const {
        std::unique_lock<std::mutex> lk(state_->mu);
        state_->closed = true;
        state_->cv.notify_all();
    }

    struct ChannelIterator {
        const Channel* ch{nullptr};
        mutable std::optional<T> cur{};

        bool operator==(std::default_sentinel_t) const noexcept {
            return !cur.has_value();
        }
        const T& operator*() const noexcept {
            return *cur;
        }
        ChannelIterator& operator++() {
            if (ch) cur = ch->recv();
            return *this;
        }
    };

    [[nodiscard]] auto begin() const -> ChannelIterator {
        return ChannelIterator{this, recv()};
    }

    [[nodiscard]] auto end() const noexcept -> std::default_sentinel_t {
        return std::default_sentinel;
    }


private:
    struct State {
        std::mutex mu{};
        std::condition_variable cv{};
        std::queue<T> q{};
        std::size_t capacity = 0;
        bool closed = false;
    };
    std::shared_ptr<State> state_;
};

template <typename T>
[[nodiscard]] inline auto make_channel(std::size_t capacity = 0) -> Channel<T> {
    return Channel<T>{capacity};
}

template <typename T>
class MutexGuard {
public:
    struct State {
        std::mutex mtx;
        T value;
        template <typename... Args>
        explicit State(Args&&... args) : value(std::forward<Args>(args)...) {}
    };

    MutexGuard() = default;
    MutexGuard(std::shared_ptr<State> state, std::unique_lock<std::mutex> lock)
        : state_(std::move(state)), lock_(std::move(lock)) {}

    auto get() const -> const T& { return state_->value; }
    auto get() -> T& { return state_->value; }
    void set(T v) { state_->value = std::move(v); }

    auto operator->() -> T* { return &state_->value; }
    auto operator->() const -> const T* { return &state_->value; }
    auto operator*() -> T& { return state_->value; }
    auto operator*() const -> const T& { return state_->value; }

    void unlock() { if (lock_.owns_lock()) lock_.unlock(); }

private:
    std::shared_ptr<State> state_{};
    std::unique_lock<std::mutex> lock_{};
};

template <typename T>
class MutexT {
public:
    using Guard = MutexGuard<T>;
    using State = typename MutexGuard<T>::State;

    MutexT() : state_(std::make_shared<State>()) {}
    explicit MutexT(T initial) : state_(std::make_shared<State>(std::move(initial))) {}

    template <typename Fn>
    decltype(auto) with_lock(Fn&& fn) const {
        std::lock_guard<std::mutex> lk(state_->mtx);
        return fn(state_->value);
    }

    auto lock() const -> MutexGuard<T> {
        return MutexGuard<T>(state_, std::unique_lock<std::mutex>(state_->mtx));
    }

    auto try_lock() const -> std::optional<MutexGuard<T>> {
        std::unique_lock<std::mutex> lk(state_->mtx, std::try_to_lock);
        if (lk.owns_lock()) {
            return MutexGuard<T>(state_, std::move(lk));
        }
        return std::nullopt;
    }

    auto get() const -> T {
        std::lock_guard<std::mutex> lk(state_->mtx);
        return state_->value;
    }

    void set(T val) const {
        std::lock_guard<std::mutex> lk(state_->mtx);
        state_->value = std::move(val);
    }

private:
    std::shared_ptr<State> state_;
};

class Mutex {
public:
    Mutex() : mu_(std::make_shared<std::mutex>()) {}
    void lock() const { mu_->lock(); }
    void unlock() const { mu_->unlock(); }
    [[nodiscard]] auto try_lock() const -> bool { return mu_->try_lock(); }

    template <typename Fn>
    decltype(auto) with_lock(Fn&& fn) const {
        std::lock_guard<std::mutex> lk(*mu_);
        return fn();
    }

private:
    std::shared_ptr<std::mutex> mu_;
};

template <typename T>
[[nodiscard]] inline auto make_mutex(T initial) -> MutexT<T> {
    return MutexT<T>(std::move(initial));
}

[[nodiscard]] inline auto new_mutex() -> Mutex {
    return Mutex{};
}

template <typename T = int64_t>
class Atomic {
public:
    Atomic() : val_(std::make_shared<std::atomic<T>>(T{})) {}
    explicit Atomic(T initial) : val_(std::make_shared<std::atomic<T>>(initial)) {}

    auto load() const -> T { return val_->load(); }
    void store(T v) const { val_->store(v); }
    auto add(T delta) const -> T { return val_->fetch_add(delta) + delta; }
    auto sub(T delta) const -> T { return val_->fetch_sub(delta) - delta; }
    auto fetch_add(T delta) const -> T { return val_->fetch_add(delta); }
    auto fetch_sub(T delta) const -> T { return val_->fetch_sub(delta); }
    auto cas(T expected, T desired) const -> bool {
        return val_->compare_exchange_strong(expected, desired);
    }

private:
    std::shared_ptr<std::atomic<T>> val_;
};

template <typename T = int64_t>
[[nodiscard]] inline auto make_atomic(T initial = T{}) -> Atomic<T> {
    return Atomic<T>(initial);
}

} // namespace cfire::sync

