#pragma once
#include "../cfire_prelude.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <algorithm>
#include <iostream>

namespace cfire::cli {

template <typename T>
auto parse(int argc, char** argv) -> T;

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

    void bind(std::string_view long_flag, std::string_view short_flag, std::int32_t& target) const {
        if (auto val = find_val(long_flag, short_flag)) {
            target = static_cast<std::int32_t>(std::stoi(std::string(*val)));
        }
    }

    void bind(std::string_view long_flag, std::string_view short_flag, std::int64_t& target) const {
        if (auto val = find_val(long_flag, short_flag)) {
            target = static_cast<std::int64_t>(std::stoll(std::string(*val)));
        }
    }

    void bind(std::string_view long_flag, std::string_view short_flag, std::size_t& target) const {
        if (auto val = find_val(long_flag, short_flag)) {
            target = static_cast<std::size_t>(std::stoull(std::string(*val)));
        }
    }

    void bind(std::string_view long_flag, std::string_view short_flag, double& target) const {
        if (auto val = find_val(long_flag, short_flag)) {
            target = std::stod(std::string(*val));
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
};

} // namespace cfire::cli

