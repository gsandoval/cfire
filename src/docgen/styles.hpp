#pragma once
// src/docgen/styles.hpp — Shared HTML/CSS Theme & Navigation Layout for `cfire doc`

#include "../version.hpp"
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

[[nodiscard]] inline auto icon_font_head_links() -> std::string {
    return "  <script>(function(){var t=localStorage.getItem('cfire-theme')||'light';document.documentElement.setAttribute('data-theme',t);})();</script>\n"
           "  <link rel=\"preconnect\" href=\"https://fonts.googleapis.com\" />\n"
           "  <link rel=\"preconnect\" href=\"https://fonts.gstatic.com\" crossorigin />\n"
           "  <link rel=\"stylesheet\" href=\"https://fonts.googleapis.com/css2?family=Material+Symbols+Outlined:opsz,wght,FILL,GRAD@20..24,400,0,0&display=swap\" />\n";
}

[[nodiscard]] inline auto shared_css() -> std::string {
    return R"CSS(
:root, [data-theme="light"] {
    --bg: #F8FAFC;
    --bg-nav: rgba(255, 255, 255, 0.96);
    --bg-card: #FFFFFF;
    --bg-card-hover: #F1F5F9;
    --bg-code: #F1F5F9;
    --bg-code-inline: #F1F5F9;
    --bg-table-head: #F1F5F9;
    --bg-table-hover: #F8FAFC;
    --bg-subtle: #E2E8F0;
    --bg-accent-tint: #FFF7ED;
    --border-accent-tint: #FDBA74;
    --bg-sapphire-tint: #F0F9FF;
    --border-sapphire-tint: #BAE6FD;
    --bg-tip-tint: #F0FDF4;
    --bg-imp-tint: #F5F3FF;
    --bg-warn-tint: #FFFBEB;
    --bg-err-tint: #FEF2F2;
    --bg-terminal: #F0FDF4;
    --text-terminal: #14532D;
    --bg-backdrop: rgba(15, 23, 42, 0.55);
    --border: #E2E8F0;
    --border-dark: #CBD5E1;
    --text: #0F172A;
    --text-bright: #020617;
    --muted: #475569;
    --accent: #EA580C;
    --accent-hover: #C2410C;
    --accent-contrast: #FFFFFF;
    --accent-sapphire: #0284C7;
    --accent-sapphire-hover: #0369A1;
    --ok: #16A34A;
    --warn: #D97706;
    --err: #DC2626;
    --purple: #7C3AED;
    --font-sans: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", sans-serif;
    --font-mono: ui-monospace, "JetBrains Mono", SFMono-Regular, Menlo, Monaco, Consolas, monospace;
}
[data-theme="dark"] {
    --bg: #0D1117;
    --bg-nav: rgba(13, 17, 23, 0.96);
    --bg-card: #161B22;
    --bg-card-hover: #21262D;
    --bg-code: #090D12;
    --bg-code-inline: #1C2128;
    --bg-table-head: #1C2128;
    --bg-table-hover: #1F242C;
    --bg-subtle: #21262D;
    --bg-accent-tint: #27140B;
    --border-accent-tint: #7C2D12;
    --bg-sapphire-tint: #0B1622;
    --border-sapphire-tint: #1E3A5F;
    --bg-tip-tint: #0A1C14;
    --bg-imp-tint: #171124;
    --bg-warn-tint: #1E1606;
    --bg-err-tint: #220B0E;
    --bg-terminal: #070A0E;
    --text-terminal: #D2F4DC;
    --bg-backdrop: rgba(9, 13, 18, 0.82);
    --border: #30363D;
    --border-dark: #484F58;
    --text: #F0F6FC;
    --text-bright: #FFFFFF;
    --muted: #8B949E;
    --accent: #F97316;
    --accent-hover: #FB923C;
    --accent-contrast: #0D1117;
    --accent-sapphire: #38BDF8;
    --accent-sapphire-hover: #7DD3FC;
    --ok: #3FB950;
    --warn: #D29922;
    --err: #F85149;
    --purple: #A78BFA;
}
*, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }
.material-symbols-outlined {
    font-family: 'Material Symbols Outlined';
    font-weight: normal;
    font-style: normal;
    font-size: 1.25rem;
    line-height: 1;
    letter-spacing: normal;
    text-transform: none;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    white-space: nowrap;
    word-wrap: normal;
    direction: ltr;
    -webkit-font-smoothing: antialiased;
    font-variation-settings: 'FILL' 0, 'wght' 400, 'GRAD' 0, 'opsz' 24;
    vertical-align: middle;
    user-select: none;
}
.bento-icon {
    font-size: 1.4rem;
    color: var(--accent);
}
html, body {
    width: 100%;
    max-width: 100%;
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
::selection { background: var(--accent); color: var(--accent-contrast); }
a {
    color: var(--accent-sapphire);
    text-decoration: none;
    transition: color 0.15s ease, border-color 0.15s ease;
}
a:hover {
    color: var(--accent-sapphire-hover);
}

/* Crisp Technical Prose Links */
main a:not(.pager-card):not(.btn):not(.code-copy-btn):not(.sidebar-ch-link):not(.brand),
.chapter-pane a:not(.pager-card):not(.btn):not(.code-copy-btn),
.callout a {
    color: var(--accent-sapphire);
    text-decoration: none;
    border-bottom: 1px solid var(--border-sapphire-tint);
    font-weight: 500;
    transition: color 0.15s ease, border-bottom-color 0.15s ease;
}
main a:not(.pager-card):not(.btn):not(.code-copy-btn):not(.sidebar-ch-link):not(.brand):hover,
.chapter-pane a:not(.pager-card):not(.btn):not(.code-copy-btn):hover,
.callout a:hover {
    color: var(--accent-sapphire-hover);
    border-bottom-color: var(--accent-sapphire);
    text-decoration: none;
}
main a code, .chapter-pane a code {
    color: inherit;
    border-color: var(--border-sapphire-tint);
    background: var(--bg-code-inline);
}

/* Sticky Edge-to-Edge Navigation Bar */
.navbar {
    position: sticky; top: 0; z-index: 100;
    width: 100%;
    background: var(--bg-nav);
    backdrop-filter: blur(8px);
    border-bottom: 1px solid var(--border);
}
.navbar-inner {
    max-width: 1120px;
    margin: 0 auto;
    padding: 0.7rem 1.25rem;
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 1rem;
    width: 100%;
    box-sizing: border-box;
}
.brand {
    display: flex; align-items: center; gap: 0.65rem;
    font-weight: 800; font-size: 1.25rem; color: var(--text-bright);
    text-decoration: none; flex-shrink: 0;
}
.brand:hover { text-decoration: none; }
.brand img.brand-icon {
    width: 30px; height: 30px; object-fit: contain;
}
.brand span.brand-text {
    font-size: 1.2rem; font-weight: 850; letter-spacing: -0.02em; color: var(--accent-sapphire);
}
.brand span.fire { color: var(--accent); font-weight: 850; }
.brand-badge {
    font-family: var(--font-mono); font-size: 0.65rem; font-weight: 700;
    background: var(--bg-subtle); border: 1px solid var(--border); color: var(--muted);
    padding: 0.12rem 0.45rem; border-radius: 4px; margin-left: 0.25rem;
}
.nav-links { display: flex; align-items: center; gap: 1rem; font-size: 0.9rem; font-weight: 600; }
.nav-links a {
    color: var(--muted); transition: all 0.15s ease;
    padding: 0.25rem 0.5rem; border-radius: 5px;
}
.nav-links a:hover { color: var(--text-bright); text-decoration: none; background: var(--bg-card-hover); }
.nav-links a.active { color: var(--text-bright); text-decoration: none; font-weight: 750; background: var(--bg-subtle); }
.btn-theme-toggle {
    display: inline-flex;
    align-items: center;
    gap: 0.3rem;
    color: var(--text) !important;
    font-family: var(--font-mono);
    font-size: 0.76rem !important;
    font-weight: 700 !important;
    padding: 0.32rem 0.6rem !important;
    border-radius: 6px !important;
    border: 1px solid var(--border) !important;
    background: var(--bg-card) !important;
    cursor: pointer;
    transition: all 0.15s ease !important;
}
.btn-theme-toggle:hover {
    background: var(--bg-card-hover) !important;
    border-color: var(--border-dark) !important;
}
.btn-nav-github {
    display: inline-flex;
    align-items: center;
    color: var(--muted) !important;
    font-size: 0.82rem !important;
    font-weight: 600 !important;
    padding: 0.35rem 0.65rem !important;
    border-radius: 6px !important;
    border: 1px solid var(--border) !important;
    background: var(--bg-card) !important;
    transition: all 0.15s ease !important;
    text-decoration: none !important;
    border-bottom: none !important;
}
.btn-nav-github:hover {
    color: var(--text-bright) !important;
    background: var(--bg-card-hover) !important;
    border-color: var(--border-dark) !important;
    text-decoration: none !important;
}
.btn-nav-primary {
    background: var(--accent) !important; color: var(--accent-contrast) !important;
    padding: 0.4rem 0.9rem !important; border-radius: 6px !important; font-size: 0.85rem !important; font-weight: 700 !important;
    transition: background 0.15s !important; border: none;
    text-decoration: none !important;
    border-bottom: none !important;
}
.btn-nav-primary:hover { background: var(--accent-hover) !important; text-decoration: none !important; }

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
    background: var(--bg-card);
    border: 1px solid var(--border);
    border-radius: 8px;
    padding: 1.25rem;
}

