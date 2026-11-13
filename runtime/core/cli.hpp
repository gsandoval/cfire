#pragma once
#include "../cfire_prelude.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <algorithm>
#if !defined(_WIN32)
#include <termios.h>
#include <unistd.h>
#endif

namespace cfire::cli {

template <typename T>
auto parse(int argc, char** argv) -> T;

template <typename T>
auto try_parse(int argc, char** argv) -> std::optional<T>;

template <typename T>
[[nodiscard]] inline auto parse() -> T {
    auto& stored = cfire::detail::process_args_storage();
    std::vector<char*> ptrs;
    std::vector<std::string> copy = stored;
    if (copy.empty()) copy.push_back("cfire_app");
    for (auto& s : copy) ptrs.push_back(s.data());
    return parse<T>(static_cast<int>(ptrs.size()), ptrs.data());
}

template <typename T>
[[nodiscard]] inline auto try_parse() -> std::optional<T> {
    auto& stored = cfire::detail::process_args_storage();
    std::vector<char*> ptrs;
    std::vector<std::string> copy = stored;
    if (copy.empty()) copy.push_back("cfire_app");
    for (auto& s : copy) ptrs.push_back(s.data());
    return try_parse<T>(static_cast<int>(ptrs.size()), ptrs.data());
}

template <typename T>
[[nodiscard]] inline auto try_parse(std::span<const std::string> args) -> std::optional<T> {
    std::vector<char*> ptrs;
    std::vector<std::string> copy;
    copy.reserve(args.size() + 1);
    copy.push_back("cfire_app");
    for (const auto& s : args) copy.push_back(s);
    for (auto& s : copy) ptrs.push_back(s.data());
    return try_parse<T>(static_cast<int>(ptrs.size()), ptrs.data());
}

template <typename T>
[[nodiscard]] inline auto try_parse(std::initializer_list<const char*> args) -> std::optional<T> {
    std::vector<char*> ptrs;
    std::vector<std::string> copy;
    copy.reserve(args.size() + 1);
    copy.push_back("cfire_app");
    for (const char* s : args) copy.emplace_back(s);
    for (auto& s : copy) ptrs.push_back(s.data());
    return try_parse<T>(static_cast<int>(ptrs.size()), ptrs.data());
}

template <typename T>
[[nodiscard]] inline auto try_parse(std::initializer_list<std::string_view> args) -> std::optional<T> {
    std::vector<char*> ptrs;
    std::vector<std::string> copy;
    copy.reserve(args.size() + 1);
    copy.push_back("cfire_app");
    for (std::string_view s : args) copy.emplace_back(s);
    for (auto& s : copy) ptrs.push_back(s.data());
    return try_parse<T>(static_cast<int>(ptrs.size()), ptrs.data());
}

template <typename T>
[[nodiscard]] inline auto parse(std::span<const std::string> args) -> T {
    std::vector<char*> ptrs;
    std::vector<std::string> copy;
    copy.reserve(args.size() + 1);
    copy.push_back("cfire_app");
    for (const auto& s : args) copy.push_back(s);
    for (auto& s : copy) ptrs.push_back(s.data());
    return parse<T>(static_cast<int>(ptrs.size()), ptrs.data());
}

template <typename T>
[[nodiscard]] inline auto parse(std::initializer_list<const char*> args) -> T {
    std::vector<char*> ptrs;
    std::vector<std::string> copy;
    copy.reserve(args.size() + 1);
    copy.push_back("cfire_app");
    for (const char* s : args) copy.emplace_back(s);
    for (auto& s : copy) ptrs.push_back(s.data());
    return parse<T>(static_cast<int>(ptrs.size()), ptrs.data());
}

template <typename T>
[[nodiscard]] inline auto parse(std::initializer_list<std::string_view> args) -> T {
    std::vector<char*> ptrs;
    std::vector<std::string> copy;
    copy.reserve(args.size() + 1);
    copy.push_back("cfire_app");
    for (std::string_view s : args) copy.emplace_back(s);
    for (auto& s : copy) ptrs.push_back(s.data());
    return parse<T>(static_cast<int>(ptrs.size()), ptrs.data());
}

[[nodiscard]] inline auto args() -> std::vector<std::string> {
    return cfire::detail::process_args_storage();
}

[[nodiscard]] inline auto subcommand() -> std::string {
    const auto& a = cfire::detail::process_args_storage();
    for (std::size_t i = 1; i < a.size(); ++i) {
        if (!a[i].empty() && a[i][0] != '-') {
            return a[i];
        }
    }
    return "";
}

[[nodiscard]] inline auto positional() -> std::vector<std::string> {
    const auto& a = cfire::detail::process_args_storage();
    std::vector<std::string> out;
    for (std::size_t i = 1; i < a.size(); ++i) {
        if (a[i] == "--") {
            for (std::size_t j = i + 1; j < a.size(); ++j) out.push_back(a[j]);
            break;
        }
        if (!a[i].empty() && a[i][0] != '-') {
            out.push_back(a[i]);
        }
    }
    return out;
}

[[nodiscard]] inline auto prompt(std::string_view message = "") -> std::optional<std::string> {
    if (!message.empty()) {
        (void)cfire::detail::fd_write_all(2, cfire::as_u8(std::span(message.data(), message.size())));
    }
    return cfire::detail::fd_read_line(0);
}

[[nodiscard]] inline auto prompt_password(std::string_view message = "") -> std::optional<std::string> {
    return cfire::detail::readln_noecho(message);
}

class FlagReader {
public:
    FlagReader(int argc, char** argv) {
        for (int i = 1; i < argc; ++i) {
            args_.emplace_back(argv[i]);
        }
    }

