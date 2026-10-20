#include "cfire_prelude.hpp"

struct BuildEmitter;

struct BuildEmitter {
    std::string buf{};
};

template <>
struct cfire::reflect_struct<BuildEmitter> {
    static constexpr std::string_view name = "BuildEmitter";
    static constexpr std::size_t field_count = 1;
    static constexpr std::array<std::string_view, 1> field_names = {
        "buf",
    };
    static constexpr std::array<std::string_view, 1> field_tags = {
        R"tag()tag",
    };
};

auto BuildEmitter__write(BuildEmitter& e, const std::string& s) -> void;
auto BuildEmitter__writeln(BuildEmitter& e, const std::string& s) -> void;
[[nodiscard]] auto buildsys_starts_with(const std::string& s, const std::string& prefix) -> bool;
[[nodiscard]] auto selfhost_generate_cmake_file(const cfire::build::BuildPlan& plan) -> std::string;

auto BuildEmitter__write(BuildEmitter& e, const std::string& s) -> void {
    e.buf += s;
}

auto BuildEmitter__writeln(BuildEmitter& e, const std::string& s) -> void {
    e.buf += (s + "\n");
}

[[nodiscard]] auto buildsys_starts_with(const std::string& s, const std::string& prefix) -> bool {
    auto n = s.size();
    auto pn = prefix.size();
    if (pn > n) {
        return false;
    }
    return (cfire::slice(s, 0, pn) == prefix);
}

[[nodiscard]] auto selfhost_generate_cmake_file(const cfire::build::BuildPlan& plan) -> std::string {
    auto e = BuildEmitter{.buf = ""};
    std::string cm_dir = "${";
    cm_dir += "CMAKE_CURRENT_SOURCE_DIR}";
    (void)(BuildEmitter__writeln(e, "cmake_minimum_required(VERSION 3.25)"));
    (void)(BuildEmitter__writeln(e, "project(cfire_ejected_app LANGUAGES C CXX)"));
    (void)(BuildEmitter__writeln(e, "set(CMAKE_CXX_STANDARD 23)"));
    (void)(BuildEmitter__writeln(e, "set(CMAKE_CXX_STANDARD_REQUIRED OFF)\n"));
    (void)(BuildEmitter__writeln(e, (("include_directories(\"" + cm_dir) + "\")\n")));
    auto n_pkg = plan.pkg_config_deps.size();
    if (n_pkg > 0) {
        (void)(BuildEmitter__writeln(e, "find_package(PkgConfig REQUIRED)"));
        std::size_t pi = 0;
        while (pi < n_pkg) {
            auto dep_name = plan.pkg_config_deps[pi];
            (void)(BuildEmitter__writeln(e, (((("pkg_check_modules(DEP_" + cfire::frontend::codegen_usize_to_str(pi)) + " REQUIRED IMPORTED_TARGET ") + dep_name) + ")")));
            pi = (pi + 1);
        }
        (void)(BuildEmitter__writeln(e, ""));
    }
    std::size_t di = 0;
    auto n_deps = plan.external_deps.size();
    while (di < n_deps) {
        auto dep = plan.external_deps[di];
        auto vendor_prefix = ("vendor/" + dep.name);
        auto n_srcs = dep.sources.size();
        if (n_srcs > 0) {
            std::string srcs = "";
            std::size_t si = 0;
            while (si < n_srcs) {
                if (si > 0) {
                    srcs += " ";
                }
                srcs += (((("\"" + vendor_prefix) + "/") + dep.sources[si]) + "\"");
                si = (si + 1);
            }
            (void)(BuildEmitter__writeln(e, (((("add_library(" + dep.name) + " STATIC ") + srcs) + ")")));
            (void)(BuildEmitter__writeln(e, (((((("target_include_directories(" + dep.name) + " PUBLIC \"") + cm_dir) + "/") + vendor_prefix) + "\")")));
            auto defines_str = cfire::build::buildsys_emit_dep_defines(dep);
            (void)(BuildEmitter__write(e, defines_str));
            (void)(BuildEmitter__writeln(e, ""));
        } else {
            if ((dep.driver == "CMake") || (dep.driver == "cmake")) {
                (void)(BuildEmitter__writeln(e, (((("if (EXISTS \"" + cm_dir) + "/") + vendor_prefix) + "/CMakeLists.txt\")")));
                (void)(BuildEmitter__writeln(e, (("    add_subdirectory(" + vendor_prefix) + ")")));
                (void)(BuildEmitter__writeln(e, "endif()\n"));
            }
        }
        di = (di + 1);
    }
    (void)(BuildEmitter__writeln(e, "add_executable(app main.cpp)"));
    (void)(BuildEmitter__writeln(e, "if (CMAKE_CXX_COMPILER_ID MATCHES \"Clang|AppleClang\")"));
    (void)(BuildEmitter__writeln(e, "    target_compile_options(app PRIVATE -std=c++2b)"));
    (void)(BuildEmitter__writeln(e, "endif()"));
    (void)(BuildEmitter__writeln(e, (("target_include_directories(app PRIVATE \"" + cm_dir) + "\")")));
    std::size_t pki = 0;
    while (pki < n_pkg) {
        (void)(BuildEmitter__writeln(e, (("target_link_libraries(app PRIVATE PkgConfig::DEP_" + cfire::frontend::codegen_usize_to_str(pki)) + ")")));
        pki = (pki + 1);
    }
    std::size_t dli = 0;
    while (dli < n_deps) {
        auto dep = plan.external_deps[dli];
        (void)(BuildEmitter__writeln(e, (("target_link_libraries(app PRIVATE " + dep.name) + ")")));
        dli = (dli + 1);
    }
    std::size_t ldi = 0;
    auto n_ld = plan.ldflags.size();
    while (ldi < n_ld) {
        auto flag = plan.ldflags[ldi];
        (void)(BuildEmitter__writeln(e, (("target_link_libraries(app PRIVATE \"" + flag) + "\")")));
        ldi = (ldi + 1);
    }
    std::size_t ali = 0;
    auto n_al = plan.auto_linked.size();
    while (ali < n_al) {
        auto al = plan.auto_linked[ali];
        if (buildsys_starts_with(al, "vendor:raylib")) {
            (void)(BuildEmitter__writeln(e, "target_link_libraries(app PRIVATE raylib)"));
        }
        ali = (ali + 1);
    }
    return e.buf;
}