/* 3-Pane Persistent Reader Shell (Guide & Reference) */
.guide-shell {
    display: flex;
    height: calc(100vh - 3.5rem);
    overflow: hidden;
    width: 100%;
}
.guide-sidebar {
    width: 280px;
    flex-shrink: 0;
    height: 100%;
    overflow-y: auto;
    background: var(--bg-card);
    border-right: 1px solid var(--border);
    padding: 1.25rem 1rem;
}
.guide-sidebar::-webkit-scrollbar,
.guide-viewport::-webkit-scrollbar,
.guide-toc-rail::-webkit-scrollbar,
pre::-webkit-scrollbar {
    width: 6px;
    height: 6px;
}
.guide-sidebar::-webkit-scrollbar-track,
.guide-viewport::-webkit-scrollbar-track,
.guide-toc-rail::-webkit-scrollbar-track,
pre::-webkit-scrollbar-track {
    background: var(--bg-code);
}
.guide-sidebar::-webkit-scrollbar-thumb,
.guide-viewport::-webkit-scrollbar-thumb,
.guide-toc-rail::-webkit-scrollbar-thumb,
pre::-webkit-scrollbar-thumb {
    background: var(--border);
    border-radius: 3px;
}
.guide-sidebar::-webkit-scrollbar-thumb:hover,
.guide-viewport::-webkit-scrollbar-thumb:hover,
.guide-toc-rail::-webkit-scrollbar-thumb:hover,
pre::-webkit-scrollbar-thumb:hover {
    background: var(--border-dark);
}
.guide-viewport {
    flex: 1;
    min-width: 0;
    height: 100%;
    overflow-y: auto;
    scroll-behavior: smooth;
    display: flex;
}
.guide-content-inner {
    flex: 1;
    min-width: 0;
    max-width: 860px;
    margin: 0 auto;
    padding: 2rem 2.5rem 6rem;
}
.guide-toc-rail {
    width: 240px;
    flex-shrink: 0;
    height: 100%;
    overflow-y: auto;
    padding: 2rem 1.25rem;
    border-left: 1px solid var(--border);
    background: var(--bg);
}

/* Reader Toolbar & Breadcrumbs */
.reader-toolbar {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 1rem;
    padding-bottom: 1.25rem;
    margin-bottom: 2rem;
    border-bottom: 1px solid var(--border);
    flex-wrap: nowrap;
}
.reader-breadcrumbs {
    font-family: var(--font-mono);
    font-size: 0.78rem;
    color: var(--muted);
    flex: 1;
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
}
.reader-breadcrumbs a {
    color: var(--muted);
    text-decoration: none;
}
.reader-breadcrumbs a:hover {
    color: var(--text-bright);
}
.reader-breadcrumbs .sep {
    margin: 0 0.4rem;
    color: var(--border-dark);
}
.reader-breadcrumbs .current {
    color: var(--accent);
    font-weight: 700;
}
.reader-mode-toggle {
    display: flex;
    flex-shrink: 0;
    border: 1px solid var(--border);
    border-radius: 6px;
    overflow: hidden;
    background: var(--bg-card);
}
.mode-btn {
    background: transparent;
    border: none;
    padding: 0.3rem 0.75rem;
    font-family: var(--font-mono);
    font-size: 0.75rem;
    font-weight: 600;
    color: var(--muted);
    cursor: pointer;
    transition: all 0.15s ease;
}
.mode-btn:hover {
    color: var(--text-bright);
}
.mode-btn.active {
    background: var(--bg-subtle);
    color: var(--text-bright);
    font-weight: 700;
}

/* Chapter Panes & Bite-Sized Topic Units */
.chapter-pane {
    display: none;
}
.chapter-pane.active {
    display: block;
    animation: fadeIn 0.15s ease-in;
}
@keyframes fadeIn {
    from { opacity: 0; transform: translateY(4px); }
    to { opacity: 1; transform: translateY(0); }
}
.view-all-mode .chapter-pane {
    display: block !important;
    margin-bottom: 4rem;
    padding-bottom: 3.5rem;
    border-bottom: 1px solid var(--border);
}
.view-all-mode .chapter-pager,
.view-all-mode .topic-pager,
.view-all-mode .topic-pill-bar {
    display: none !important;
}

/* Topic View Mode (Bite-Sized Default) vs Chapter View Mode */
.topic-pill-bar {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: 0.4rem;
    margin: 0.85rem 0 1.4rem;
    padding: 0.55rem 0.75rem;
    background: var(--bg-card);
    border: 1px solid var(--border);
    border-radius: 8px;
}
.topic-pill-label {
    font-family: var(--font-mono);
    font-size: 0.68rem;
    font-weight: 800;
    text-transform: uppercase;
    letter-spacing: 0.06em;
    color: var(--muted);
    margin-right: 0.35rem;
}
.topic-pill {
    background: var(--bg-code);
    border: 1px solid var(--border);
    border-radius: 5px;
    padding: 0.25rem 0.6rem;
    font-family: var(--font-mono);
    font-size: 0.73rem;
    font-weight: 600;
    color: var(--muted);
    cursor: pointer;
    transition: all 0.15s ease;
    text-decoration: none !important;
    border-bottom: 1px solid var(--border) !important;
}
.topic-pill:hover {
    color: var(--text-bright);
    border-color: var(--border-dark) !important;
    background: var(--bg-card-hover);
}
.topic-pill.active {
    background: var(--bg-accent-tint);
    color: var(--accent);
    border-color: var(--accent) !important;
    font-weight: 700;
}
.topic-View-mode .guide-topic {
    display: none;
}
.topic-View-mode .guide-topic.active-topic {
    display: block;
    animation: fadeIn 0.15s ease-in;
}
.topic-View-mode .chapter-pager {
    display: none;
}
.chapter-View-mode .guide-topic {
    display: block !important;
    padding-bottom: 1.5rem;
    margin-bottom: 1.75rem;
    border-bottom: 1px dashed var(--border);
}
.chapter-View-mode .guide-topic:last-of-type {
    border-bottom: none;
}
.chapter-View-mode .topic-pager {
    display: none !important;
}

/* Collapsible C++23 Lowering Drawer & Terminal Output Tray */
details.lowering-drawer {
    margin: 1.15rem 0 1.5rem;
    background: var(--bg-card);
    border: 1px solid var(--border);
    border-left: 3px solid var(--warn);
    border-radius: 7px;
    overflow: hidden;
}
details.lowering-drawer > summary {
    padding: 0.55rem 0.95rem;
    font-family: var(--font-mono);
    font-size: 0.76rem;
    font-weight: 750;
    color: var(--warn);
    cursor: pointer;
    user-select: none;
    display: flex;
    align-items: center;
    justify-content: space-between;
    background: var(--bg-warn-tint);
}
details.lowering-drawer > summary:hover {
    background: var(--bg-card-hover);
}
details.lowering-drawer > .lowering-body {
    padding: 0.75rem 0.95rem 0.95rem;
    border-top: 1px solid var(--border);
}
.terminal-tray {
    margin: -0.5rem 0 1.5rem;
    background: var(--bg-terminal);
    border: 1px solid var(--border);
    border-left: 3px solid var(--ok);
    border-radius: 0 0 7px 7px;
    padding: 0.6rem 0.95rem;
    font-family: var(--font-mono);
    font-size: 0.8rem;
}
.terminal-tray-header {
    font-size: 0.66rem;
    font-weight: 800;
    text-transform: uppercase;
    letter-spacing: 0.06em;
    color: var(--ok);
    margin-bottom: 0.25rem;
}
.terminal-tray pre {
    margin: 0;
    padding: 0.25rem 0 0;
    border: none;
    background: transparent;
    font-size: 0.8rem;
    color: var(--text-terminal);
}

/* Search Results Instant Dropdown */
.search-results-dropdown {
    display: none;
    margin-top: 0.35rem;
    margin-bottom: 0.85rem;
    background: var(--bg-code);
    border: 1px solid var(--accent);
    border-radius: 6px;
    max-height: 280px;
    overflow-y: auto;
    padding: 0.35rem;
}
.search-results-dropdown.active {
    display: block;
}
.search-hit-item {
    display: block;
    padding: 0.4rem 0.55rem;
    border-radius: 4px;
    text-decoration: none !important;
    border-bottom: none !important;
    margin-bottom: 0.15rem;
}
.search-hit-item:hover {
    background: var(--bg-card-hover);
}
.search-hit-title {
    font-size: 0.78rem;
    font-weight: 700;
    color: var(--text-bright);
    display: block;
}
.search-hit-meta {
    font-family: var(--font-mono);
    font-size: 0.68rem;
    color: var(--accent-sapphire);
    display: block;
}

