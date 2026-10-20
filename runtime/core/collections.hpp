#pragma once
#include "../cfire_prelude.hpp"

#include <vector>
#include <deque>
#include <queue>
#include <unordered_set>
#include <map>
#include <algorithm>
#include <optional>
#include <functional>
#include <span>

namespace cfire::collections {

template <typename T>
class Set {
public:
    Set() = default;

    auto insert(const T& val) -> bool {
        return set_.insert(val).second;
    }

    auto insert(T&& val) -> bool {
        return set_.insert(std::move(val)).second;
    }

    [[nodiscard]] auto contains(const T& val) const -> bool {
        return set_.contains(val);
    }

    auto remove(const T& val) -> bool {
        return set_.erase(val) > 0;
    }

    [[nodiscard]] auto size() const -> std::size_t {
        return set_.size();
    }

    [[nodiscard]] auto empty() const -> bool {
        return set_.empty();
    }

    void clear() {
        set_.clear();
    }

    [[nodiscard]] auto to_slice() const -> std::vector<T> {
        std::vector<T> out;
        out.reserve(set_.size());
        for (const auto& item : set_) out.push_back(item);
        return out;
    }

private:
    std::unordered_set<T> set_{};
};

template <typename T>
class PriorityQueue {
public:
    PriorityQueue() = default;

    void push(const T& val) {
        pq_.push(val);
    }

    void push(T&& val) {
        pq_.push(std::move(val));
    }

    [[nodiscard]] auto peek() const -> std::optional<T> {
        if (pq_.empty()) return std::nullopt;
        return pq_.top();
    }

    auto pop() -> std::optional<T> {
        if (pq_.empty()) return std::nullopt;
        T val = std::move(const_cast<T&>(pq_.top()));
        pq_.pop();
        return val;
    }

    [[nodiscard]] auto size() const -> std::size_t {
        return pq_.size();
    }

    [[nodiscard]] auto empty() const -> bool {
        return pq_.empty();
    }

private:
    std::priority_queue<T> pq_{};
};

template <typename T>
class Deque {
public:
    Deque() = default;

    void push_back(const T& val) {
        deq_.push_back(val);
    }

    void push_back(T&& val) {
        deq_.push_back(std::move(val));
    }

    void push_front(const T& val) {
        deq_.push_front(val);
    }

    void push_front(T&& val) {
        deq_.push_front(std::move(val));
    }

    auto pop_back() -> std::optional<T> {
        if (deq_.empty()) return std::nullopt;
        T val = std::move(deq_.back());
        deq_.pop_back();
        return val;
    }

    auto pop_front() -> std::optional<T> {
        if (deq_.empty()) return std::nullopt;
        T val = std::move(deq_.front());
        deq_.pop_front();
        return val;
    }

    [[nodiscard]] auto at(std::size_t idx) const -> std::optional<T> {
        if (idx >= deq_.size()) return std::nullopt;
        return deq_[idx];
    }

    [[nodiscard]] auto size() const -> std::size_t {
        return deq_.size();
    }

    [[nodiscard]] auto empty() const -> bool {
        return deq_.empty();
    }

    void clear() {
        deq_.clear();
    }

private:
    std::deque<T> deq_{};
};

} // namespace cfire::collections

// --- `core:sort` (In-Place Slice Sorting & Binary Search) ---
namespace cfire::sort {

template <typename T>
inline void slice(std::vector<T>& s) {
    std::ranges::sort(s);
}

template <typename T, typename Pred>
inline void slice_by(std::vector<T>& s, Pred&& comp) {
    std::ranges::sort(s, std::forward<Pred>(comp));
}

template <typename T>
[[nodiscard]] inline auto binary_search(std::span<const T> s, const T& val) -> std::optional<std::size_t> {
    auto it = std::ranges::lower_bound(s, val);
    if (it != s.end() && *it == val) {
        return static_cast<std::size_t>(std::distance(s.begin(), it));
    }
    return std::nullopt;
}

} // namespace cfire::sort