    void bind(std::string_view long_flag, std::string_view short_flag, std::filesystem::path& target) const {
        if (auto val = find_val(long_flag, short_flag)) {
            target = *val;
        }
    }

    void bind(std::string_view long_flag, std::string_view short_flag, std::string& target) const {
        if (auto val = find_val(long_flag, short_flag)) {
            target = std::string(*val);
        }
    }

    template <std::integral IntT>
        requires (!std::is_same_v<IntT, bool>)
    void bind(std::string_view long_flag, std::string_view short_flag, IntT& target) {
        if (auto val = find_val(long_flag, short_flag)) {
            if (auto parsed = cfire::detail::parse_int<IntT>(*val)) {
                target = *parsed;
            } else {
                errors_.push_back(std::format("invalid integer value '{}' for flag '{}'", *val, long_flag));
            }
        }
    }

    void bind(std::string_view long_flag, std::string_view short_flag, double& target) {
        if (auto val = find_val(long_flag, short_flag)) {
            if (auto parsed = cfire::detail::parse_float<double>(*val)) {
                target = *parsed;
            } else {
                errors_.push_back(std::format("invalid f64 value '{}' for flag '{}'", *val, long_flag));
            }
        }
    }

    void bind(std::string_view long_flag, std::string_view short_flag, float& target) {
        if (auto val = find_val(long_flag, short_flag)) {
            if (auto parsed = cfire::detail::parse_float<float>(*val)) {
                target = *parsed;
            } else {
                errors_.push_back(std::format("invalid f32 value '{}' for flag '{}'", *val, long_flag));
            }
        }
    }

    void bind(std::string_view long_flag, std::string_view short_flag, bool& target) const {
        for (std::size_t i = 0; i < args_.size(); ++i) {
            if (args_[i] == long_flag || (!short_flag.empty() && args_[i] == short_flag)) {
                if (i + 1 < args_.size() && (args_[i + 1] == "true" || args_[i + 1] == "false")) {
                    target = (args_[i + 1] == "true");
                } else {
                    target = true;
                }
                return;
            }
        }
    }

    [[nodiscard]] auto has_errors() const noexcept -> bool { return !errors_.empty(); }
    [[nodiscard]] auto errors() const noexcept -> const std::vector<std::string>& { return errors_; }

private:
    [[nodiscard]] auto find_val(std::string_view lflag, std::string_view sflag) const -> std::optional<std::string_view> {
        for (std::size_t i = 0; i + 1 < args_.size(); ++i) {
            if (args_[i] == lflag || (!sflag.empty() && args_[i] == sflag)) {
                return args_[i + 1];
            }
        }
        return std::nullopt;
    }
    std::vector<std::string_view> args_;
    std::vector<std::string> errors_;
};

} // namespace cfire::cli