/* Paginated Reference Panes (reference.html) */
.ref-pane {
    display: none;
}
.ref-pane.active {
    display: block;
    animation: fadeIn 0.15s ease-in;
}
.view-all-mode .ref-pane {
    display: block !important;
    margin-bottom: 3.5rem;
    padding-bottom: 2.5rem;
    border-bottom: 1px solid var(--border);
}
.view-all-mode #ref-pane-overview {
    display: none !important;
}
.view-all-mode .ref-pane .chapter-pager {
    display: none !important;
}
.ref-category-grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(260px, 1fr));
    gap: 0.85rem;
    margin: 0.75rem 0 2rem;
}
.ref-mod-card {
    background: var(--bg-card);
    border: 1px solid var(--border);
    border-radius: 8px;
    padding: 1rem 1.15rem;
    cursor: pointer;
    transition: all 0.15s ease;
    display: flex;
    flex-direction: column;
    justify-content: space-between;
    text-decoration: none !important;
}
.ref-mod-card:hover {
    background: var(--bg-card-hover);
    border-color: var(--accent) !important;
}
.ref-mod-card-top {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 0.5rem;
    margin-bottom: 0.45rem;
}
.ref-mod-card-title {
    font-size: 0.9rem;
    font-weight: 700;
    color: var(--text-bright);
    margin-bottom: 0.35rem;
}
.ref-mod-card-desc {
    font-size: 0.8rem;
    color: var(--muted);
    line-height: 1.45;
}

/* Chapter Pager Buttons */
.chapter-pager {
    display: flex;
    justify-content: space-between;
    gap: 1rem;
    margin-top: 3.5rem;
    padding-top: 1.5rem;
    border-top: 1px solid var(--border);
    flex-wrap: wrap;
}
.pager-card {
    background: var(--bg-card);
    border: 1px solid var(--border) !important;
    border-radius: 8px;
    padding: 1.15rem 1.4rem;
    text-decoration: none !important;
    transition: all 0.15s ease;
    display: flex;
    flex-direction: column;
    gap: 0.35rem;
    flex: 1;
    min-width: 200px;
}
.pager-card:hover {
    background: var(--bg-card-hover);
    border-color: var(--accent) !important;
    text-decoration: none !important;
}
.pager-card:hover .pager-title {
    color: var(--accent);
}
.pager-label {
    font-family: var(--font-mono);
    font-size: 0.68rem;
    font-weight: 700;
    text-transform: uppercase;
    letter-spacing: 0.08em;
    color: var(--muted);
}
.pager-card.pager-next {
    text-align: right;
    margin-left: auto;
}
.pager-title {
    font-size: 1rem;
    font-weight: 750;
    color: var(--text-bright);
    transition: color 0.15s ease;
}

/* Right-Rail TOC */
.toc-header {
    font-family: var(--font-mono);
    font-size: 0.72rem;
    font-weight: 800;
    text-transform: uppercase;
    letter-spacing: 0.08em;
    color: var(--muted);
    margin-bottom: 0.85rem;
}
.toc-list {
    list-style: none;
    padding: 0;
    margin: 0 0 1rem 0;
}
.toc-list li {
    margin-bottom: 0.35rem;
}
.toc-list a {
    display: block;
    font-size: 0.82rem;
    color: var(--muted);
    padding: 0.2rem 0;
    text-decoration: none;
    border-bottom: none !important;
    line-height: 1.4;
    transition: color 0.15s ease;
}
.toc-list a:hover {
    color: var(--text-bright);
}
.toc-list a.active-toc-item {
    color: var(--accent) !important;
    font-weight: 600;
}
.rail-action-list {
    list-style: none;
    padding: 0;
    margin: 1.25rem 0;
    border-top: 1px solid var(--border);
    padding-top: 1rem;
}
.rail-action-btn {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    font-size: 0.8rem;
    color: var(--muted);
    padding: 0.35rem 0;
    text-decoration: none;
    border-bottom: none !important;
    transition: color 0.15s ease;
    cursor: pointer;
    background: transparent;
    border: none;
    width: 100%;
    text-align: left;
}
.rail-action-btn:hover {
    color: var(--text-bright);
    text-decoration: none;
}
.rail-telemetry-box {
    margin-top: 1.25rem;
    background: var(--bg-card);
    border: 1px solid var(--border);
    border-radius: 6px;
    padding: 0.75rem 0.85rem;
    font-family: var(--font-mono);
    font-size: 0.72rem;
}
.rail-telemetry-row {
    display: flex;
    justify-content: space-between;
    padding: 0.25rem 0;
    border-bottom: 1px solid var(--border);
}
.rail-telemetry-row:last-child {
    border-bottom: none;
}
.rail-telemetry-label {
    color: var(--muted);
    text-transform: uppercase;
    letter-spacing: 0.05em;
}
.rail-telemetry-val {
    color: var(--text-bright);
    font-weight: 700;
}
.rail-telemetry-val.val-amber {
    color: var(--accent);
}
.toc-quick-links {
    margin-top: 1.5rem;
    padding-top: 1.25rem;
    border-top: 1px solid var(--border);
}

/* Telemetry Console Bar */
.telemetry-bar {
    background: var(--bg-code);
    border: 1px solid var(--border);
    border-radius: 6px;
    margin-top: 0.85rem;
    font-family: var(--font-mono);
    font-size: 0.8rem;
}
.telemetry-header {
    background: var(--bg-card);
    border-bottom: 1px solid var(--border);
    padding: 0.4rem 0.85rem;
    display: flex;
    justify-content: space-between;
    align-items: center;
    font-weight: 700;
    font-size: 0.72rem;
    text-transform: uppercase;
    letter-spacing: 0.05em;
    color: var(--muted);
}
.telemetry-body {
    padding: 0.75rem 0.85rem;
    display: flex;
    flex-wrap: wrap;
    justify-content: space-between;
    gap: 0.75rem;
    color: var(--muted);
}
.telemetry-stat {
    display: inline-flex;
    align-items: center;
    gap: 0.35rem;
}
.telemetry-val {
    color: var(--accent-sapphire);
    font-weight: 700;
}
.telemetry-pass {
    color: var(--ok);
    font-weight: 700;
}

/* Bento Architectural Cards */
.bento-grid {
    display: grid;
    grid-template-columns: repeat(3, 1fr);
    gap: 1.25rem;
    margin: 2rem 0;
}
.bento-card {
    background: var(--bg-card);
    border: 1px solid var(--border);
    border-radius: 8px;
    padding: 1.5rem;
    display: flex;
    flex-direction: column;
    justify-content: space-between;
    transition: border-color 0.15s ease;
}
.bento-card:hover {
    border-color: var(--border-dark);
}
.bento-card.span-2 {
    grid-column: span 2;
}
.bento-card-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 0.75rem;
}
.bento-tag {
    font-family: var(--font-mono);
    font-size: 0.68rem;
    font-weight: 800;
    color: var(--accent);
    background: var(--bg-accent-tint);
    border: 1px solid var(--border-accent-tint);
    padding: 0.15rem 0.5rem;
    border-radius: 4px;
    letter-spacing: 0.05em;
}
.bento-card h3 {
    font-size: 1.15rem;
    font-weight: 750;
    color: var(--text-bright);
    margin-bottom: 0.5rem;
}
.bento-card p {
    color: var(--muted);
    font-size: 0.9rem;
    line-height: 1.6;
    margin-bottom: 1rem;
}
.bento-footer {
    padding-top: 0.75rem;
    border-top: 1px solid var(--border);
    font-family: var(--font-mono);
    font-size: 0.75rem;
    color: var(--accent-sapphire);
}
.mobile-unified-bar { display: none; }
.sidebar-summary { display: none; }
.sidebar h4 {
    font-size: 0.75rem; font-weight: 800; text-transform: uppercase;
    letter-spacing: 0.08em; color: var(--muted); margin-bottom: 0.75rem;
}

.nav-search-wrapper {
    position: relative;
    margin-bottom: 0.85rem;
}
.nav-search-wrapper .search-input {
    width: 100%;
    padding: 0.45rem 2.6rem 0.45rem 0.65rem;
    font-size: 0.82rem;
    font-family: var(--font-sans);
    border: 1px solid var(--border);
    border-radius: 6px;
    background: var(--bg-code);
    color: var(--text);
    outline: none;
    transition: border-color 0.15s;
    box-sizing: border-box;
}
.nav-search-wrapper .search-input:focus {
    border-color: var(--accent);
}
.nav-search-kbd {
    position: absolute;
    right: 0.55rem;
    top: 50%;
    transform: translateY(-50%);
    font-family: var(--font-mono);
    font-size: 0.65rem;
    font-weight: 700;
    color: var(--muted);
    background: var(--bg-card);
    border: 1px solid var(--border);
    padding: 0.08rem 0.35rem;
    border-radius: 3px;
    pointer-events: none;
    line-height: 1.2;
}

.sidebar-part-title {
    font-size: 0.72rem; font-weight: 850; text-transform: uppercase;
    letter-spacing: 0.08em; color: var(--accent); margin: 1.15rem 0 0.35rem;
    padding-left: 0.4rem;
}
.sidebar-chapter-list { list-style: none; margin-bottom: 0.65rem; }
.sidebar-chapter-list .nav-item { margin-bottom: 0.15rem; }

