#pragma once
// src/codegen/reflect.hpp — Struct Reflection, Arbitrary Fuzzing, CLI, JSON & SQLite Code Generation

namespace cfire::frontend {

inline void Emitter::emit_struct_reflection(const StructDecl& st) {
    writeln();
    writeln("template <>");
    writeln(std::format("struct cfire::reflect_struct<{}> {{", st.name));
    ++indent_;
    writeln(std::format("static constexpr std::string_view name = \"{}\";", st.name));
    writeln(std::format("static constexpr std::size_t field_count = {};", st.fields.size()));
    writeln("static constexpr std::array<std::string_view, " + std::to_string(st.fields.size()) + "> field_names = {");
    ++indent_;
    for (const auto& fld : st.fields) {
        writeln(std::format("\"{}\",", fld.name));
    }
    --indent_;
    writeln("};");
    writeln("static constexpr std::array<std::string_view, " + std::to_string(st.fields.size()) + "> field_tags = {");
    ++indent_;
    for (const auto& fld : st.fields) {
        writeln(std::format("R\"tag({})tag\",", fld.raw_tag));
    }
    --indent_;
    writeln("};");
    --indent_;
    writeln("};");

    auto tags_it = cf_.struct_tags.find(st.name);

    bool can_emit_arbitrary = true;
    for (const auto& fld : st.fields) {
        if (is_non_value_field_type(fld.type, 0)) {
            can_emit_arbitrary = false;
            break;
        }
    }
    if (can_emit_arbitrary) {
        writeln();
        writeln("template <>");
        writeln(std::format("struct cfire::testing_detail::Arbitrary<{}> {{", st.name));
        ++indent_;
        writeln(std::format("static auto generate(cfire::testing_detail::FuzzRng& rng, std::size_t iter) -> {} {{", st.name));
        ++indent_;
        writeln(std::format("{} out{{}};", st.name));
        for (std::size_t fi = 0; fi < st.fields.size(); ++fi) {
            std::string fname = sanitize_cpp_ident(st.fields[fi].name);
            writeln(std::format("out.{} = cfire::testing_detail::Arbitrary<std::decay_t<decltype(out.{})>>::generate(rng, iter + {});",
                fname, fname, fi));
            if (tags_it != cf_.struct_tags.end() && fi < tags_it->second.size() && tags_it->second[fi].validation.has_rules) {
                const auto& v = tags_it->second[fi].validation;
                const auto& ftype = tags_it->second[fi].field_type;
                std::string val_expr = ftype.is_optional ? ("(*out." + fname + ")") : ("out." + fname);
                if (ftype.is_optional) {
                    writeln(std::format("if (out.{}.has_value()) {{", fname));
                    ++indent_;
                }
                if (v.has_min) {
                    writeln(std::format("if ({0} < static_cast<std::decay_t<decltype({0})>>({1})) {0} = static_cast<std::decay_t<decltype({0})>>({1});", val_expr, v.min_raw));
                }
                if (v.has_max) {
                    writeln(std::format("if ({0} > static_cast<std::decay_t<decltype({0})>>({1})) {0} = static_cast<std::decay_t<decltype({0})>>({1});", val_expr, v.max_raw));
                }
                if (!v.oneof.empty()) {
                    writeln(std::format("{0} = \"{1}\";", val_expr, escape_unescaped_quotes(v.oneof.front())));
                } else {
                    std::size_t req_min_len = v.has_min_len ? v.min_len : (v.non_empty ? 1u : 0u);
                    if (req_min_len > 0 && ftype.name == "str") {
                        writeln(std::format("while ({0}.size() < {1}u) {0} += 'a';", val_expr, req_min_len));
                    } else if (req_min_len > 0 && ftype.is_slice) {
                        writeln(std::format("while ({0}.size() < {1}u) {0}.emplace_back();", val_expr, req_min_len));
                    }
                    if (v.has_max_len && (ftype.name == "str" || ftype.is_slice)) {
                        writeln(std::format("if ({0}.size() > {1}u) {0}.resize({1}u);", val_expr, v.max_len));
                    }
                }
                if (ftype.is_optional) {
                    --indent_;
                    writeln("}");
                }
            }
        }
        writeln("return out;");
        --indent_;
        writeln("}");
        --indent_;
        writeln("};");
    }

    if (tags_it == cf_.struct_tags.end()) return;
    const auto& tags = tags_it->second;

    bool has_cli = false, has_json = false, has_desc = false, has_db = false;
    for (const auto& t : tags) {
        if (!t.cli_long.empty()) has_cli = true;
        if (!t.json_key.empty()) has_json = true;
        if (t.custom_tags.contains("desc")) has_desc = true;
        if (!t.db_column.empty()) has_db = true;
    }
    (void)has_json; (void)has_desc;

    if (has_cli && used_core_.contains("cli")) {
        writeln();
        auto emit_cli_defaults = [&]() {
            for (const auto& t : tags) {
                if (auto dit = t.custom_tags.find("default"); dit != t.custom_tags.end()) {
                    std::string fname = sanitize_cpp_ident(t.field_name);
                    const std::string& dval = dit->second;
                    if (t.field_type.name == "str") {
                        writeln(std::format("out.{} = \"{}\";", fname, escape_unescaped_quotes(dval)));
                    } else if (t.field_type.name == "bool") {
                        writeln(std::format("out.{} = {};", fname, (dval == "true" || dval == "1") ? "true" : "false"));
                    } else if (!dval.empty()) {
                        writeln(std::format("out.{} = static_cast<{}>({});", fname, lower_raw_owning_type(t.field_type), dval));
                    }
                }
            }
        };

        writeln("template <>");
        writeln(std::format("inline auto cfire::cli::try_parse<{}>(int argc, char** argv) -> std::optional<{}> {{", st.name, st.name));
        ++indent_;
        writeln(std::format("{} out{{}};", st.name));
        emit_cli_defaults();
        writeln("cfire::cli::FlagReader reader(argc, argv);");
        for (const auto& t : tags) {
            if (!t.cli_long.empty()) {
                writeln(std::format("reader.bind(\"{}\", \"{}\", out.{});", t.cli_long, t.cli_short, sanitize_cpp_ident(t.field_name)));
            }
        }
        writeln("if (reader.has_errors()) return std::nullopt;");
        writeln("return out;");
        --indent_;
        writeln("}");

        writeln();
        writeln("template <>");
        writeln(std::format("inline auto cfire::cli::parse<{}>(int argc, char** argv) -> {} {{", st.name, st.name));
        ++indent_;
        writeln(std::format("{} out{{}};", st.name));
        emit_cli_defaults();
        writeln("cfire::cli::FlagReader reader(argc, argv);");
        for (const auto& t : tags) {
            if (!t.cli_long.empty()) {
                writeln(std::format("reader.bind(\"{}\", \"{}\", out.{});", t.cli_long, t.cli_short, sanitize_cpp_ident(t.field_name)));
            }
        }
        writeln("return out;");
        --indent_;
        writeln("}");
    }

    bool can_emit_json = can_emit_arbitrary;
    if (can_emit_json) {
        for (const auto& t : tags) {
            std::string jkey = (t.json_key == "-") ? "" : (!t.json_key.empty() ? t.json_key : t.field_name);
            if (!jkey.empty() && !can_emit_json_field_type(t.field_type, 0)) {
                can_emit_json = false;
                break;
            }
        }
    }

    if (can_emit_json && used_core_.contains("json")) {
        writeln();
        writeln("template <>");
        writeln(std::format("inline auto cfire::json::parse_struct<{}>(simdjson::ondemand::object obj) -> std::expected<{}, cfire::Error> {{", st.name, st.name));
        ++indent_;
        writeln("(void)obj;");
        writeln(std::format("{} out{{}};", st.name));
        for (const auto& t : tags) {
            std::string jkey = (t.json_key == "-") ? "" : (!t.json_key.empty() ? t.json_key : t.field_name);
            std::string fname = sanitize_cpp_ident(t.field_name);
            if (auto dit = t.custom_tags.find("default"); dit != t.custom_tags.end()) {
                const std::string& dval = dit->second;
                if (t.field_type.name == "str") {
                    writeln(std::format("out.{} = \"{}\";", fname, escape_unescaped_quotes(dval)));
                } else if (t.field_type.name == "bool") {
                    writeln(std::format("out.{} = {};", fname, (dval == "true" || dval == "1") ? "true" : "false"));
                } else if (!dval.empty() && (t.field_type.name == "u64" || t.field_type.name == "usize" ||
                           t.field_type.name == "i64" || t.field_type.name == "isize" ||
                           t.field_type.name == "i32" || t.field_type.name == "f64" || t.field_type.name == "f32")) {
                    writeln(std::format("out.{} = static_cast<{}>({});", fname, lower_raw_owning_type(t.field_type), dval));
                }
            }
            if (!jkey.empty()) {
                if (t.field_type.is_optional) {
                    std::string inner_cpp = lower_raw_owning_type(t.field_type);
                    writeln(std::format("CFIRE_TRY_ASSIGN(out.{}, cfire::json::get_opt<{}>(obj, \"{}\"));", fname, inner_cpp, jkey));
                } else if (t.field_type.is_slice && t.field_type.elem_type) {
                    std::string elem_cpp = lower_owning_type(*t.field_type.elem_type);
                    writeln(std::format("CFIRE_TRY_ASSIGN(out.{}, cfire::json::get_vec<{}>(obj, \"{}\"));", fname, elem_cpp, jkey));
                } else if (t.field_type.name == "str") {
                    writeln(std::format("CFIRE_TRY_ASSIGN(out.{}, cfire::json::get_str(obj, \"{}\", out.{}));", fname, jkey, fname));
                } else if (t.field_type.name == "u64" || t.field_type.name == "usize") {
                    writeln(std::format("CFIRE_TRY_ASSIGN(out.{}, cfire::json::get_u64(obj, \"{}\", out.{}));", fname, jkey, fname));
                } else if (t.field_type.name == "i64" || t.field_type.name == "isize") {
                    writeln(std::format("CFIRE_TRY_ASSIGN(out.{}, cfire::json::get_i64(obj, \"{}\", out.{}));", fname, jkey, fname));
                } else if (t.field_type.name == "i32") {
                    writeln(std::format("CFIRE_TRY_ASSIGN(out.{}, cfire::json::get_i32(obj, \"{}\", out.{}));", fname, jkey, fname));
                } else if (t.field_type.name == "f64") {
                    writeln(std::format("CFIRE_TRY_ASSIGN(out.{}, cfire::json::get_f64(obj, \"{}\", out.{}));", fname, jkey, fname));
                } else if (t.field_type.name == "f32") {
                    writeln(std::format("CFIRE_TRY_ASSIGN(out.{}, cfire::json::get_f32(obj, \"{}\", out.{}));", fname, jkey, fname));
                } else if (t.field_type.name == "bool") {
                    writeln(std::format("CFIRE_TRY_ASSIGN(out.{}, cfire::json::get_bool(obj, \"{}\", out.{}));", fname, jkey, fname));
                } else if (is_local_type(t.field_type.name)) {
                    writeln(std::format("CFIRE_TRY_ASSIGN(out.{}, cfire::json::get_obj<{}>(obj, \"{}\"));", fname, lower_raw_owning_type(t.field_type), jkey));
                }
                if (t.validation.has_rules) {
                    const auto& v = t.validation;
                    std::string val_expr = t.field_type.is_optional ? ("(*out." + fname + ")") : ("out." + fname);
                    if (t.field_type.is_optional) {
                        writeln(std::format("if (out.{}.has_value()) {{", fname));
                        ++indent_;
                    }
                    if (v.has_min) {
                        writeln(std::format(
                            "if ({0} < static_cast<std::decay_t<decltype({0})>>({1})) return cfire::make_error(std::format(\"validation failed for field '{2}': value {{}} is less than min {1}\", {0}));",
                            val_expr, v.min_raw, jkey));
                    }
                    if (v.has_max) {
                        writeln(std::format(
                            "if ({0} > static_cast<std::decay_t<decltype({0})>>({1})) return cfire::make_error(std::format(\"validation failed for field '{2}': value {{}} exceeds max {1}\", {0}));",
                            val_expr, v.max_raw, jkey));
                    }
                    if (v.non_empty) {
                        writeln(std::format(
                            "if ({0}.empty()) return cfire::make_error(\"validation failed for field '{1}': must not be empty\");",
                            val_expr, jkey));
                    }
                    if (v.has_min_len) {
                        writeln(std::format(
                            "if ({0}.size() < {1}u) return cfire::make_error(std::format(\"validation failed for field '{2}': length {{}} is less than min_len {1}\", {0}.size()));",
                            val_expr, v.min_len, jkey));
                    }
                    if (v.has_max_len) {
                        writeln(std::format(
                            "if ({0}.size() > {1}u) return cfire::make_error(std::format(\"validation failed for field '{2}': length {{}} exceeds max_len {1}\", {0}.size()));",
                            val_expr, v.max_len, jkey));
                    }
                    if (!v.oneof.empty()) {
                        std::string cond;
                        std::string allowed_list;
                        for (std::size_t oi = 0; oi < v.oneof.size(); ++oi) {
                            if (oi > 0) {
                                cond += " && ";
                                allowed_list += "|";
                            }
                            cond += std::format("({} != \"{}\")", val_expr, escape_unescaped_quotes(v.oneof[oi]));
                            allowed_list += escape_unescaped_quotes(v.oneof[oi]);
                        }
                        writeln(std::format(
                            "if ({0}) return cfire::make_error(std::format(\"validation failed for field '{1}': '{{}}' is not one of [{2}]\", {3}));",
                            cond, jkey, allowed_list, val_expr));
                    }
                    if (t.field_type.is_optional) {
                        --indent_;
                        writeln("}");
                    }
                }
            }
        }
        writeln("return out;");
        --indent_;
        writeln("}");

        writeln();
        writeln("template <>");
        writeln(std::format("inline auto cfire::json::stringify_struct<{}>(const {}& val) -> std::string {{", st.name, st.name));
        ++indent_;
        writeln("(void)val;");
        writeln("std::string out = \"{\";");
        bool first_j = true;
        for (const auto& t : tags) {
            std::string jkey = (t.json_key == "-") ? "" : (!t.json_key.empty() ? t.json_key : t.field_name);
            std::string fname = sanitize_cpp_ident(t.field_name);
            if (!jkey.empty()) {
                std::string prefix = first_j ? "" : ",";
                first_j = false;
                if (t.field_type.is_optional || t.field_type.is_slice || is_local_type(t.field_type.name)) {
                    writeln(std::format("out += std::format(\"{}\\\"{}\\\":{{}}\", cfire::json::stringify(val.{}));", prefix, jkey, fname));
                } else if (t.field_type.name == "str") {
                    writeln(std::format("out += std::format(\"{}\\\"{}\\\":\\\"{{}}\\\"\", cfire::json::escape_string(val.{}));", prefix, jkey, fname));
                } else if (t.field_type.name == "bool") {
                    writeln(std::format("out += std::format(\"{}\\\"{}\\\":{{}}\", val.{} ? \"true\" : \"false\");", prefix, jkey, fname));
                } else {
                    writeln(std::format("out += std::format(\"{}\\\"{}\\\":{{}}\", val.{});", prefix, jkey, fname));
                }
            }
        }
        writeln("out += \"}\";");
        writeln("return out;");
        --indent_;
        writeln("}");

        writeln();
        writeln("template <>");
        writeln(std::format("inline auto cfire::json::schema_struct<{}>() -> std::string {{", st.name));
        ++indent_;
        writeln("std::string props = \"{\";");
        writeln("std::string req = \"[\";");
        bool first_p = true;
        bool first_r = true;
        for (const auto& t : tags) {
            std::string jkey = (t.json_key == "-") ? "" : (!t.json_key.empty() ? t.json_key : t.field_name);
            if (jkey.empty()) continue;
            std::string p_prefix = first_p ? "" : ",";
            first_p = false;
            std::string f_cpp = lower_raw_owning_type(t.field_type);
            std::string desc_str;
            if (auto dit = t.custom_tags.find("desc"); dit != t.custom_tags.end()) {
                desc_str = dit->second;
            }
            std::string extra_schema;
            if (t.validation.has_rules) {
                const auto& v = t.validation;
                auto append_kv = [&](std::string_view kv) {
                    if (!extra_schema.empty()) extra_schema += ",";
                    extra_schema += kv;
                };
                if (v.has_min) append_kv(std::format("\"minimum\":{}", v.min_raw));
                if (v.has_max) append_kv(std::format("\"maximum\":{}", v.max_raw));
                std::size_t eff_min_len = v.has_min_len ? v.min_len : (v.non_empty ? 1u : 0u);
                if (eff_min_len > 0) {
                    append_kv(std::format("\"{}\":{}", t.field_type.is_slice ? "minItems" : "minLength", eff_min_len));
                }
                if (v.has_max_len) {
                    append_kv(std::format("\"{}\":{}", t.field_type.is_slice ? "maxItems" : "maxLength", v.max_len));
                }
                if (!v.oneof.empty()) {
                    std::string enum_arr = "\"enum\":[";
                    for (std::size_t oi = 0; oi < v.oneof.size(); ++oi) {
                        if (oi > 0) enum_arr += ",";
                        enum_arr += std::format("\"{}\"", escape_unescaped_quotes(v.oneof[oi]));
                    }
                    enum_arr += "]";
                    append_kv(enum_arr);
                }
            }
            if (!desc_str.empty() || !extra_schema.empty()) {
                writeln(std::format("props += std::format(\"{}\\\"{}\\\":{{}}\", cfire::json::with_schema_meta(cfire::json::schema<{}>(), R\"desc({})desc\", R\"meta({})meta\"));",
                    p_prefix, jkey, f_cpp, desc_str, extra_schema));
            } else {
                writeln(std::format("props += std::format(\"{}\\\"{}\\\":{{}}\", cfire::json::schema<{}>());",
                    p_prefix, jkey, f_cpp));
            }
            if (!t.field_type.is_optional) {
                std::string r_prefix = first_r ? "" : ",";
                first_r = false;
                writeln(std::format("req += \"{}\\\"{}\\\"\";", r_prefix, jkey));
            }
        }
        writeln("props += \"}\";");
        writeln("req += \"]\";");
        writeln("return std::format(R\"({{\"type\":\"object\",\"properties\":{},\"required\":{},\"additionalProperties\":false}})\", props, req);");
        --indent_;
        writeln("}");
    }

    if (has_db && used_core_.contains("sqlite")) {
        writeln();
        writeln("template <>");
        writeln(std::format("inline auto cfire::sqlite::map_row<{}>(sqlite3_stmt* stmt) -> {} {{", st.name, st.name));
        ++indent_;
        writeln(std::format("return {}{{", st.name));
        ++indent_;
        int col_idx = 0;
        for (const auto& t : tags) {
            if (!t.db_column.empty()) {
                std::string col_fn = "col_str";
                if (t.field_type.is_slice && t.field_type.elem_type) {
                    if (t.field_type.elem_type->name == "f32") col_fn = "col_f32_vec";
                    else if (t.field_type.elem_type->name == "byte") col_fn = "col_bytes";
                    else if (t.field_type.elem_type->name == "u8") col_fn = "col_u8_vec";
                } else if (t.field_type.name == "u64" || t.field_type.name == "usize") col_fn = "col_u64";
                else if (t.field_type.name == "i64" || t.field_type.name == "isize") col_fn = "col_i64";
                else if (t.field_type.name == "i32") col_fn = "col_i32";
                else if (t.field_type.name == "f64") col_fn = "col_f64";
                else if (t.field_type.name == "f32") col_fn = "col_f32";
                else if (t.field_type.name == "bool") col_fn = "col_bool";
                writeln(std::format(".{} = cfire::sqlite::{}(stmt, {}),", sanitize_cpp_ident(t.field_name), col_fn, col_idx++));
            }
        }
        --indent_;
        writeln("};");
        --indent_;
        writeln("}");
    }
}

inline void Emitter::emit_enum_reflection(const EnumDecl& en) {
    writeln();
    writeln("template <>");
    writeln(std::format("struct cfire::reflect_enum<{}> {{", en.name));
    ++indent_;
    writeln("static constexpr bool is_enum = true;");
    writeln(std::format("static constexpr std::string_view name = \"{}\";", en.name));
    writeln(std::format("static constexpr std::size_t variant_count = {};", en.variants.size()));
    writeln(std::format("static constexpr std::array<std::string_view, {}> variant_names = {{", en.variants.size()));
    ++indent_;
    for (const auto& v : en.variants) {
        writeln(std::format("\"{}\",", v.name));
    }
    --indent_;
    writeln("};");
    writeln(std::format("static auto from_string(std::string_view s) -> std::optional<{}> {{", en.name));
    writeln(std::format("    return {}::from_string(s);", en.name));
    writeln("}");
    --indent_;
    writeln("};");

    bool is_plain = std::all_of(en.variants.begin(), en.variants.end(), [](const auto& v) { return v.payload_types.empty(); });
    if (is_plain && !en.variants.empty()) {
        writeln();
        writeln("template <>");
        writeln(std::format("struct cfire::testing_detail::Arbitrary<{}> {{", en.name));
        ++indent_;
        writeln(std::format("static auto generate(cfire::testing_detail::FuzzRng&, std::size_t iter) -> {} {{", en.name));
        writeln(std::format("    auto opt = {}::from_i32(static_cast<std::int32_t>(iter % {}));", en.name, en.variants.size()));
        writeln(std::format("    return opt ? *opt : {}::{}();", en.name, en.variants[0].name));
        writeln("}");
        --indent_;
        writeln("};");
    }

    if (used_core_.contains("json")) {
        writeln();
        writeln("template <>");
        writeln(std::format("inline auto cfire::json::stringify_struct<{}>(const {}& val) -> std::string {{", en.name, en.name));
        writeln("    return std::format(\"\\\"{}\\\"\", val.to_string());");
        writeln("}");

        writeln();
        writeln("template <>");
        writeln(std::format("inline auto cfire::json::schema_struct<{}>() -> std::string {{", en.name));
        std::string enum_list;
        for (std::size_t i = 0; i < en.variants.size(); ++i) {
            if (i > 0) enum_list += ",";
            enum_list += std::format("\"{}\"", en.variants[i].name);
        }
        writeln(std::format("    return R\"({{\"type\":\"string\",\"enum\":[{}]}})\";", enum_list));
        writeln("}");
    }
}

} // namespace cfire::frontend
