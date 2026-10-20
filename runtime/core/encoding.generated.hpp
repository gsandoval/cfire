#include "cfire_prelude.hpp"

[[nodiscard]] inline auto hex_encode(const std::string& s) -> std::string;
[[nodiscard]] inline auto hex_char_val(std::int64_t c) -> std::int64_t;
[[nodiscard]] inline auto hex_decode(const std::string& s) -> std::expected<std::string, cfire::Error>;
[[nodiscard]] inline auto base64_encode(const std::string& s, bool url_safe) -> std::string;
[[nodiscard]] inline auto b64_char_val(std::int64_t c) -> std::int64_t;
[[nodiscard]] inline auto base64_decode(const std::string& s) -> std::expected<std::string, cfire::Error>;
[[nodiscard]] inline auto csv_parse(const std::string& s) -> std::vector<std::vector<std::string>>;
[[nodiscard]] inline auto csv_stringify(const std::vector<std::vector<std::string>>& rows) -> std::string;

[[nodiscard]] inline auto hex_encode(const std::string& s) -> std::string {
    std::string digits = "0123456789abcdef";
    std::string res = "";
    std::size_t i = 0;
    auto n = s.size();
    while (i < n) {
        std::int64_t val = s[i];
        if (val < 0) {
            val = (val + 256);
        }
        auto hi = ((val / 16) % 16);
        auto lo = (val % 16);
        res += cfire::slice(digits, hi, (hi + 1));
        res += cfire::slice(digits, lo, (lo + 1));
        i = (i + 1);
    }
    return res;
}

[[nodiscard]] inline auto hex_char_val(std::int64_t c) -> std::int64_t {
    if ((c >= 48) && (c <= 57)) {
        return (c - 48);
    }
    if ((c >= 97) && (c <= 102)) {
        return ((c - 97) + 10);
    }
    if ((c >= 65) && (c <= 70)) {
        return ((c - 65) + 10);
    }
    return -1;
}

[[nodiscard]] inline auto hex_decode(const std::string& s) -> std::expected<std::string, cfire::Error> {
    if ((s.size() % 2) != 0) {
        return cfire::make_error("odd hex string length");
    }
    std::string res = "";
    std::size_t i = 0;
    auto n = s.size();
    while (i < n) {
        auto hi = hex_char_val(s[i]);
        auto lo = hex_char_val(s[(i + 1)]);
        if ((hi < 0) || (lo < 0)) {
            return cfire::make_error("invalid hex character");
        }
        auto byte_val = ((hi * 16) + lo);
        (void)(res.push_back(byte_val));
        i = (i + 2);
    }
    return res;
}

[[nodiscard]] inline auto base64_encode(const std::string& s, bool url_safe) -> std::string {
    std::string tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (url_safe) {
        tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    }
    std::string res = "";
    std::size_t i = 0;
    auto n = s.size();
    while (i < n) {
        auto remaining = (n - i);
        std::int64_t b0 = s[i];
        if (b0 < 0) {
            b0 = (b0 + 256);
        }
        std::int64_t b1 = 0;
        if (remaining > 1) {
            b1 = s[(i + 1)];
            if (b1 < 0) {
                b1 = (b1 + 256);
            }
        }
        std::int64_t b2 = 0;
        if (remaining > 2) {
            b2 = s[(i + 2)];
            if (b2 < 0) {
                b2 = (b2 + 256);
            }
        }
        auto triple = (((b0 * 65536) + (b1 * 256)) + b2);
        auto c0 = ((triple / 262144) % 64);
        auto c1 = ((triple / 4096) % 64);
        auto c2 = ((triple / 64) % 64);
        auto c3 = (triple % 64);
        res += cfire::slice(tbl, c0, (c0 + 1));
        res += cfire::slice(tbl, c1, (c1 + 1));
        if (remaining > 1) {
            res += cfire::slice(tbl, c2, (c2 + 1));
        } else {
            if (!url_safe) {
                res += "=";
            }
        }
        if (remaining > 2) {
            res += cfire::slice(tbl, c3, (c3 + 1));
        } else {
            if (!url_safe) {
                res += "=";
            }
        }
        if (remaining >= 3) {
            i = (i + 3);
        } else {
            i = (i + remaining);
        }
    }
    return res;
}

[[nodiscard]] inline auto b64_char_val(std::int64_t c) -> std::int64_t {
    if ((c >= 65) && (c <= 90)) {
        return (c - 65);
    }
    if ((c >= 97) && (c <= 122)) {
        return ((c - 97) + 26);
    }
    if ((c >= 48) && (c <= 57)) {
        return ((c - 48) + 52);
    }
    if ((c == 43) || (c == 45)) {
        return 62;
    }
    if ((c == 47) || (c == 95)) {
        return 63;
    }
    if (c == 61) {
        return -1;
    }
    return -2;
}