.sidebar-ch-link {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 0.5rem;
    padding: 0.38rem 0.6rem;
    border-radius: 5px;
    color: var(--muted);
    font-size: 0.84rem;
    font-weight: 500;
    text-decoration: none !important;
    border-bottom: none !important;
    border-left: 2px solid transparent;
    transition: all 0.15s ease;
}
.sidebar-ch-link:hover {
    color: var(--text-bright);
    background: var(--bg-card-hover);
    text-decoration: none !important;
}
.sidebar-ch-link.active {
    color: var(--text-bright);
    background: var(--bg-card-hover);
    font-weight: 650;
    border-left-color: var(--accent);
}
.sidebar-ch-link .ch-text {
    flex: 1;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
}
.active-badge {
    font-family: var(--font-mono);
    font-size: 0.58rem;
    font-weight: 800;
    color: var(--accent);
    background: var(--bg-accent-tint);
    border: 1px solid var(--border-accent-tint);
    padding: 0.06rem 0.32rem;
    border-radius: 3px;
    letter-spacing: 0.05em;
    text-transform: uppercase;
    flex-shrink: 0;
}

.sidebar-footer-card {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-top: 1.5rem;
    padding: 0.6rem 0.75rem;
    background: var(--bg-card);
    border: 1px solid var(--border);
    border-radius: 6px;
    text-decoration: none !important;
    border-bottom: none !important;
    transition: all 0.15s ease;
}
.sidebar-footer-card:hover {
    background: var(--bg-card-hover);
    border-color: var(--border-dark);
    text-decoration: none !important;
}
.sfc-title {
    font-size: 0.8rem;
    font-weight: 650;
    color: var(--text-bright);
}
.sfc-badge {
    font-family: var(--font-mono);
    font-size: 0.65rem;
    font-weight: 750;
    color: var(--accent-sapphire);
    background: var(--bg-sapphire-tint);
    border: 1px solid var(--border-sapphire-tint);
    padding: 0.12rem 0.4rem;
    border-radius: 4px;
}

.guide-heading {
    display: flex;
    align-items: center;
    justify-content: space-between;
    position: relative;
    margin: 2.25rem 0 0.85rem;
    padding-bottom: 0.4rem;
    border-bottom: 1px solid var(--border);
}
.guide-heading .heading-hash {
    color: var(--accent);
    font-family: var(--font-mono);
    font-weight: 700;
    margin-right: 0.45rem;
    opacity: 0.9;
    user-select: none;
}
.guide-heading .heading-text {
    flex: 1;
    color: var(--text-bright);
    font-size: 1.25rem;
    font-weight: 750;
}
.guide-heading .heading-badge {
    font-family: var(--font-mono);
    font-size: 0.68rem;
    font-weight: 700;
    color: var(--muted);
    background: var(--bg-card);
    border: 1px solid var(--border);
    padding: 0.15rem 0.5rem;
    border-radius: 4px;
    letter-spacing: 0.04em;
    flex-shrink: 0;
}

.search-input {
    width: 100%;
    padding: 0.45rem 0.65rem;
    font-size: 0.82rem;
    font-family: var(--font-sans);
    border: 1px solid var(--border);
    border-radius: 6px;
    background: var(--bg-code);
    color: var(--text);
    outline: none;
    transition: border-color 0.15s;
    box-sizing: border-box;
}
.search-input:focus {
    border-color: var(--accent);
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
    background: var(--bg-card-hover); color: var(--text-bright); text-decoration: none;
}
.sidebar a.active {
    background: var(--bg-card-hover); color: var(--accent); font-weight: 700;
}
.sidebar a code { background: none; border: none; padding: 0; color: inherit; font-size: inherit; }

main, .guide-content-inner {
    min-width: 0;
    max-width: 100%;
    width: 100%;
    overflow-wrap: break-word;
    word-break: break-word;
}
main h1, .guide-content-inner h1 {
    font-size: clamp(1.75rem, 3.5vw, 2.25rem); font-weight: 800;
    color: var(--text-bright); letter-spacing: -0.03em; margin-bottom: 0.75rem;
}
main h2, .chapter-intro > h2, .ref-pane > h2 {
    font-size: 1.45rem; font-weight: 800; color: var(--text-bright);
    letter-spacing: -0.02em; margin: 1.5rem 0 0.85rem; padding-bottom: 0.45rem;
    border-bottom: 1px solid var(--border);
}
main h3, .guide-content-inner h3:not(.guide-heading) {
    font-size: 1.2rem; font-weight: 700; color: var(--text-bright);
    margin: 1.75rem 0 0.5rem;
}
main h4, .guide-content-inner h4 {
    font-size: 1.02rem; font-weight: 700; color: var(--text-bright);
    margin: 1.35rem 0 0.55rem; line-height: 1.45;
}
.guide-subhead5 {
    font-size: 0.98rem; font-weight: 700; color: var(--accent-sapphire);
    margin: 1.2rem 0 0.5rem; line-height: 1.45;
}
main p, .guide-content-inner p:not(.subtitle):not(.ref-mod-card-desc) {
    color: var(--text); font-size: 0.96rem; line-height: 1.7; margin-bottom: 1rem;
}
main ul, main ol,
.guide-content-inner ul:not(.toc-list):not(.rail-action-list):not(.sidebar-chapter-list):not(.subhead-list):not(.sidebar-module-list):not(.sidebar-diag-list),
.guide-content-inner ol {
    margin: 0.25rem 0 1.25rem 0;
    padding-left: 1.65rem;
    color: var(--text);
    font-size: 0.96rem;
    line-height: 1.7;
    list-style-position: outside;
}
main li,
.guide-content-inner ul:not(.toc-list):not(.rail-action-list):not(.sidebar-chapter-list):not(.subhead-list):not(.sidebar-module-list):not(.sidebar-diag-list) > li,
.guide-content-inner ol > li {
    margin-bottom: 0.45rem;
    padding-left: 0.2rem;
    font-size: 0.96rem;
    line-height: 1.7;
}
main li::marker,
.guide-content-inner ul:not(.toc-list):not(.rail-action-list) > li::marker,
.guide-content-inner ol > li::marker {
    color: var(--muted);
    font-size: 0.95em;
    font-weight: 600;
}
main li > ul, main li > ol,
.guide-content-inner li > ul, .guide-content-inner li > ol {
    margin: 0.4rem 0 0.5rem 0 !important;
    padding-left: 1.5rem !important;
    font-size: 0.95rem;
}
main li > .code-header,
.guide-content-inner li > .code-header {
    margin-top: 0.55rem;
}
main li > pre,
.guide-content-inner li > pre {
    margin-bottom: 0.65rem;
}

/* Part banner & Callouts */
.part-banner {
    margin: 3.5rem 0 1.5rem;
    padding-bottom: 0.75rem;
    border-bottom: 2px solid var(--border);
}
.part-banner:first-of-type { margin-top: 1rem; }
.part-pill {
    display: inline-block;
    font-family: var(--font-mono);
    font-size: 0.7rem;
    font-weight: 850;
    text-transform: uppercase;
    letter-spacing: 0.08em;
    color: var(--accent);
    background: var(--bg-accent-tint);
    border: 1px solid var(--border-accent-tint);
    padding: 0.15rem 0.55rem;
    border-radius: 4px;
    margin-bottom: 0.5rem;
}
.part-banner h2 {
    font-size: 1.85rem;
    font-weight: 850;
    color: var(--text-bright);
    border: none;
    padding: 0;
    margin: 0;
}
.callout {
    border-radius: 6px;
    padding: 1rem 1.25rem;
    margin: 1.5rem 0;
    border: 1px solid var(--border);
    border-left-width: 4px;
    background: var(--bg-card);
}
.callout-title {
    font-size: 0.8rem;
    font-weight: 800;
    text-transform: uppercase;
    letter-spacing: 0.06em;
    margin-bottom: 0.4rem;
}
.callout p {
    margin: 0;
    font-size: 0.92rem;
    color: var(--text);
    line-height: 1.6;
}
.callout-note {
    border-left-color: var(--accent-sapphire);
    background: var(--bg-sapphire-tint);
}
.callout-note .callout-title { color: var(--accent-sapphire); }
.callout-tip {
    border-left-color: var(--ok);
    background: var(--bg-tip-tint);
}
.callout-tip .callout-title { color: var(--ok); }
.callout-important {
    border-left-color: var(--purple);
    background: var(--bg-imp-tint);
}
.callout-important .callout-title { color: var(--purple); }
.callout-warning {
    border-left-color: var(--warn);
    background: var(--bg-warn-tint);
}
.callout-warning .callout-title { color: var(--warn); }
.callout-caution {
    border-left-color: var(--err);
    background: var(--bg-err-tint);
}
.callout-caution .callout-title { color: var(--err); }

