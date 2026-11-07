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
        }
        writeln("return out;");
        --indent_;
        writeln("}");
        --indent_;
        writeln("};");
    }

    auto it = cf_.struct_tags.find(st.name);
    if (it == cf_.struct_tags.end()) return;
    const auto& tags = it->second;

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
        writeln("template <>");
        writeln(std::format("inline auto cfire::cli::try_parse<{}>(int argc, char** argv) -> std::optional<{}> {{", st.name, st.name));
        ++indent_;
        writeln(std::format("{} out{{}};", st.name));
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
            if (!desc_str.empty()) {
                writeln(std::format("props += std::format(\"{}\\\"{}\\\":{{}}\", cfire::json::with_description(cfire::json::schema<{}>(), R\"desc({})desc\"));",
                    p_prefix, jkey, f_cpp, desc_str));
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

} // namespace cfire::frontend