[[nodiscard]] inline auto base64_decode(const std::string& s) -> std::expected<std::string, cfire::Error> {
    std::string res = "";
    std::vector<std::int64_t> quad = {};
    std::size_t i = 0;
    auto n = s.size();
    while (i < n) {
        auto c = s[i];
        if ((((c == 32) || (c == 9)) || (c == 10)) || (c == 13)) {
            i = (i + 1);
            continue;
        }
        auto val = b64_char_val(c);
        if (val == -2) {
            return cfire::make_error("invalid base64 character");
        }
        (void)(quad.push_back(val));
        if (quad.size() == 4) {
            auto b0 = quad[0];
            auto b1 = quad[1];
            auto b2 = quad[2];
            auto b3 = quad[3];
            if ((b0 < 0) || (b1 < 0)) {
                return cfire::make_error("malformed base64 padding");
            }
            std::int64_t b2_val = b2;
            if (b2 < 0) {
                b2_val = 0;
            }
            std::int64_t b3_val = b3;
            if (b3 < 0) {
                b3_val = 0;
            }
            auto triple = ((((b0 * 262144) + (b1 * 4096)) + (b2_val * 64)) + b3_val);
            auto byte0 = ((triple / 65536) % 256);
            auto byte1 = ((triple / 256) % 256);
            auto byte2 = (triple % 256);
            (void)(res.push_back(byte0));
            if (b2 != -1) {
                (void)(res.push_back(byte1));
            }
            if (b3 != -1) {
                (void)(res.push_back(byte2));
            }
            quad = {};
        }
        i = (i + 1);
    }
    if (quad.size() > 0) {
        if (quad.size() == 1) {
            return cfire::make_error("truncated base64 input");
        }
        auto b0 = quad[0];
        auto b1 = quad[1];
        if ((b0 < 0) || (b1 < 0)) {
            return cfire::make_error("malformed base64");
        }
        std::int64_t b2 = -1;
        if (quad.size() > 2) {
            b2 = quad[2];
        }
        std::int64_t b2_val = b2;
        if (b2 < 0) {
            b2_val = 0;
        }
        auto triple = (((b0 * 262144) + (b1 * 4096)) + (b2_val * 64));
        auto byte0 = ((triple / 65536) % 256);
        auto byte1 = ((triple / 256) % 256);
        (void)(res.push_back(byte0));
        if (b2 != -1) {
            (void)(res.push_back(byte1));
        }
    }
    return res;
}

[[nodiscard]] inline auto csv_parse(const std::string& s) -> std::vector<std::vector<std::string>> {
    std::vector<std::vector<std::string>> rows = {};
    std::vector<std::string> current_row = {};
    std::string current_field = "";
    bool in_quotes = false;
    std::size_t i = 0;
    auto n = s.size();
    while (i < n) {
        auto c = s[i];
        if (in_quotes) {
            if (c == 34) {
                if (((i + 1) < n) && (s[(i + 1)] == 34)) {
                    current_field += "\"";
                    i = (i + 2);
                } else {
                    in_quotes = false;
                    i = (i + 1);
                }
            } else {
                current_field += cfire::slice(s, i, (i + 1));
                i = (i + 1);
            }
        } else {
            if (c == 34) {
                in_quotes = true;
                i = (i + 1);
            } else {
                if (c == 44) {
                    (void)(current_row.push_back(current_field));
                    current_field = "";
                    i = (i + 1);
                } else {
                    if (c == 13) {
                        if (((i + 1) < n) && (s[(i + 1)] == 10)) {
                            i = (i + 1);
                        }
                        (void)(current_row.push_back(current_field));
                        current_field = "";
                        (void)(rows.push_back(current_row));
                        current_row = {};
                        i = (i + 1);
                    } else {
                        if (c == 10) {
                            (void)(current_row.push_back(current_field));
                            current_field = "";
                            (void)(rows.push_back(current_row));
                            current_row = {};
                            i = (i + 1);
                        } else {
                            current_field += cfire::slice(s, i, (i + 1));
                            i = (i + 1);
                        }
                    }
                }
            }
        }
    }
    if ((current_field.size() > 0) || (current_row.size() > 0)) {
        (void)(current_row.push_back(current_field));
        (void)(rows.push_back(current_row));
    }
    return rows;
}

[[nodiscard]] inline auto csv_stringify(const std::vector<std::vector<std::string>>& rows) -> std::string {
    std::string res = "";
    std::size_t r = 0;
    auto num_rows = rows.size();
    while (r < num_rows) {
        auto row = rows[r];
        std::size_t c = 0;
        auto num_cols = row.size();
        while (c < num_cols) {
            if (c > 0) {
                res += ",";
            }
            auto cell = row[c];
            bool needs_quote = false;
            std::size_t k = 0;
            auto cell_len = cell.size();
            while (k < cell_len) {
                auto ch = cell[k];
                if ((((ch == 44) || (ch == 34)) || (ch == 13)) || (ch == 10)) {
                    needs_quote = true;
                    break;
                }
                k = (k + 1);
            }
            if (needs_quote) {
                res += "\"";
                std::size_t j = 0;
                while (j < cell_len) {
                    auto ch = cell[j];
                    if (ch == 34) {
                        res += "\"\"";
                    } else {
                        res += cfire::slice(cell, j, (j + 1));
                    }
                    j = (j + 1);
                }
                res += "\"";
            } else {
                res += cell;
            }
            c = (c + 1);
        }
        res += "\r\n";
        r = (r + 1);
    }
    return res;
}