/* Hero section */
.hero { text-align: center; padding: 3rem 0.5rem 3.5rem; }
.badge {
    display: inline-block; padding: 0.35rem 0.85rem; border-radius: 999px;
    font-family: var(--font-mono); font-size: 0.75rem; font-weight: 700;
    letter-spacing: 0.06em; text-transform: uppercase;
    background: var(--bg-card); border: 1px solid var(--border); color: var(--accent);
    margin-bottom: 1.5rem;
}
h1.hero-title {
    font-size: clamp(2rem, 5vw, 3.25rem); font-weight: 850;
    letter-spacing: -0.03em; color: var(--text-bright); margin-bottom: 1.25rem;
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
.btn-primary { background: var(--accent); color: var(--accent-contrast); border-color: var(--accent); font-weight: 700; }
.btn-primary:hover { background: var(--accent-hover); border-color: var(--accent-hover); }
.btn-secondary { background: var(--bg-card); color: var(--text); border-color: var(--border); }
.btn-secondary:hover { background: var(--bg-card-hover); border-color: var(--border-dark); }

/* Install Box */
.install-bar {
    display: inline-flex; align-items: center; justify-content: space-between; gap: 0.75rem;
    background: var(--bg-code); border: 1px solid var(--border); border-radius: 8px;
    padding: 0.65rem 1rem; font-family: var(--font-mono); font-size: 0.88rem;
    color: var(--text); max-width: 100%; overflow-x: auto;
}
.install-bar .prompt { color: var(--muted); font-weight: 700; user-select: none; }
.copy-btn {
    background: var(--bg-card); color: var(--text); border: 1px solid var(--border);
    padding: 0.35rem 0.75rem; border-radius: 5px; font-size: 0.75rem; font-weight: 700;
    font-family: var(--font-mono); cursor: pointer; transition: all 0.15s; shrink-0;
}
.copy-btn:hover { background: var(--bg-card-hover); border-color: var(--border-dark); }

/* Stats ribbon */
.stats-ribbon {
    display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 1rem;
    margin: 3rem 0 2rem;
}
.stat-pill {
    background: var(--bg-card); border: 1px solid var(--border); border-radius: 8px;
    padding: 1.25rem 1rem; text-align: center;
}
.stat-val {
    font-size: 1.85rem; font-weight: 850; color: var(--text-bright); font-family: var(--font-mono);
}
.stat-lbl {
    font-size: 0.76rem; color: var(--muted); font-weight: 700; text-transform: uppercase;
    letter-spacing: 0.06em; margin-top: 0.35rem;
}

/* Grids & Cards */
.grid-2 { display: grid; grid-template-columns: repeat(auto-fit, minmax(340px, 1fr)); gap: 1.25rem; }
.grid-3 { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 1.25rem; }
.card {
    background: var(--bg-card); border: 1px solid var(--border); border-radius: 8px;
    padding: 1.5rem; transition: border-color 0.15s ease;
}
.card:hover { border-color: var(--border-dark); }
.card-num {
    font-family: var(--font-mono); font-size: 0.78rem; font-weight: 800;
    color: var(--accent); margin-bottom: 0.5rem; text-transform: uppercase;
}
.card h3 { font-size: 1.15rem; font-weight: 750; margin-bottom: 0.5rem; color: var(--text-bright); }
.card p { color: var(--muted); font-size: 0.92rem; line-height: 1.6; margin: 0; }

/* Code Blocks: Strict Horizontal Scroll Containment */
pre {
    background: var(--bg-code); border: 1px solid var(--border);
    border-radius: 8px; padding: 1.1rem 1.25rem; overflow-x: auto;
    max-width: 100%; min-width: 0; box-sizing: border-box;
    font-family: var(--font-mono); font-size: 0.85rem; line-height: 1.65;
    color: var(--text); -webkit-overflow-scrolling: touch;
}
code {
    font-family: var(--font-mono); font-size: 0.88em;
    background: var(--bg-code-inline); border: 1px solid var(--border);
    padding: 0.12rem 0.35rem; border-radius: 4px; color: var(--accent);
    word-break: break-word;
}
pre code { background: none; border: none; padding: 0; color: inherit; word-break: normal; }
.code-header {
    font-family: var(--font-mono); font-size: 0.75rem; font-weight: 700;
    text-transform: uppercase; letter-spacing: 0.05em; color: var(--muted);
    margin-bottom: 0.45rem; display: flex; justify-content: space-between;
    align-items: center;
}
.code-header span.tag-cf { color: var(--accent-sapphire); }
.code-header span.tag-cpp { color: var(--warn); }
.code-copy-btn {
    background: var(--bg-card);
    color: var(--muted);
    border: 1px solid var(--border);
    padding: 0.15rem 0.55rem;
    border-radius: 4px;
    font-size: 0.7rem;
    font-family: var(--font-mono);
    cursor: pointer;
    transition: all 0.15s ease;
}
.code-copy-btn:hover {
    color: var(--text-bright);
    border-color: var(--border-dark);
    background: var(--bg-card-hover);
}

.section-title {
    font-size: clamp(1.6rem, 3.5vw, 2.2rem); font-weight: 800; margin: 4rem 0 0.75rem;
    text-align: center; letter-spacing: -0.02em; color: var(--text-bright);
}
.section-subtitle {
    text-align: center; color: var(--muted); font-size: 1.05rem; max-width: 640px; margin: 0 auto 2.5rem;
}

.tabs { display: flex; gap: 0.5rem; margin-bottom: 1.25rem; justify-content: center; flex-wrap: wrap; }
.tab-btn {
    background: var(--bg-card); color: var(--muted); border: 1px solid var(--border);
    padding: 0.5rem 1rem; border-radius: 6px; cursor: pointer; font-weight: 600;
    font-size: 0.88rem; font-family: var(--font-mono); transition: all 0.15s;
}
.tab-btn:hover { color: var(--text-bright); border-color: var(--border-dark); }
.tab-btn.active {
    background: var(--bg-card-hover); color: var(--text-bright); border-color: var(--accent);
}
.tab-panel { display: none; }
.tab-panel.active { display: block; }

/* Rosetta Mobile Toggle */
.rosetta-mobile-toggle {
    display: none; margin-bottom: 0.75rem; gap: 0.35rem; background: var(--bg-card);
    padding: 0.25rem; border-radius: 6px; border: 1px solid var(--border);
}
.rosetta-mobile-toggle button {
    flex: 1; padding: 0.45rem 0.5rem; border: none; background: transparent;
    font-family: var(--font-mono); font-size: 0.78rem; font-weight: 700;
    color: var(--muted); border-radius: 4px; cursor: pointer;
}
.rosetta-mobile-toggle button.active {
    background: var(--bg-card-hover); color: var(--text-bright);
}

/* Console preview */
.console-preview {
    background: var(--bg-code); border: 1px solid var(--border); border-radius: 6px;
    padding: 0.75rem 1rem; margin-top: 0.85rem; font-family: var(--font-mono);
    font-size: 0.82rem; color: var(--text);
}
.console-header {
    font-size: 0.7rem; font-weight: 700; color: var(--muted); text-transform: uppercase;
    letter-spacing: 0.05em; margin-bottom: 0.3rem; display: flex; align-items: center; gap: 0.35rem;
}
.console-dot { width: 7px; height: 7px; border-radius: 50%; background: var(--ok); }

/* Robust Table System: Isolated Scrolling & Polished Mobile Experience */
.table-wrap {
    position: relative;
    width: 100%;
    max-width: 100%;
    min-width: 0;
    overflow-x: auto;
    -webkit-overflow-scrolling: touch;
    margin: 1.5rem 0 2.25rem;
    background: var(--bg-card);
    border: 1px solid var(--border);
    border-radius: 8px;
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
    background: var(--bg-table-head); color: var(--text-bright); font-weight: 750; font-size: 0.82rem;
    text-transform: uppercase; letter-spacing: 0.04em;
}
tr:last-child td { border-bottom: none; }
tr:hover td { background: var(--bg-table-hover); }

/* Subtle mobile scroll hint banner */
.table-scroll-cue {
    display: none;
    font-size: 0.72rem;
    font-weight: 700;
    color: var(--muted);
    text-transform: uppercase;
    letter-spacing: 0.05em;
    padding: 0.4rem 0.85rem;
    background: var(--bg-table-head);
    border-bottom: 1px solid var(--border);
    text-align: right;
}

/* Step Grid */
.step-grid {
    display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 1.25rem; margin: 2rem 0;
}
.step-card {
    background: var(--bg-card); border: 1px solid var(--border); border-radius: 8px;
    padding: 1.5rem; position: relative;
}
.step-badge {
    display: inline-block; font-family: var(--font-mono); font-size: 0.72rem;
    font-weight: 800; text-transform: uppercase; letter-spacing: 0.05em;
    background: var(--bg-accent-tint); color: var(--accent); padding: 0.2rem 0.6rem; border-radius: 4px;
    border: 1px solid var(--border-accent-tint); margin-bottom: 0.75rem;
}
.step-card h4 { font-size: 1.15rem; margin-bottom: 0.5rem; color: var(--text-bright); }
.step-card p { font-size: 0.9rem; color: var(--muted); line-height: 1.6; margin-bottom: 0.85rem; }

/* Roadmap */
.roadmap-grid {
    display: grid; grid-template-columns: repeat(auto-fit, minmax(240px, 1fr)); gap: 1rem; margin: 2rem 0;
}
.roadmap-card {
    background: var(--bg-card); border: 1px solid var(--border); border-radius: 8px;
    padding: 1.25rem; transition: border-color 0.15s ease, background 0.15s ease; text-decoration: none; display: block;
}
.roadmap-card:hover {
    background: var(--bg-card-hover); border-color: var(--border-dark); text-decoration: none;
}
.roadmap-num {
    font-family: var(--font-mono); font-size: 0.72rem; font-weight: 800;
    text-transform: uppercase; letter-spacing: 0.05em; color: var(--accent); margin-bottom: 0.35rem;
}
.roadmap-card h4 { font-size: 1.05rem; color: var(--text-bright); margin-bottom: 0.35rem; font-weight: 700; }
.roadmap-card p { font-size: 0.85rem; color: var(--muted); line-height: 1.5; margin: 0; }

.footer {
    text-align: center; padding: 3rem 1rem 2.5rem; border-top: 1px solid var(--border);
    background: var(--bg); color: var(--muted); font-size: 0.88rem; margin-top: 4.5rem;
}
.footer-logo { max-width: 120px; margin-bottom: 1rem; }
.footer-links { display: flex; justify-content: center; gap: 1.5rem; margin: 1rem 0; flex-wrap: wrap; }
.footer-links a { color: var(--muted); font-weight: 600; font-size: 0.88rem; }
.footer-links a:hover { color: var(--text-bright); }

/* Responsive adjustments for mobile & tablet */
.toolbar-actions {
    display: flex;
    align-items: center;
    gap: 0.45rem;
    flex-shrink: 0;
}
.btn-toolbar-ai {
    background: var(--bg-card);
    border: 1px solid var(--accent);
    color: var(--accent);
    padding: 0.3rem 0.65rem;
    border-radius: 6px;
    font-family: var(--font-mono);
    font-size: 0.74rem;
    font-weight: 700;
    display: inline-flex;
    align-items: center;
    gap: 0.35rem;
    cursor: pointer;
    transition: all 0.15s ease;
    white-space: nowrap;
}
.btn-toolbar-ai:hover {
    background: var(--accent);
    color: var(--accent-contrast);
}
.btn-toolbar-share {
    background: var(--bg-card);
    border: 1px solid var(--border);
    color: var(--muted);
    padding: 0.3rem 0.55rem;
    border-radius: 6px;
    font-family: var(--font-mono);
    font-size: 0.74rem;
    font-weight: 600;
    display: inline-flex;
    align-items: center;
    gap: 0.3rem;
    cursor: pointer;
    transition: all 0.15s ease;
    white-space: nowrap;
}
.btn-toolbar-share:hover {
    background: var(--bg-card-hover);
    color: var(--text-bright);
    border-color: var(--border-dark);
}

/* Ask AI with Topic Context Modal & Mobile Bottom Sheet */
.ai-modal-backdrop {
    position: fixed;
    inset: 0;
    background: var(--bg-backdrop);
    z-index: 250;
    display: none;
    align-items: center;
    justify-content: center;
    padding: 1rem;
}
.ai-modal-backdrop.open {
    display: flex;
}
.ai-modal-sheet {
    background: var(--bg-card);
    border: 1px solid var(--border);
    border-radius: 8px;
    width: 100%;
    max-width: 620px;
    max-height: 86vh;
    display: flex;
    flex-direction: column;
    overflow: hidden;
}
.ai-modal-header {
    padding: 0.85rem 1.1rem;
    border-bottom: 1px solid var(--border);
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 0.75rem;
    background: var(--bg-table-head);
}
.ai-modal-title {
    font-family: var(--font-mono);
    font-size: 0.88rem;
    font-weight: 800;
    color: var(--text-bright);
    display: flex;
    align-items: center;
    gap: 0.45rem;
}
.ai-modal-close {
    background: var(--bg);
    border: 1px solid var(--border);
    color: var(--muted);
    width: 28px;
    height: 28px;
    border-radius: 4px;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    cursor: pointer;
    font-size: 1rem;
    line-height: 1;
}
.ai-modal-close:hover {
    color: var(--text-bright);
    border-color: var(--border-dark);
}
.ai-modal-body {
    padding: 1rem 1.1rem;
    overflow-y: auto;
    display: flex;
    flex-direction: column;
    gap: 0.85rem;
}
.ai-modal-sub {
    font-size: 0.84rem;
    color: var(--muted);
    line-height: 1.5;
    margin: 0;
}
.ai-goal-row {
    display: flex;
    flex-wrap: wrap;
    gap: 0.4rem;
}
.ai-goal-pill {
    background: var(--bg);
    border: 1px solid var(--border);
    color: var(--muted);
    padding: 0.3rem 0.65rem;
    border-radius: 5px;
    font-family: var(--font-mono);
    font-size: 0.72rem;
    font-weight: 600;
    cursor: pointer;
    transition: all 0.15s ease;
}
.ai-goal-pill:hover {
    color: var(--text-bright);
    border-color: var(--border-dark);
}
.ai-goal-pill.active {
    background: var(--bg-accent-tint);
    border-color: var(--accent);
    color: var(--accent);
    font-weight: 700;
}
.ai-context-box {
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: 6px;
    padding: 0.7rem 0.85rem;
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
}
.ai-context-opt {
    display: flex;
    align-items: center;
    gap: 0.55rem;
    font-family: var(--font-mono);
    font-size: 0.76rem;
    color: var(--text);
    cursor: pointer;
    user-select: none;
}
.ai-context-opt input[type="checkbox"] {
    accent-color: var(--accent);
    width: 15px;
    height: 15px;
    cursor: pointer;
}
.ai-preview-head {
    display: flex;
    align-items: center;
    justify-content: space-between;
    font-family: var(--font-mono);
    font-size: 0.68rem;
    font-weight: 700;
    text-transform: uppercase;
    letter-spacing: 0.05em;
    color: var(--muted);
}
.ai-prompt-preview {
    width: 100%;
    height: 150px;
    background: var(--bg-code);
    border: 1px solid var(--border);
    border-radius: 6px;
    padding: 0.65rem 0.75rem;
    font-family: var(--font-mono);
    font-size: 0.74rem;
    line-height: 1.5;
    color: var(--text);
    resize: vertical;
    outline: none;
}
.ai-prompt-preview:focus {
    border-color: var(--accent);
}
.ai-modal-footer {
    padding: 0.75rem 1.1rem;
    border-top: 1px solid var(--border);
    background: var(--bg-table-head);
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 0.65rem;
    flex-wrap: wrap;
}
.ai-btn-secondary {
    background: var(--bg);
    border: 1px solid var(--border);
    color: var(--text);
    padding: 0.48rem 0.85rem;
    border-radius: 6px;
    font-family: var(--font-mono);
    font-size: 0.76rem;
    font-weight: 600;
    cursor: pointer;
    display: inline-flex;
    align-items: center;
    gap: 0.4rem;
    text-decoration: none !important;
}
.ai-btn-secondary:hover {
    background: var(--bg-card-hover);
    border-color: var(--border-dark);
    color: var(--text-bright);
}
.ai-btn-primary {
    background: var(--accent);
    border: 1px solid var(--accent);
    color: var(--accent-contrast);
    padding: 0.48rem 1rem;
    border-radius: 6px;
    font-family: var(--font-mono);
    font-size: 0.78rem;
    font-weight: 800;
    cursor: pointer;
    display: inline-flex;
    align-items: center;
    gap: 0.4rem;
    margin-left: auto;
}
.ai-btn-primary:hover {
    background: var(--accent-hover);
    border-color: var(--accent-hover);
}

@media (max-width: 1100px) {
    .guide-toc-rail { display: none; }
}

@media (max-width: 900px) {
    .bento-grid { grid-template-columns: 1fr; }
    .bento-card.span-2 { grid-column: span 1; }
}

@media (max-width: 840px) {
    .container { padding: 1.5rem 1rem 3.5rem; }
    .doc-layout { display: block; width: 100%; min-width: 0; }
    .sidebar {
        position: static; max-height: none; width: 100%;
        margin-bottom: 1.25rem; padding: 0.7rem 0.9rem;
    }
    .guide-shell {
        display: block;
        height: auto;
        overflow: visible;
    }
    .guide-sidebar {
        position: sticky;
        top: 0;
        z-index: 90;
        width: 100%;
        height: auto;
        max-height: none;
        border-right: none;
        border-bottom: 1px solid var(--border);
        margin-bottom: 0;
        padding: 0.45rem 0.75rem;
        background: var(--bg-nav);
        backdrop-filter: blur(8px);
    }
    .guide-viewport {
        height: auto;
        overflow: visible;
        display: block;
    }
    .guide-content-inner {
        padding: 1rem 1rem 3.5rem;
    }
    /* Unified 1-Line Mobile Sub-Header: [☰ Ch 1: Install & Setup ▾] [1.1 Toolchain ▾] [Paged] */
    .mobile-unified-bar {
        display: flex;
        align-items: center;
        gap: 0.4rem;
        width: 100%;
    }
    .sidebar-summary {
        display: inline-flex; align-items: center; justify-content: space-between;
        gap: 0.35rem;
        flex: 1 1 0;
        min-width: 0;
        text-align: left;
        font-family: var(--font-mono);
        font-weight: 700; font-size: 0.75rem; cursor: pointer; color: var(--text-bright);
        user-select: none;
        background: var(--bg-card);
        border: 1px solid var(--border);
        border-radius: 6px;
        padding: 0.38rem 0.55rem;
    }
    .sidebar-summary #mobile-sidebar-label,
    .sidebar-summary #ref-mobile-sidebar-label {
        white-space: nowrap;
        overflow: hidden;
        text-overflow: ellipsis;
        min-width: 0;
    }
    .sidebar-summary::-webkit-details-marker { display: none; }
    .sidebar-summary-badge {
        color: var(--accent);
        font-weight: 800;
        margin-right: 0.25rem;
    }
    .sidebar-chevron { font-size: 0.95rem; color: var(--muted); transition: transform 0.2s ease; flex-shrink: 0; }
    .mobile-topic-select {
        flex: 1 1 0;
        min-width: 0;
        max-width: 46%;
        background: var(--bg-accent-tint);
        color: var(--accent);
        border: 1px solid var(--border-accent-tint);
        border-radius: 6px;
        padding: 0.36rem 0.45rem;
        font-family: var(--font-mono);
        font-size: 0.73rem;
        font-weight: 750;
        cursor: pointer;
        outline: none;
        text-overflow: ellipsis;
    }
    .mobile-mode-btn {
        flex-shrink: 0;
        background: var(--bg-card);
        color: var(--muted);
        border: 1px solid var(--border);
        border-radius: 6px;
        padding: 0.36rem 0.5rem;
        font-family: var(--font-mono);
        font-size: 0.7rem;
        font-weight: 700;
        cursor: pointer;
        white-space: nowrap;
    }
    .mobile-mode-btn:hover {
        color: var(--text-bright);
        border-color: var(--border-dark);
    }
    .sidebar-details:not(.open) .sidebar-content { display: none; }
    .sidebar-details.open .sidebar-content {
        display: block; margin-top: 0.55rem; padding: 0.75rem;
        background: var(--bg-card);
        border: 1px solid var(--border);
        border-radius: 6px;
        max-height: 62vh; overflow-y: auto;
    }
    .sidebar-details.open .sidebar-chevron { transform: rotate(180deg); }
    /* Eliminate duplicated breadcrumbs and subsection chip bar on mobile */
    .reader-toolbar { display: none !important; }
    .topic-pill-bar { display: none !important; }
    .grid-2 { grid-template-columns: 1fr; }
    .rosetta-mobile-toggle { display: flex; }
    .rosetta-pane-cpp { display: none; }
    .rosetta-pane-cpp.is-active { display: block; }
    .rosetta-pane-cf.is-inactive { display: none; }
    .hero { padding: 2rem 0.25rem 2.5rem; }
}

