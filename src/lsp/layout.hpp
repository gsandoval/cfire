#pragma once
// src/lsp/layout.hpp — Struct & Type Memory Layout (`sizeof` / `alignof` / Field Offsets) for LSP Hover & Inlay Hints

#include "type_query.hpp"
#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace cfire::lsp::detail {

struct TypeLayout {
    int size = 0;
    int align = 1;
};

struct FieldLayout {
    std::string name;
    int offset = 0;
    int size = 0;
    int align = 1;
};

struct StructLayout {
    int total_size = 0;
    int align = 1;
    std::vector<FieldLayout> fields;
};

inline auto compute_type_layout(
    const cfire::frontend::TypeRef& tr,
    const cfire::frontend::FileAST& ast,
    const WorkspaceContext& ws,
    std::unordered_set<std::string>& visited
) -> TypeLayout;

inline auto compute_struct_layout(
    const cfire::frontend::StructDecl& st,
    const cfire::frontend::FileAST& ast,
    const WorkspaceContext& ws,
    std::unordered_set<std::string>& visited
) -> StructLayout {
    StructLayout layout;
    if (st.fields.empty()) {
        layout.total_size = 1;
        layout.align = 1;
        return layout;
    }
    if (visited.contains(st.name)) {
        layout.total_size = 8;
        layout.align = 8;
        return layout;
    }
    visited.insert(st.name);

    int cur_offset = 0;
    int max_align = 1;

    for (const auto& f : st.fields) {
        TypeLayout tl = compute_type_layout(f.type, ast, ws, visited);
        int field_align = std::max(1, tl.align);
        max_align = std::max(max_align, field_align);

        cur_offset = (cur_offset + field_align - 1) & ~(field_align - 1);

        FieldLayout fl;
        fl.name = f.name;
        fl.offset = cur_offset;
        fl.size = tl.size;
        fl.align = field_align;
        layout.fields.push_back(fl);

        cur_offset += tl.size;
    }

    layout.align = max_align;
    layout.total_size = (cur_offset + max_align - 1) & ~(max_align - 1);
    if (layout.total_size == 0) layout.total_size = max_align;
    visited.erase(st.name);
    return layout;
}

inline auto compute_type_layout(
    const cfire::frontend::TypeRef& tr,
    const cfire::frontend::FileAST& ast,
    const WorkspaceContext& ws,
    std::unordered_set<std::string>& visited
) -> TypeLayout {
    if (tr.is_slice) return TypeLayout{24, 8}; // std::vector: 24 bytes, 8 align
    if (tr.is_map) return TypeLayout{40, 8};   // std::unordered_map
    if (tr.is_ptr || tr.is_mut_ptr) return TypeLayout{8, 8};
    if (!tr.smart_wrap.empty()) {
        if (tr.smart_wrap == "own") return TypeLayout{8, 8}; // std::unique_ptr
        if (tr.smart_wrap == "shared") return TypeLayout{16, 8}; // std::shared_ptr
        return TypeLayout{8, 8};
    }
    if (tr.is_array) {
        TypeLayout elem = tr.elem_type ? compute_type_layout(*tr.elem_type, ast, ws, visited) : TypeLayout{1, 1};
        int n = 1;
        try {
            if (!tr.array_len.empty()) n = std::stoi(tr.array_len);
        } catch (...) {}
        return TypeLayout{n * elem.size, elem.align};
    }

    std::string_view name = tr.name;
    if (name == "bool" || name == "char" || name == "i8" || name == "u8" || name == "byte") return TypeLayout{1, 1};
    if (name == "i16" || name == "u16") return TypeLayout{2, 2};
    if (name == "i32" || name == "u32" || name == "f32" || name == "int") return TypeLayout{4, 4};
    if (name == "i64" || name == "u64" || name == "f64" || name == "usize" || name == "isize" || name == "rawptr") return TypeLayout{8, 8};
    if (name == "str") return TypeLayout{24, 8};
    if (name == "net.TcpListener" || name == "net.TcpConn" || name == "sqlite.Conn") return TypeLayout{8, 8};
    if (name.starts_with("sync.Channel")) return TypeLayout{16, 8};

    // Check user structs in ast
    for (const auto& s : ast.structs) {
        if (s.name == name) {
            auto sl = compute_struct_layout(s, ast, ws, visited);
            return TypeLayout{sl.total_size, sl.align};
        }
    }
    // Check local package structs
    for (const auto& pkg : ws.local_packages) {
        for (const auto& s : pkg.ast.structs) {
            if (s.name == name || (pkg.alias + "." + s.name) == name) {
                auto sl = compute_struct_layout(s, pkg.ast, ws, visited);
                return TypeLayout{sl.total_size, sl.align};
            }
        }
    }

    return TypeLayout{8, 8}; // Default fallback
}

} // namespace cfire::lsp::detail
