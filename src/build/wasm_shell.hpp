#pragma once
// src/build/wasm_shell.hpp — WebAssembly HTML Shell Templates & Emscripten Discovery

#include "toolchain_layout.hpp"

namespace cfire::build {

// --- WebAssembly & HTML Shell Templates ---

inline constexpr std::string_view kCanvasShellHtml = R"HTML(<!DOCTYPE html>
<html lang="en-us">
  <head>
    <meta charset="utf-8">
    <meta http-equiv="Content-Type" content="text/html; charset=utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>cfire Web Application</title>
    <style>
      html, body {
        width: 100%;
        height: 100%;
        margin: 0;
        padding: 0;
        overflow: hidden;
        background-color: #121214;
        font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
        color: #e0e0e0;
      }
      #canvas {
        width: 100vw;
        height: 100vh;
        display: block;
        outline: none;
        background-color: #121214;
      }
      #status {
        position: absolute;
        top: 50%;
        left: 50%;
        transform: translate(-50%, -50%);
        font-size: 14px;
        color: #888;
        text-align: center;
        pointer-events: none;
      }
      .spinner {
        height: 28px;
        width: 28px;
        margin: 10px auto;
        border: 3px solid rgba(255,255,255,0.1);
        border-top-color: #6366f1;
        border-radius: 50%;
        animation: spin 0.8s linear infinite;
      }
      @keyframes spin { to { transform: rotate(360deg); } }
    </style>
  </head>
  <body>
    <canvas id="canvas" oncontextmenu="event.preventDefault()" tabindex="-1"></canvas>
    <div id="status"><div class="spinner"></div>Loading WebAssembly...</div>
    <script type="text/javascript">
      var statusElement = document.getElementById('status');
      var canvas = document.getElementById('canvas');

      function syncGlfwState() {
        if (typeof GLFW !== 'undefined' && GLFW.active && canvas) {
          GLFW.active.width = canvas.width;
          GLFW.active.height = canvas.height;
        }
      }

      var Module = {
        preRun: [],
        postRun: [function() {
          if (statusElement) statusElement.style.display = 'none';
          syncGlfwState();
          if (typeof GLFW !== 'undefined' && GLFW.calculateMouseCoords) {
            var origCalc = GLFW.calculateMouseCoords;
            GLFW.calculateMouseCoords = function(pageX, pageY) {
              syncGlfwState();
              return origCalc.call(this, pageX, pageY);
            };
          }
          window.addEventListener('resize', syncGlfwState);
        }],
        print: function(text) {
          console.log(text);
        },
        printErr: function(text) {
          console.error(text);
        },
        canvas: canvas,
        setStatus: function(text) {
          if (!text) {
            if (statusElement) statusElement.style.display = 'none';
          } else {
            if (statusElement) statusElement.innerText = text;
          }
        },
        totalDependencies: 0,
        monitorRunDependencies: function(left) {
          this.totalDependencies = Math.max(this.totalDependencies, left);
          Module.setStatus(left ? 'Preparing... (' + (this.totalDependencies-left) + '/' + this.totalDependencies + ')' : 'All downloads complete.');
        }
      };
      window.cfireStageFile = function(name, uint8Array) {
        if (typeof FS !== 'undefined' && FS.writeFile) {
          var clean = String(name || 'dropped.bin').replace(/[^a-zA-Z0-9._-]/g, '_');
          try { FS.mkdir('/tmp'); } catch (e) {}
          FS.writeFile('/' + clean, uint8Array);
          FS.writeFile('/tmp/' + clean, uint8Array);
          window.dispatchEvent(new CustomEvent('cfire-file-staged', { detail: { name: clean, path: '/' + clean } }));
        }
      };
      window.addEventListener('dragover', function(e) { e.preventDefault(); });
      window.addEventListener('drop', function(e) {
        e.preventDefault();
        if (!e.dataTransfer || !e.dataTransfer.files) return;
        Array.from(e.dataTransfer.files).forEach(function(file) {
          file.arrayBuffer().then(function(buf) {
            window.cfireStageFile(file.name, new Uint8Array(buf));
          });
        });
      });
      window.onerror = function() {
        Module.setStatus('Exception thrown, see JavaScript console');
      };
    </script>
    {{{ SCRIPT }}}
  </body>
</html>
)HTML";