@media (max-width: 680px) {
    .navbar { position: relative; }
    .navbar-inner { flex-wrap: wrap; padding: 0.5rem 0.75rem; gap: 0.4rem; }
    .brand { font-size: 1.05rem; }
    .brand img.brand-icon { width: 24px; height: 24px; }
    .nav-links {
        display: flex; align-items: center; gap: 0.35rem;
        width: 100%; overflow-x: auto; -webkit-overflow-scrolling: touch;
        scrollbar-width: none; padding: 0.1rem 0;
    }
    .nav-links::-webkit-scrollbar { display: none; }
    .nav-links a {
        white-space: nowrap; padding: 0.3rem 0.6rem; border-radius: 5px;
        background: var(--bg-card); border: 1px solid var(--border); font-size: 0.78rem; color: var(--muted); flex-shrink: 0;
    }
    .nav-links a.active { background: var(--bg-subtle); color: var(--text-bright) !important; }
    .btn-nav-primary {
        margin-left: auto; padding: 0.3rem 0.65rem !important;
        font-size: 0.76rem !important; flex-shrink: 0;
    }
    .mode-btn {
        padding: 0.28rem 0.55rem;
        font-size: 0.7rem;
    }
    .chapter-pager {
        flex-direction: column;
        gap: 0.65rem;
        margin-top: 2.25rem;
    }
    .pager-card {
        width: 100%;
        min-width: 0;
        padding: 0.95rem 1.1rem;
    }
    .pager-card.empty { display: none; }
    .pager-card.pager-next, .pager-card.next {
        text-align: left;
        margin-left: 0;
    }
    .ai-modal-backdrop {
        align-items: flex-end;
        padding: 0;
    }
    .ai-modal-sheet {
        border-radius: 12px 12px 0 0;
        max-width: 100%;
        max-height: 88vh;
        border-bottom: none;
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
        background: var(--bg-card); border: 1px solid var(--border); border-radius: 8px;
        margin-bottom: 0.85rem; padding: 0.95rem 1rem;
    }
    .api-table-wrap td { padding: 0.25rem 0; border: none; font-size: 0.88rem; }
    .api-table-wrap td:first-child {
        padding-bottom: 0.45rem; border-bottom: 1px dashed var(--border); margin-bottom: 0.45rem;
    }
    .api-table-wrap td:first-child code {
        font-weight: 700; font-size: 0.88rem; color: var(--accent);
        background: var(--bg-code-inline); border-color: var(--border); word-break: break-all;
        display: inline-block; padding: 0.2rem 0.45rem; border-radius: 4px;
    }
    .api-table-wrap td:nth-child(2)::before {
        content: "C++23: "; font-weight: 700; font-size: 0.72rem; text-transform: uppercase;
        letter-spacing: 0.05em; color: var(--muted); display: inline-block; margin-right: 0.35rem;
    }
    .api-table-wrap td:nth-child(2) {
        font-size: 0.82rem; color: var(--muted); padding-bottom: 0.35rem; word-break: break-all;
    }
    .api-table-wrap td:last-child { color: var(--text); line-height: 1.55; }
    .api-table-wrap td:last-child code {
        display: block; margin-top: 0.35rem; word-break: break-all; background: var(--bg-code);
    }
}

