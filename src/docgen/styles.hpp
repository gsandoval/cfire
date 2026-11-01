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
    --bg: #0D1117;
    --bg-card: #161B22;
    --bg-card-hover: #21262D;
    --bg-code: #090D12;
    --bg-subtle: #21262D;
    --border: #30363D;
    --border-dark: #484F58;
    --text: #F0F6FC;
    --text-bright: #FFFFFF;
    --muted: #8B949E;
    --accent: #F97316;
    --accent-hover: #FB923C;
    --accent-sapphire: #38BDF8;
    --ok: #3FB950;
    --warn: #D29922;
    --err: #F85149;
    --font-sans: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", sans-serif;
    --font-mono: ui-monospace, "JetBrains Mono", SFMono-Regular, Menlo, Monaco, Consolas, monospace;
}
*, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }
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
::selection { background: #F97316; color: #0D1117; }
a {
    color: var(--accent-sapphire);
    text-decoration: none;
    transition: color 0.15s ease, border-color 0.15s ease;
}
a:hover {
    color: #7DD3FC;
}

/* Crisp Technical Prose Links */
main a:not(.pager-card):not(.btn):not(.code-copy-btn):not(.sidebar-ch-link):not(.brand),
.chapter-pane a:not(.pager-card):not(.btn):not(.code-copy-btn),
.callout a {
    color: var(--accent-sapphire);
    text-decoration: none;
    border-bottom: 1px solid rgba(56, 189, 248, 0.35);
    font-weight: 500;
    transition: color 0.15s ease, border-bottom-color 0.15s ease;
}
main a:not(.pager-card):not(.btn):not(.code-copy-btn):not(.sidebar-ch-link):not(.brand):hover,
.chapter-pane a:not(.pager-card):not(.btn):not(.code-copy-btn):hover,
.callout a:hover {
    color: #7DD3FC;
    border-bottom-color: #38BDF8;
    text-decoration: none;
}
main a code, .chapter-pane a code {
    color: inherit;
    border-color: rgba(56, 189, 248, 0.4);
    background: #161B22;
}

/* Sticky Edge-to-Edge Navigation Bar */
.navbar {
    position: sticky; top: 0; z-index: 100;
    width: 100%;
    background: #0D1117;
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
    font-weight: 800; font-size: 1.25rem; color: var(--text-bright);
    text-decoration: none; flex-shrink: 0;
}
.brand:hover { text-decoration: none; }
.brand img.brand-icon {
    width: 30px; height: 30px; object-fit: contain;
}
.brand span.brand-text {
    font-size: 1.2rem; font-weight: 850; letter-spacing: -0.02em; color: var(--text-bright);
}
.brand span.fire { color: #F97316; font-weight: 850; }
.brand-badge {
    font-family: var(--font-mono); font-size: 0.65rem; font-weight: 700;
    background: #21262D; border: 1px solid var(--border); color: #8B949E;
    padding: 0.12rem 0.45rem; border-radius: 4px; margin-left: 0.25rem;
}
.nav-links { display: flex; align-items: center; gap: 1.25rem; font-size: 0.92rem; font-weight: 600; }
.nav-links a {
    color: var(--muted); transition: all 0.15s ease;
    padding: 0.25rem 0.4rem; border-radius: 5px;
}
.nav-links a:hover { color: var(--text-bright); text-decoration: none; background: #21262D; }
.nav-links a.active { color: var(--text-bright); text-decoration: none; font-weight: 750; background: #21262D; }
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
    background: #F97316 !important; color: #0D1117 !important;
    padding: 0.4rem 0.9rem !important; border-radius: 6px !important; font-size: 0.85rem !important; font-weight: 700 !important;
    transition: background 0.15s !important; border: none;
    text-decoration: none !important;
    border-bottom: none !important;
}
.btn-nav-primary:hover { background: #FB923C !important; text-decoration: none !important; }

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
    background: #090D12;
}
.guide-sidebar::-webkit-scrollbar-thumb,
.guide-viewport::-webkit-scrollbar-thumb,
.guide-toc-rail::-webkit-scrollbar-thumb,
pre::-webkit-scrollbar-thumb {
    background: #30363D;
    border-radius: 3px;
}
.guide-sidebar::-webkit-scrollbar-thumb:hover,
.guide-viewport::-webkit-scrollbar-thumb:hover,
.guide-toc-rail::-webkit-scrollbar-thumb:hover,
pre::-webkit-scrollbar-thumb:hover {
    background: #484F58;
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
    flex-wrap: wrap;
}
.reader-breadcrumbs {
    font-family: var(--font-mono);
    font-size: 0.78rem;
    color: var(--muted);
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
    color: #F97316;
    font-weight: 700;
}
.reader-mode-toggle {
    display: flex;
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
    background: #21262D;
    color: var(--text-bright);
    font-weight: 700;
}

/* Chapter Panes */
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
.view-all-mode .chapter-pager {
    display: none;
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
    border-bottom: 1px solid rgba(48, 54, 61, 0.4);
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
    color: #F97316;
    background: #21262D;
    border: 1px solid var(--border);
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
    border-color: #F97316;
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
    background: #21262D;
    border: 1px solid var(--border);
    padding: 0.08rem 0.35rem;
    border-radius: 3px;
    pointer-events: none;
    line-height: 1.2;
}

.sidebar-part-title {
    font-size: 0.72rem; font-weight: 850; text-transform: uppercase;
    letter-spacing: 0.08em; color: #F97316; margin: 1.25rem 0 0.4rem;
    padding-left: 0.4rem;
}
.sidebar-chapter-list { list-style: none; margin-bottom: 0.75rem; }
.sidebar-chapter-list .nav-item { margin-bottom: 0.2rem; }

.sidebar-ch-link {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 0.5rem;
    padding: 0.42rem 0.6rem;
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
    background: #21262D;
    font-weight: 600;
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
    font-size: 0.6rem;
    font-weight: 800;
    color: var(--accent);
    background: #27140B;
    border: 1px solid #7C2D12;
    padding: 0.08rem 0.35rem;
    border-radius: 3px;
    letter-spacing: 0.06em;
    text-transform: uppercase;
    flex-shrink: 0;
}

.subhead-list {
    display: none;
    list-style: none;
    margin: 0.2rem 0 0.45rem 0.85rem;
    padding-left: 0.65rem;
    border-left: 1px solid var(--border);
}
.nav-item.is-active .subhead-list {
    display: block;
    animation: fadeIn 0.15s ease-in;
}
.subhead-list li { margin-bottom: 0.15rem; }
.subhead-list a {
    display: flex;
    align-items: baseline;
    gap: 0.45rem;
    font-size: 0.78rem;
    color: var(--muted);
    padding: 0.18rem 0.35rem;
    border-radius: 3px;
    text-decoration: none !important;
    border-bottom: none !important;
    transition: color 0.15s ease;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
}
.subhead-list a::before {
    content: "•";
    color: var(--border-dark);
    font-size: 0.75rem;
    flex-shrink: 0;
}
.subhead-list a:hover {
    color: var(--text-bright);
    text-decoration: none !important;
}
.subhead-list a.active-subhead {
    color: var(--accent);
    font-weight: 600;
}
.subhead-list a.active-subhead::before {
    color: var(--accent);
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
    background: #0B1622;
    border: 1px solid #1E3A5F;
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
    border-color: #F97316;
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
    background: var(--bg-card-hover); color: #F97316; font-weight: 700;
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
    color: var(--text-bright); letter-spacing: -0.03em; margin-bottom: 0.75rem;
}
main h2 {
    font-size: 1.5rem; font-weight: 800; color: var(--text-bright);
    letter-spacing: -0.02em; margin: 2.5rem 0 0.85rem; padding-bottom: 0.4rem;
    border-bottom: 1px solid var(--border);
}
main h3 {
    font-size: 1.2rem; font-weight: 700; color: var(--text-bright);
    margin: 1.75rem 0 0.5rem;
}
main p { color: var(--text); font-size: 0.98rem; line-height: 1.7; margin-bottom: 1rem; }
main ul, main ol { margin: 0 0 1.25rem 1.5rem; color: var(--text); font-size: 0.98rem; line-height: 1.7; }
main li { margin-bottom: 0.4rem; }

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
    color: #F97316;
    background: #27140B;
    border: 1px solid #7C2D12;
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
    border-left-color: #38BDF8;
    background: #0B1622;
}
.callout-note .callout-title { color: #38BDF8; }
.callout-tip {
    border-left-color: #3FB950;
    background: #0A1C14;
}
.callout-tip .callout-title { color: #3FB950; }
.callout-important {
    border-left-color: #A78BFA;
    background: #171124;
}
.callout-important .callout-title { color: #A78BFA; }
.callout-warning {
    border-left-color: #F59E0B;
    background: #1E1606;
}
.callout-warning .callout-title { color: #F59E0B; }

/* Hero section */
.hero { text-align: center; padding: 3rem 0.5rem 3.5rem; }
.badge {
    display: inline-block; padding: 0.35rem 0.85rem; border-radius: 999px;
    font-family: var(--font-mono); font-size: 0.75rem; font-weight: 700;
    letter-spacing: 0.06em; text-transform: uppercase;
    background: var(--bg-card); border: 1px solid var(--border); color: #F97316;
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
.btn-primary { background: #F97316; color: #0D1117; border-color: #F97316; font-weight: 700; }
.btn-primary:hover { background: #FB923C; border-color: #FB923C; }
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
    color: #F97316; margin-bottom: 0.5rem; text-transform: uppercase;
}
.card h3 { font-size: 1.15rem; font-weight: 750; margin-bottom: 0.5rem; color: var(--text-bright); }
.card p { color: var(--muted); font-size: 0.92rem; line-height: 1.6; margin: 0; }

/* Code Blocks: Strict Horizontal Scroll Containment */
pre {
    background: var(--bg-code); border: 1px solid var(--border);
    border-radius: 8px; padding: 1.1rem 1.25rem; overflow-x: auto;
    max-width: 100%; min-width: 0; box-sizing: border-box;
    font-family: var(--font-mono); font-size: 0.85rem; line-height: 1.65;
    color: #F0F6FC; -webkit-overflow-scrolling: touch;
}
code {
    font-family: var(--font-mono); font-size: 0.88em;
    background: #1C2128; border: 1px solid var(--border);
    padding: 0.12rem 0.35rem; border-radius: 4px; color: #F97316;
    word-break: break-word;
}
pre code { background: none; border: none; padding: 0; color: inherit; word-break: normal; }
.code-header {
    font-family: var(--font-mono); font-size: 0.75rem; font-weight: 700;
    text-transform: uppercase; letter-spacing: 0.05em; color: var(--muted);
    margin-bottom: 0.45rem; display: flex; justify-content: space-between;
    align-items: center;
}
.code-header span.tag-cf { color: #38BDF8; }
.code-header span.tag-cpp { color: #F59E0B; }
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
    background: var(--bg-card-hover); color: var(--text-bright); border-color: #F97316;
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
.console-dot { width: 7px; height: 7px; border-radius: 50%; background: #3FB950; }

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
    background: #1C2128; color: var(--text-bright); font-weight: 750; font-size: 0.82rem;
    text-transform: uppercase; letter-spacing: 0.04em;
}
tr:last-child td { border-bottom: none; }
tr:hover td { background: #1F242C; }

/* Subtle mobile scroll hint banner */
.table-scroll-cue {
    display: none;
    font-size: 0.72rem;
    font-weight: 700;
    color: var(--muted);
    text-transform: uppercase;
    letter-spacing: 0.05em;
    padding: 0.4rem 0.85rem;
    background: #1C2128;
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
    background: #21262D; color: #F97316; padding: 0.2rem 0.6rem; border-radius: 4px;
    border: 1px solid var(--border); margin-bottom: 0.75rem;
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
    text-transform: uppercase; letter-spacing: 0.05em; color: #F97316; margin-bottom: 0.35rem;
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
        margin-bottom: 2rem; padding: 0.85rem 1rem;
    }
    .guide-shell {
        display: block;
        height: auto;
        overflow: visible;
    }
    .guide-sidebar {
        position: static;
        width: 100%;
        height: auto;
        max-height: none;
        border-right: none;
        border-bottom: 1px solid var(--border);
        margin-bottom: 1.5rem;
        padding: 0.85rem 1rem;
    }
    .guide-viewport {
        height: auto;
        overflow: visible;
        display: block;
    }
    .guide-content-inner {
        padding: 1.5rem 1rem 4rem;
    }
    .sidebar-summary {
        display: flex; align-items: center; justify-content: space-between;
        font-weight: 750; font-size: 0.9rem; cursor: pointer; color: var(--text-bright);
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
        background: #161B22; font-size: 0.82rem; color: var(--muted); flex-shrink: 0;
    }
    .nav-links a.active { background: #21262D; color: var(--text-bright) !important; }
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
        background: var(--bg-card); border: 1px solid var(--border); border-radius: 8px;
        margin-bottom: 0.85rem; padding: 0.95rem 1rem;
    }
    .api-table-wrap td { padding: 0.25rem 0; border: none; font-size: 0.88rem; }
    .api-table-wrap td:first-child {
        padding-bottom: 0.45rem; border-bottom: 1px dashed var(--border); margin-bottom: 0.45rem;
    }
    .api-table-wrap td:first-child code {
        font-weight: 700; font-size: 0.88rem; color: #F97316;
        background: #21262D; border-color: var(--border); word-break: break-all;
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
      <a href="https://github.com/gsandoval/cfire" target="_blank" rel="noopener" class="btn-nav-github">
        <svg width="14" height="14" viewBox="0 0 16 16" fill="currentColor" style="vertical-align: text-bottom; margin-right: 0.35rem;"><path d="M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27.68 0 1.36.09 2 .27 1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.013 8.013 0 0 0 16 8c0-4.42-3.58-8-8-8z"/></svg>
        GitHub 4.2k ★
      </a>
      <a href="index.html#install" class="btn-nav-primary">Install v0.9.4</a>
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