inline constexpr std::string_view kConsoleShellHtml = R"HTML(<!DOCTYPE html>
<html lang="en-us">
  <head>
    <meta charset="utf-8">
    <title>cfire WebAssembly Console</title>
    <style>
      body {
        margin: 0;
        padding: 20px;
        background-color: #1e1e24;
        color: #f0f0f0;
        font-family: ui-monospace, Menlo, Monaco, "Cascadia Mono", monospace;
      }
      #output {
        width: 100%;
        height: 80vh;
        background: #111;
        color: #0f0;
        border: 1px solid #333;
        border-radius: 6px;
        padding: 12px;
        box-sizing: border-box;
        font-family: inherit;
        font-size: 14px;
        overflow-y: auto;
        white-space: pre-wrap;
      }
    </style>
  </head>
  <body>
    <h3>cfire WebAssembly Console</h3>
    <pre id="output"></pre>
    <script type="text/javascript">
      var out = document.getElementById('output');
      var Module = {
        print: function(text) {
          if (out) { out.textContent += text + "\n"; out.scrollTop = out.scrollHeight; }
          console.log(text);
        },
        printErr: function(text) {
          if (out) { out.textContent += "[err] " + text + "\n"; out.scrollTop = out.scrollHeight; }
          console.error(text);
        }
      };
      window.cfireStageFile = function(name, uint8Array) {
        if (typeof FS !== 'undefined' && FS.writeFile) {
          var clean = String(name || 'dropped.bin').replace(/[^a-zA-Z0-9._-]/g, '_');
          try { FS.mkdir('/tmp'); } catch (e) {}
          FS.writeFile('/' + clean, uint8Array);
          FS.writeFile('/tmp/' + clean, uint8Array);
          if (out) { out.textContent += "[fs] staged /" + clean + " (" + uint8Array.byteLength + " bytes)\n"; out.scrollTop = out.scrollHeight; }
          window.dispatchEvent(new CustomEvent('cfire-file-staged', { detail: { name: clean, path: '/' + clean } }));
        }
      };
      window.addEventListener('dragover', function(e) { e.preventDefault(); });
      window.addEventListener('drop', function(e) {
        e.preventDefault();
        if (!e.dataTransfer || !e.dataTransfer.files) return;
        Array.from(e.dataTransfer.files).forEach(function(file) {
          file.arrayBuffer().then(function(buf) {
            window.cfireStageFile(file.name, new Uint8Array(buf));
          });
        });
      });
    </script>
    {{{ SCRIPT }}}
  </body>
</html>
)HTML";

[[nodiscard]] inline auto resolve_emscripten_compiler() -> std::expected<std::string, std::string> {
    if (std::getenv("EM_CACHE") == nullptr) {
        std::error_code ec;
        auto tmp_cache = std::filesystem::path("/tmp/cfire_emscripten_cache");
        std::filesystem::create_directories(tmp_cache, ec);
        if (!ec) {
            auto canon_cache = std::filesystem::canonical(tmp_cache, ec);
            ::setenv("EM_CACHE", (!ec ? canon_cache : tmp_cache).c_str(), 1);
        } else if (const char* home = std::getenv("HOME")) {
            auto cache_dir = std::filesystem::path(home) / ".emscripten_cache";
            ::setenv("EM_CACHE", cache_dir.c_str(), 1);
        }
    }
    int rc = 0;
    auto which_out = bridge::run_command_capture("which em++ 2>/dev/null", rc);
    if (rc == 0 && !which_out.empty()) {
        while (!which_out.empty() && (which_out.back() == '\n' || which_out.back() == '\r' || which_out.back() == ' ')) {
            which_out.pop_back();
        }
        if (!which_out.empty()) return which_out;
    }
    if (const char* emsdk = std::getenv("EMSDK")) {
        auto empp = std::filesystem::path(emsdk) / "upstream" / "emscripten" / "em++";
        if (std::filesystem::exists(empp)) return empp.string();
    }
    if (const char* home = std::getenv("HOME")) {
        auto empp = std::filesystem::path(home) / ".emsdk" / "upstream" / "emscripten" / "em++";
        if (std::filesystem::exists(empp)) return empp.string();
    }
    return std::unexpected(
        "em++ (Emscripten compiler) not found in PATH or EMSDK.\n"
        "To compile for WebAssembly (--target=wasm), please install the Emscripten SDK:\n"
        "  git clone https://github.com/emscripten-core/emsdk.git ~/.emsdk\n"
        "  ~/.emsdk/emsdk install latest && ~/.emsdk/emsdk activate latest\n"
        "  source ~/.emsdk/emsdk_env.sh"
    );
}


} // namespace cfire::build