@media (min-width: 841px) {
    .rosetta-mobile-toggle { display: none; }
    .rosetta-pane-cf, .rosetta-pane-cpp { display: block !important; }
    .sidebar-details > .sidebar-content { display: block !important; }
}
)CSS";
}

[[nodiscard]] inline auto ai_context_modal_html() -> std::string {
    return R"HTML(
<div class="ai-modal-backdrop" id="ai-modal-backdrop" onclick="if(event.target===this)closeAiModal()">
  <div class="ai-modal-sheet" role="dialog" aria-modal="true" aria-labelledby="ai-modal-title">
    <div class="ai-modal-header">
      <div class="ai-modal-title" id="ai-modal-title">
        <span class="material-symbols-outlined" style="color:#F97316;font-size:1.1rem;" aria-hidden="true">auto_awesome</span>
        <span>Ask AI with Topic Context</span>
      </div>
      <button type="button" class="ai-modal-close" onclick="closeAiModal()" aria-label="Close modal">&times;</button>
    </div>
    <div class="ai-modal-body">
      <p class="ai-modal-sub">
        Assemble a self-contained prompt with the active documentation section, runnable <code>.cf</code> code snippets, and a compact <code>cfire</code> language primer so any AI assistant writes valid <code>cfire</code> code immediately.
      </p>
      <div class="ai-goal-row" role="group" aria-label="Prompt Goal">
        <button type="button" class="ai-goal-pill active" data-goal="explain" onclick="setAiGoal('explain', this)">Explain &amp; Walk Through</button>
        <button type="button" class="ai-goal-pill" data-goal="build" onclick="setAiGoal('build', this)">Build a Practical Feature</button>
        <button type="button" class="ai-goal-pill" data-goal="debug" onclick="setAiGoal('debug', this)">Review / Debug My Code</button>
      </div>
      <div class="ai-context-box">
        <label class="ai-context-opt">
          <input type="checkbox" id="ai-opt-topic" checked onchange="updateAiPromptPreview()" />
          <span id="ai-opt-topic-label">Include Active Section Prose &amp; API Signatures</span>
        </label>
        <label class="ai-context-opt">
          <input type="checkbox" id="ai-opt-code" checked onchange="updateAiPromptPreview()" />
          <span id="ai-opt-code-label">Include Visible .cf &amp; Emitted C++23 Code Snippets</span>
        </label>
        <label class="ai-context-opt">
          <input type="checkbox" id="ai-opt-primer" checked onchange="updateAiPromptPreview()" />
          <span>Include Compact cfire Grammar &amp; Safety Primer (prevents Rust/Go hallucinations)</span>
        </label>
      </div>
      <div class="ai-preview-head">
        <span>Generated Markdown Prompt Preview (Editable)</span>
        <span id="ai-prompt-stats">0 chars</span>
      </div>
      <textarea id="ai-prompt-preview" class="ai-prompt-preview" spellcheck="false"></textarea>
    </div>
    <div class="ai-modal-footer">
      <a href="LLMS.txt" target="_blank" rel="noopener" class="ai-btn-secondary" title="Open complete single-file LLM specification">
        <span class="material-symbols-outlined" style="font-size:0.95rem;" aria-hidden="true">description</span>
        <span>Open Full LLMS.txt</span>
      </a>
      <button type="button" class="ai-btn-primary" id="btn-copy-ai-prompt" onclick="copyAiRichPrompt()">
        <span class="material-symbols-outlined" style="font-size:0.95rem;" aria-hidden="true">content_copy</span>
        <span id="btn-copy-ai-label">Copy Rich Prompt</span>
      </button>
    </div>
  </div>
</div>
<script>
let currentAiGoal = 'explain';

const CFIRE_GRAMMAR_PRIMER = [
  '## cfire Language Quick Reference (ISO C++23 Transpiler)',
  '- File header: `package main` and `import "core:str"`, `import "ext:webui"`.',
  '- Bindings: `x := 42` (immutable by default), `mut count := 0` (mutable).',
  '- Functions & Intents: `fn update(mut s: State, in cfg: Config, move buf: []u8) -> i32!`. Callers MUST annotate `mut`, `move`, or `out` at the call site: `update(mut s, cfg, move buf)`.',
  '- Error Handling: Fallible return `T!` (`std::expected<T, cfire::Error>`). Propagate with prefix `try expr` (NEVER postfix `?`), fallback with `expr or fallback`, or `expr or |err| { ... }`.',
  '- Optionals & Pointers: `T?` (`std::optional<T>`) with `none`; `*T` / `*mut T` (non-owning pointer, requires `if p != none` before `*p`); `^T` (`own[T]`); `defer cleanup()`.',
  '- Strings: All double-quoted strings interpolate `{expr}` natively (e.g. `"Hello, {name}!"` — do NOT prefix with `f"..."`).',
  '- Testing & CLI: `test "name" { assert(cond) }`, run with `cfire test`, `cfire check`, `cfire fix`, `cfire run`.'
].join('\n');

function extractActiveDocContext() {
  let title = document.title;
  let prose = '';
  let snippets = [];

  // Check guide.html active topic or chapter
  const activePane = document.querySelector('.chapter-pane.active');
  const vp = document.getElementById('guide-viewport');
  if (activePane) {
    const chNum = activePane.getAttribute('data-chapter') || '';
    const chTitle = activePane.getAttribute('data-title') || '';
    const isTopicMode = vp && vp.classList.contains('topic-View-mode');
    const activeTopic = isTopicMode ? activePane.querySelector('.guide-topic.active-topic') : activePane;
    const scopeEl = activeTopic || activePane;
    const topicTitle = (activeTopic && activeTopic.getAttribute('data-topic-title'))
      ? ('Ch ' + chNum + ': ' + chTitle + ' — ' + activeTopic.getAttribute('data-topic-title'))
      : ('Ch ' + chNum + ': ' + chTitle);
    title = topicTitle;

    scopeEl.querySelectorAll('p, li, .callout, .table-wrap').forEach(el => {
      if (el.closest('pre') || el.closest('.chapter-pager') || el.closest('.topic-pager')) return;
      const t = (el.innerText || '').trim();
      if (t && prose.length < 3200) prose += t + '\n\n';
    });
    scopeEl.querySelectorAll('pre code').forEach((codeEl, idx) => {
      if (snippets.length < 4) {
        const codeTxt = (codeEl.innerText || '').trim();
        if (codeTxt) snippets.push('```cf\n' + codeTxt + '\n```');
      }
    });
  } else {
    // Check reference.html active pane
    const refPane = document.querySelector('.ref-pane.active');
    if (refPane) {
      title = refPane.getAttribute('data-pane-title') || title;
      refPane.querySelectorAll('p, tbody tr').forEach(el => {
        const t = (el.innerText || '').replace(/\s+/g, ' ').trim();
        if (t && prose.length < 3500) prose += '- ' + t + '\n';
      });
      refPane.querySelectorAll('pre code').forEach(codeEl => {
        if (snippets.length < 4) {
          const codeTxt = (codeEl.innerText || '').trim();
          if (codeTxt) snippets.push('```cf\n' + codeTxt + '\n```');
        }
      });
    }
  }
  return { title, prose: prose.trim(), snippets };
}

function updateAiPromptPreview() {
  const ctx = extractActiveDocContext();
  const incTopic = document.getElementById('ai-opt-topic')?.checked ?? true;
  const incCode = document.getElementById('ai-opt-code')?.checked ?? true;
  const incPrimer = document.getElementById('ai-opt-primer')?.checked ?? true;

  const topicLbl = document.getElementById('ai-opt-topic-label');
  if (topicLbl) topicLbl.textContent = 'Active Section (' + ctx.title + ')';
  const codeLbl = document.getElementById('ai-opt-code-label');
  if (codeLbl) codeLbl.textContent = 'Visible Code Snippets (' + ctx.snippets.length + ' block' + (ctx.snippets.length === 1 ? '' : 's') + ')';

  let goalInstruction = 'Walk me through this cfire topic step by step, explain the key design trade-offs, and show a practical, runnable `.cf` example.';
  if (currentAiGoal === 'build') {
    goalInstruction = 'Using the cfire APIs and patterns below, help me design and build a complete, idiomatic `.cf` module with a `test "..."` block.';
  } else if (currentAiGoal === 'debug') {
    goalInstruction = 'Use the cfire grammar rules and API signatures below to review or debug my `.cf` code (I will paste my code below):';
  }

  let parts = [];
  parts.push('I am working with the **cfire** systems programming language (`' + ctx.title + '`).\n\n**Task:** ' + goalInstruction);
  if (incPrimer) {
    parts.push(CFIRE_GRAMMAR_PRIMER);
  }
  if (incTopic && ctx.prose) {
    parts.push('## Active Documentation Context: ' + ctx.title + '\n' + ctx.prose);
  }
  if (incCode && ctx.snippets.length > 0) {
    parts.push('## Code Examples from This Section\n' + ctx.snippets.join('\n\n'));
  }
  parts.push('Reference URL: ' + window.location.href);

  const fullPrompt = parts.join('\n\n---\n\n');
  const box = document.getElementById('ai-prompt-preview');
  if (box) box.value = fullPrompt;
  const stats = document.getElementById('ai-prompt-stats');
  if (stats) stats.textContent = fullPrompt.length.toLocaleString() + ' chars (~' + Math.ceil(fullPrompt.length / 4) + ' tokens)';
}

function setAiGoal(goal, btn) {
  currentAiGoal = goal;
  document.querySelectorAll('.ai-goal-pill').forEach(b => b.classList.remove('active'));
  if (btn) btn.classList.add('active');
  updateAiPromptPreview();
}

function openAiModal() {
  updateAiPromptPreview();
  const backdrop = document.getElementById('ai-modal-backdrop');
  if (backdrop) backdrop.classList.add('open');
}

function closeAiModal() {
  const backdrop = document.getElementById('ai-modal-backdrop');
  if (backdrop) backdrop.classList.remove('open');
}

function askAiAgent() {
  openAiModal();
}

function copyAiRichPrompt() {
  const box = document.getElementById('ai-prompt-preview');
  if (!box) return;
  navigator.clipboard.writeText(box.value).then(() => {
    const lbl = document.getElementById('btn-copy-ai-label');
    const railLbl = document.getElementById('ai-agent-label');
    if (lbl) {
      lbl.textContent = 'Copied Rich Prompt!';
      setTimeout(() => { lbl.textContent = 'Copy Rich Prompt'; }, 2200);
    }
    if (railLbl) {
      railLbl.textContent = 'Prompt Copied!';
      setTimeout(() => { railLbl.textContent = 'Ask AI Agent'; }, 2200);
    }
  });
}
</script>
)HTML";
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
      <button type="button" class="btn-theme-toggle" id="theme-toggle-btn" onclick="toggleCfireTheme()" title="Switch between Swiss Alabaster Light and Obsidian Dark themes">
        <span class="material-symbols-outlined" id="theme-toggle-icon" style="font-size:0.95rem;color:var(--accent);" aria-hidden="true">light_mode</span>
        <span id="theme-toggle-label">Light</span>
      </button>
      <a href="https://github.com/gsandoval/cfire" target="_blank" rel="noopener" class="btn-nav-github">
        <svg width="14" height="14" viewBox="0 0 16 16" fill="currentColor" style="vertical-align: text-bottom; margin-right: 0.35rem;"><path d="M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27.68 0 1.36.09 2 .27 1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.013 8.013 0 0 0 16 8c0-4.42-3.58-8-8-8z"/></svg>
        GitHub
      </a>
      <a href="index.html#install" class="btn-nav-primary">Install v{3}</a>
    </div>
  </div>
