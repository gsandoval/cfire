#pragma once
// src/docgen/styles.hpp — Shared HTML/CSS Theme & Navigation Layout for `cfire doc`

#include <format>
#include <string>
#include <string_view>

namespace cfire::doc {
namespace detail {

[[nodiscard]] inline auto html_escape(std::string_view sv) -> std::string {
    std::string out;
    out.reserve(sv.size() + 32);
    for (char c : sv) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

[[nodiscard]] inline auto shared_css() -> std::string {
    return R"CSS(
:root {
    --bg: #FAFAF9;
    --bg-card: #FFFFFF;
    --bg-card-hover: #F5F5F4;
    --bg-code: #18181B;
    --border: #E7E5E4;
    --border-dark: #D6D3D1;
    --text: #1C1917;
    --text-bright: #0C0A09;
    --muted: #57534E;
    --accent: #1D4ED8;
    --accent-hover: #1E40AF;
    --ok: #047857;
    --warn: #B45309;
    --err: #B91C1C;
    --font-sans: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", sans-serif;
    --font-mono: ui-monospace, "JetBrains Mono", SFMono-Regular, Menlo, Monaco, Consolas, monospace;
}
*, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }
html, body {
    width: 100%;
    max-width: 100%;
    overflow-x: hidden;
    margin: 0;
    padding: 0;
}
body {
    font-family: var(--font-sans);
    background: var(--bg);
    color: var(--text);
    line-height: 1.65;
    min-height: 100vh;
    -webkit-font-smoothing: antialiased;
}
::selection { background: #1C1917; color: #FFFFFF; }
a { color: var(--accent); text-decoration: none; transition: color 0.15s ease; }
a:hover { color: var(--accent-hover); text-decoration: underline; }

/* Sticky Edge-to-Edge Navigation Bar */
.navbar {
    position: sticky; top: 0; z-index: 100;
    width: 100%;
    background: rgba(255, 255, 255, 0.96);
    backdrop-filter: blur(12px);
    -webkit-backdrop-filter: blur(12px);
    border-bottom: 1px solid var(--border);
}
.navbar-inner {
    max-width: 1120px;
    margin: 0 auto;
    padding: 0.75rem 1.25rem;
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 1rem;
    width: 100%;
    box-sizing: border-box;
}
.brand {
    display: flex; align-items: center; gap: 0.65rem;
    font-weight: 800; font-size: 1.25rem; color: var(--text);
    text-decoration: none; flex-shrink: 0;
}
.brand:hover { text-decoration: none; }
.brand img.brand-icon {
    width: 30px; height: 30px; object-fit: contain;
}
.brand span.brand-text {
    font-size: 1.2rem; font-weight: 850; letter-spacing: -0.02em; color: var(--text);
}
.brand span.fire { color: #EA580C; font-weight: 850; }
.brand-badge {
    font-family: var(--font-mono); font-size: 0.65rem; font-weight: 700;
    background: #F5F5F4; border: 1px solid var(--border); color: #57534E;
    padding: 0.12rem 0.45rem; border-radius: 4px; margin-left: 0.25rem;
}
.nav-links { display: flex; align-items: center; gap: 1.25rem; font-size: 0.92rem; font-weight: 600; }
.nav-links a {
    color: var(--muted); transition: all 0.15s ease;
    padding: 0.25rem 0.4rem; border-radius: 5px;
}
.nav-links a:hover { color: var(--text); text-decoration: none; background: #F5F5F4; }
.nav-links a.active { color: var(--text); text-decoration: none; font-weight: 750; background: #F5F5F4; }
.btn-nav-primary {
    background: #1C1917 !important; color: #FFFFFF !important;
    padding: 0.4rem 0.9rem !important; border-radius: 6px !important; font-size: 0.85rem !important; font-weight: 600 !important;
    transition: background 0.15s !important; border: none;
}
.btn-nav-primary:hover { background: #292524 !important; text-decoration: none !important; }

/* Page container with robust bounds */
.container {
    width: 100%;
    max-width: 1120px;
    margin: 0 auto;
    padding: 2.5rem 1.25rem 5rem;
    box-sizing: border-box;
    min-width: 0;
}

/* Documentation Layout (Guide & Reference) */
.doc-layout {
    display: grid;
    grid-template-columns: 260px minmax(0, 1fr);
    gap: 2.5rem;
    align-items: start;
    width: 100%;
    min-width: 0;
}
.sidebar {
    position: sticky;
    top: 4.5rem;
    max-height: calc(100vh - 5.5rem);
    overflow-y: auto;
    background: #FFFFFF;
    border: 1px solid var(--border);
    border-radius: 8px;
    padding: 1.25rem;
    box-shadow: 0 1px 2px rgba(0,0,0,0.02);
}
.sidebar-summary { display: none; }
.sidebar h4 {
    font-size: 0.75rem; font-weight: 800; text-transform: uppercase;
    letter-spacing: 0.08em; color: var(--muted); margin-bottom: 0.75rem;
}
.sidebar ul { list-style: none; margin-bottom: 1.25rem; }
.sidebar li { margin-bottom: 0.25rem; }
.sidebar a {
    display: block; padding: 0.35rem 0.6rem; border-radius: 5px;
    color: var(--muted); font-size: 0.88rem; text-decoration: none;
    transition: all 0.15s ease;
    white-space: nowrap; overflow: hidden; text-overflow: ellipsis;
}
.sidebar a:hover {
    background: #F5F5F4; color: var(--text); text-decoration: none;
}
.sidebar a.active {
    background: #F5F5F4; color: var(--accent); font-weight: 700;
}
.sidebar a code { background: none; border: none; padding: 0; color: inherit; font-size: inherit; }

main {
    min-width: 0;
    max-width: 100%;
    width: 100%;
    overflow-wrap: break-word;
    word-break: break-word;
}
main h1 {
    font-size: clamp(1.85rem, 4vw, 2.5rem); font-weight: 800;
    color: var(--text); letter-spacing: -0.03em; margin-bottom: 0.75rem;
}
main h2 {
    font-size: 1.5rem; font-weight: 800; color: var(--text);
    letter-spacing: -0.02em; margin: 2.5rem 0 0.85rem; padding-bottom: 0.4rem;
    border-bottom: 1px solid var(--border);
}
main h3 {
    font-size: 1.2rem; font-weight: 700; color: var(--text);
    margin: 1.75rem 0 0.5rem;
}
main p { color: #44403C; font-size: 0.98rem; line-height: 1.7; margin-bottom: 1rem; }
main ul, main ol { margin: 0 0 1.25rem 1.5rem; color: #44403C; font-size: 0.98rem; line-height: 1.7; }
main li { margin-bottom: 0.4rem; }

/* Hero section */
.hero { text-align: center; padding: 3rem 0.5rem 3.5rem; }
.badge {
    display: inline-block; padding: 0.35rem 0.85rem; border-radius: 999px;
    font-family: var(--font-mono); font-size: 0.75rem; font-weight: 700;
    letter-spacing: 0.06em; text-transform: uppercase;
    background: #FFFFFF; border: 1px solid var(--border); color: var(--accent);
    margin-bottom: 1.5rem;
}
h1.hero-title {
    font-size: clamp(2rem, 5vw, 3.25rem); font-weight: 850;
    letter-spacing: -0.03em; color: var(--text); margin-bottom: 1.25rem;
    line-height: 1.15;
}
.subtitle {
    max-width: 44rem; margin: 0 auto 2.25rem; font-size: clamp(1rem, 2.5vw, 1.2rem);
    color: var(--muted); line-height: 1.6;
}
.cta-row { display: flex; justify-content: center; gap: 0.85rem; flex-wrap: wrap; margin-bottom: 2rem; }
.btn {
    padding: 0.75rem 1.5rem; border-radius: 8px; font-weight: 600; font-size: 0.95rem;
    border: 1px solid var(--border); transition: all 0.15s ease; display: inline-flex;
    align-items: center; gap: 0.5rem; text-decoration: none; cursor: pointer;
}
.btn:hover { text-decoration: none; }
.btn-primary { background: #1C1917; color: #FFFFFF; border-color: #1C1917; }
.btn-primary:hover { background: #292524; border-color: #292524; }
.btn-secondary { background: #FFFFFF; color: var(--text); border-color: var(--border); }
.btn-secondary:hover { background: #F5F5F4; border-color: var(--border-dark); }

/* Install Box */
.install-bar {
    display: inline-flex; align-items: center; justify-content: space-between; gap: 0.75rem;
    background: #FFFFFF; border: 1px solid var(--border); border-radius: 8px;
    padding: 0.65rem 1rem; font-family: var(--font-mono); font-size: 0.88rem;
    color: #292524; max-width: 100%; overflow-x: auto;
}
.install-bar .prompt { color: var(--muted); font-weight: 700; user-select: none; }
.copy-btn {
    background: #1C1917; color: #FFFFFF; border: none;
    padding: 0.35rem 0.75rem; border-radius: 5px; font-size: 0.75rem; font-weight: 700;
    font-family: var(--font-mono); cursor: pointer; transition: background 0.15s; shrink-0;
}
.copy-btn:hover { background: #292524; }

/* Stats ribbon */
.stats-ribbon {
    display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 1rem;
    margin: 3rem 0 2rem;
}
.stat-pill {
    background: #FFFFFF; border: 1px solid var(--border); border-radius: 8px;
    padding: 1.25rem 1rem; text-align: center;
}
.stat-val {
    font-size: 1.85rem; font-weight: 850; color: var(--text); font-family: var(--font-mono);
}
.stat-lbl {
    font-size: 0.76rem; color: var(--muted); font-weight: 700; text-transform: uppercase;
    letter-spacing: 0.06em; margin-top: 0.35rem;
}

/* Grids & Cards */
.grid-2 { display: grid; grid-template-columns: repeat(auto-fit, minmax(340px, 1fr)); gap: 1.25rem; }
.grid-3 { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 1.25rem; }
.card {
    background: #FFFFFF; border: 1px solid var(--border); border-radius: 8px;
    padding: 1.5rem; transition: border-color 0.15s ease;
}
.card:hover { border-color: var(--border-dark); }
.card-num {
    font-family: var(--font-mono); font-size: 0.78rem; font-weight: 800;
    color: var(--accent); margin-bottom: 0.5rem; text-transform: uppercase;
}
.card h3 { font-size: 1.15rem; font-weight: 750; margin-bottom: 0.5rem; color: var(--text); }
.card p { color: var(--muted); font-size: 0.92rem; line-height: 1.6; margin: 0; }

/* Code Blocks: Strict Horizontal Scroll Containment */
pre {
    background: var(--bg-code); border: 1px solid #27272A;
    border-radius: 8px; padding: 1.1rem 1.25rem; overflow-x: auto;
    max-width: 100%; min-width: 0; box-sizing: border-box;
    font-family: var(--font-mono); font-size: 0.85rem; line-height: 1.65;
    color: #F4F4F5; -webkit-overflow-scrolling: touch;
}
code {
    font-family: var(--font-mono); font-size: 0.88em;
    background: #F5F5F4; border: 1px solid var(--border);
    padding: 0.12rem 0.35rem; border-radius: 4px; color: #1C1917;
    word-break: break-word;
}
pre code { background: none; border: none; padding: 0; color: inherit; word-break: normal; }
.code-header {
    font-family: var(--font-mono); font-size: 0.75rem; font-weight: 700;
    text-transform: uppercase; letter-spacing: 0.05em; color: var(--muted);
    margin-bottom: 0.45rem; display: flex; justify-content: space-between;
}
.code-header span.tag-cf { color: #60A5FA; }
.code-header span.tag-cpp { color: #F59E0B; }

.section-title {
    font-size: clamp(1.6rem, 3.5vw, 2.2rem); font-weight: 800; margin: 4rem 0 0.75rem;
    text-align: center; letter-spacing: -0.02em; color: var(--text);
}
.section-subtitle {
    text-align: center; color: var(--muted); font-size: 1.05rem; max-width: 640px; margin: 0 auto 2.5rem;
}

.tabs { display: flex; gap: 0.5rem; margin-bottom: 1.25rem; justify-content: center; flex-wrap: wrap; }
.tab-btn {
    background: #FFFFFF; color: var(--muted); border: 1px solid var(--border);
    padding: 0.5rem 1rem; border-radius: 6px; cursor: pointer; font-weight: 600;
    font-size: 0.88rem; font-family: var(--font-mono); transition: all 0.15s;
}
.tab-btn:hover { color: var(--text); border-color: var(--border-dark); }
.tab-btn.active {
    background: #1C1917; color: #FFFFFF; border-color: #1C1917;
}
.tab-panel { display: none; }
.tab-panel.active { display: block; }

/* Rosetta Mobile Toggle */
.rosetta-mobile-toggle {
    display: none; margin-bottom: 0.75rem; gap: 0.35rem; background: #E7E5E4;
    padding: 0.25rem; border-radius: 6px;
}
.rosetta-mobile-toggle button {
    flex: 1; padding: 0.45rem 0.5rem; border: none; background: transparent;
    font-family: var(--font-mono); font-size: 0.78rem; font-weight: 700;
    color: var(--muted); border-radius: 4px; cursor: pointer;
}
.rosetta-mobile-toggle button.active {
    background: #FFFFFF; color: var(--text);
}

/* Console preview */
.console-preview {
    background: #F5F5F4; border: 1px solid var(--border); border-radius: 6px;
    padding: 0.75rem 1rem; margin-top: 0.85rem; font-family: var(--font-mono);
    font-size: 0.82rem; color: #1C1917;
}
.console-header {
    font-size: 0.7rem; font-weight: 700; color: var(--muted); text-transform: uppercase;
    letter-spacing: 0.05em; margin-bottom: 0.3rem; display: flex; align-items: center; gap: 0.35rem;
}
.console-dot { width: 7px; height: 7px; border-radius: 50%; background: #059669; }

/* Robust Table System: Isolated Scrolling & Polished Mobile Experience */
.table-wrap {
    position: relative;
    width: 100%;
    max-width: 100%;
    min-width: 0;
    overflow-x: auto;
    -webkit-overflow-scrolling: touch;
    margin: 1.5rem 0 2.25rem;
    background: #FFFFFF;
    border: 1px solid var(--border);
    border-radius: 8px;
    box-shadow: 0 1px 3px rgba(0, 0, 0, 0.02);
}
.table-wrap table {
    width: 100%;
    min-width: 620px;
    border-collapse: collapse;
    background: transparent;
    border: none;
    margin: 0;
}
th, td {
    text-align: left; padding: 0.85rem 1rem; border-bottom: 1px solid var(--border);
    font-size: 0.9rem; vertical-align: top;
}
th {
    background: #F5F5F4; color: var(--text); font-weight: 750; font-size: 0.82rem;
    text-transform: uppercase; letter-spacing: 0.04em;
}
tr:last-child td { border-bottom: none; }
tr:hover td { background: #FAFAF9; }

/* Subtle mobile scroll hint banner */
.table-scroll-cue {
    display: none;
    font-size: 0.72rem;
    font-weight: 700;
    color: var(--muted);
    text-transform: uppercase;
    letter-spacing: 0.05em;
    padding: 0.4rem 0.85rem;
    background: #F5F5F4;
    border-bottom: 1px solid var(--border);
    text-align: right;
}

/* Step Grid */
.step-grid {
    display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 1.25rem; margin: 2rem 0;
}
.step-card {
    background: #FFFFFF; border: 1px solid var(--border); border-radius: 8px;
    padding: 1.5rem; position: relative;
}
.step-badge {
    display: inline-block; font-family: var(--font-mono); font-size: 0.72rem;
    font-weight: 800; text-transform: uppercase; letter-spacing: 0.05em;
    background: #1C1917; color: #FFFFFF; padding: 0.2rem 0.6rem; border-radius: 4px;
    margin-bottom: 0.75rem;
}
.step-card h4 { font-size: 1.15rem; margin-bottom: 0.5rem; color: var(--text); }
.step-card p { font-size: 0.9rem; color: var(--muted); line-height: 1.6; margin-bottom: 0.85rem; }

/* Roadmap */
.roadmap-grid {
    display: grid; grid-template-columns: repeat(auto-fit, minmax(240px, 1fr)); gap: 1rem; margin: 2rem 0;
}
.roadmap-card {
    background: #FFFFFF; border: 1px solid var(--border); border-radius: 8px;
    padding: 1.25rem; transition: border-color 0.15s ease; text-decoration: none; display: block;
}
.roadmap-card:hover {
    border-color: var(--border-dark); text-decoration: none;
}
.roadmap-num {
    font-family: var(--font-mono); font-size: 0.72rem; font-weight: 800;
    text-transform: uppercase; letter-spacing: 0.05em; color: var(--accent); margin-bottom: 0.35rem;
}
.roadmap-card h4 { font-size: 1.05rem; color: var(--text); margin-bottom: 0.35rem; font-weight: 700; }
.roadmap-card p { font-size: 0.85rem; color: var(--muted); line-height: 1.5; margin: 0; }

.footer {
    text-align: center; padding: 3rem 1rem 2.5rem; border-top: 1px solid var(--border);
    background: #FFFFFF; color: var(--muted); font-size: 0.88rem; margin-top: 4.5rem;
}
.footer-logo { max-width: 120px; margin-bottom: 1rem; }
.footer-links { display: flex; justify-content: center; gap: 1.5rem; margin: 1rem 0; flex-wrap: wrap; }
.footer-links a { color: var(--muted); font-weight: 600; font-size: 0.88rem; }
.footer-links a:hover { color: var(--text); }

/* Responsive adjustments for mobile & tablet */
@media (max-width: 840px) {
    .container { padding: 1.5rem 1rem 3.5rem; }
    .doc-layout { display: block; width: 100%; min-width: 0; }
    .sidebar {
        position: static; max-height: none; width: 100%;
        margin-bottom: 2rem; padding: 0.85rem 1rem;
    }
    .sidebar-summary {
        display: flex; align-items: center; justify-content: space-between;
        font-weight: 750; font-size: 0.9rem; cursor: pointer; color: var(--text);
        user-select: none;
    }
    .sidebar-summary::-webkit-details-marker { display: none; }
    .sidebar-chevron { font-size: 0.8rem; color: var(--muted); transition: transform 0.2s ease; }
    .sidebar-details:not([open]) .sidebar-content { display: none; }
    .sidebar-details[open] .sidebar-content {
        display: block; margin-top: 0.85rem; padding-top: 0.85rem;
        border-top: 1px solid var(--border); max-height: 50vh; overflow-y: auto;
    }
    .sidebar-details[open] .sidebar-chevron { transform: rotate(180deg); }
    .grid-2 { grid-template-columns: 1fr; }
    .rosetta-mobile-toggle { display: flex; }
    .rosetta-pane-cpp { display: none; }
    .rosetta-pane-cpp.is-active { display: block; }
    .rosetta-pane-cf.is-inactive { display: none; }
    .hero { padding: 2rem 0.25rem 2.5rem; }
}

@media (max-width: 680px) {
    .navbar-inner { flex-wrap: wrap; padding: 0.6rem 0.85rem; gap: 0.5rem; }
    .brand { font-size: 1.1rem; }
    .brand img.brand-icon { width: 26px; height: 26px; }
    .nav-links {
        display: flex; align-items: center; gap: 0.4rem;
        width: 100%; overflow-x: auto; -webkit-overflow-scrolling: touch;
        scrollbar-width: none; padding: 0.1rem 0;
    }
    .nav-links::-webkit-scrollbar { display: none; }
    .nav-links a {
        white-space: nowrap; padding: 0.35rem 0.65rem; border-radius: 6px;
        background: #F5F5F4; font-size: 0.82rem; color: var(--muted); flex-shrink: 0;
    }
    .nav-links a.active { background: #1C1917; color: #FFFFFF !important; }
    .btn-nav-primary {
        margin-left: auto; padding: 0.35rem 0.75rem !important;
        font-size: 0.8rem !important; flex-shrink: 0;
    }
    .table-scroll-cue { display: block; }

    /* API Reference Tables: Mobile Card Transformation */
    .api-table-wrap {
        border: none; background: transparent; overflow-x: visible;
        box-shadow: none; margin: 1rem 0 1.75rem;
    }
    .api-table-wrap .table-scroll-cue { display: none; }
    .api-table-wrap table,
    .api-table-wrap thead,
    .api-table-wrap tbody,
    .api-table-wrap th,
    .api-table-wrap td,
    .api-table-wrap tr {
        display: block; width: 100%; min-width: 0 !important; box-sizing: border-box;
    }
    .api-table-wrap thead { display: none; }
    .api-table-wrap tr {
        background: #FFFFFF; border: 1px solid var(--border); border-radius: 8px;
        margin-bottom: 0.85rem; padding: 0.95rem 1rem; box-shadow: 0 1px 2px rgba(0, 0, 0, 0.03);
    }
    .api-table-wrap td { padding: 0.25rem 0; border: none; font-size: 0.88rem; }
    .api-table-wrap td:first-child {
        padding-bottom: 0.45rem; border-bottom: 1px dashed var(--border); margin-bottom: 0.45rem;
    }
    .api-table-wrap td:first-child code {
        font-weight: 700; font-size: 0.88rem; color: var(--accent);
        background: #EFF6FF; border-color: #DBEAFE; word-break: break-all;
        display: inline-block; padding: 0.2rem 0.45rem; border-radius: 4px;
    }
    .api-table-wrap td:nth-child(2)::before {
        content: "C++23: "; font-weight: 700; font-size: 0.72rem; text-transform: uppercase;
        letter-spacing: 0.05em; color: var(--muted); display: inline-block; margin-right: 0.35rem;
    }
    .api-table-wrap td:nth-child(2) {
        font-size: 0.82rem; color: #57534E; padding-bottom: 0.35rem; word-break: break-all;
    }
    .api-table-wrap td:last-child { color: #1C1917; line-height: 1.55; }
    .api-table-wrap td:last-child code {
        display: block; margin-top: 0.35rem; word-break: break-all; background: #F5F5F4;
    }
}

@media (min-width: 841px) {
    .rosetta-mobile-toggle { display: none; }
    .rosetta-pane-cf, .rosetta-pane-cpp { display: block !important; }
}
)CSS";
}

[[nodiscard]] inline auto navbar_html(std::string_view active_page) -> std::string {
    return std::format(R"HTML(
<nav class="navbar">
  <div class="navbar-inner">
    <a class="brand" href="index.html">
      <img src="assets/cfire_logo_emblem.png" alt="cfire logo" class="brand-icon" />
      <span class="brand-text">c<span class="fire">fire</span></span>
      <span class="brand-badge">ISO C++23</span>
    </a>
    <div class="nav-links">
      <a href="index.html" class="{0}">Overview</a>
      <a href="guide.html" class="{1}">Guide</a>
      <a href="reference.html" class="{2}">Stdlib</a>
      <a href="https://github.com/gsandoval/cfire" target="_blank" rel="noopener">GitHub &rarr;</a>
      <a href="index.html#install" class="btn-nav-primary">Install</a>
    </div>
  </div>
</nav>
)HTML",
        active_page == "index" ? "active" : "",
        active_page == "guide" ? "active" : "",
        active_page == "reference" ? "active" : "");
}

} // namespace detail

[[nodiscard]] inline auto html_escape(std::string_view s) -> std::string {
    return detail::html_escape(s);
}

} // namespace cfire::doc