</nav>
<script>
function syncCfireThemeBtn() {{
  var t = document.documentElement.getAttribute('data-theme') || 'light';
  var icon = document.getElementById('theme-toggle-icon');
  var lbl = document.getElementById('theme-toggle-label');
  if (icon && lbl) {{
    if (t === 'dark') {{
      icon.textContent = 'dark_mode';
      icon.style.color = 'var(--accent-sapphire)';
      lbl.textContent = 'Dark';
    }} else {{
      icon.textContent = 'light_mode';
      icon.style.color = 'var(--accent)';
      lbl.textContent = 'Light';
    }}
  }}
}}
function toggleCfireTheme() {{
  var cur = document.documentElement.getAttribute('data-theme') || 'light';
  var next = (cur === 'dark') ? 'light' : 'dark';
  document.documentElement.setAttribute('data-theme', next);
  try {{ localStorage.setItem('cfire-theme', next); }} catch (e) {{}}
  syncCfireThemeBtn();
}}
syncCfireThemeBtn();
</script>
)HTML",
        active_page == "index" ? "active" : "",
        active_page == "guide" ? "active" : "",
        active_page == "reference" ? "active" : "",
        cfire::version::kVersion);
}

} // namespace detail

[[nodiscard]] inline auto html_escape(std::string_view s) -> std::string {
    return detail::html_escape(s);
}

} // namespace cfire::doc
