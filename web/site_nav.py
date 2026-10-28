#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""OPX fork: the site's pages linked to each other. The site (CI, .github/workflows/build.yml) holds four builds:

    ./              OPX, the latest release (else this build)
    dev/            OPX, this build of opx-engine
    opx-solo/       OPX Solo, the latest release that has it
    dev/opx-solo/   OPX Solo, this build

each with webapp/installer/, webapp/editor/ and webapp/try/ (the simulator) where built. Into every one of those
pages, after <body>, goes a bar: for each build, its version and links to its installer, editor and simulator
(relative, so the site works under any address), the page's own one marked.

    web/site_nav.py SITE_DIR"""
import html
import re
import sys
from pathlib import Path

BUILDS = [("", "OPX"), ("dev/", "OPX dev"), ("opx-solo/", "Solo"), ("dev/opx-solo/", "Solo dev")]
PAGES = [("webapp/installer/", "Installer"), ("webapp/editor/", "Editor"), ("webapp/try/", "Simulator")]
MARK = "<!--site-nav-->"


def version(site, base):
    """the build's version, as its installer page names it (make_site.py's META), else its simulator's"""
    for page, rx in (("webapp/installer/index.html", r'"version":\s*"([^"]+)"'),
                     ("webapp/try/index.html", r"v\d[\w.\-+]*")):
        f = site / base / page
        if f.exists():
            m = re.search(rx, f.read_text(encoding="utf-8", errors="replace"))
            if m:
                return m.group(1) if m.groups() else m.group(0)
    return ""


def bar(site, here, builds):
    """the bar for the page at site/here (a folder, relative to the site)"""
    up = "../" * here.count("/")
    rows = []
    for base, name, ver in builds:
        links = []
        for page, label in PAGES:
            if (site / base / page / "index.html").exists():
                if base + page == here:
                    links.append(f'<b>{label}</b>')
                else:
                    links.append(f'<a href="{up}{base}{page}" style="color:inherit">{label}</a>')
        if links:
            v = f' <span style="opacity:.6">{html.escape(ver)}</span>' if ver else ""
            rows.append(f'<span style="white-space:nowrap">{html.escape(name)}{v}: {" · ".join(links)}</span>')
    return (f'{MARK}<nav style="font:12px/1.7 -apple-system,BlinkMacSystemFont,\'Helvetica Neue\',Arial,sans-serif;'
            f'padding:6px 12px;border-bottom:1px solid rgba(128,128,128,.35);display:flex;flex-wrap:wrap;gap:4px 18px;'
            f'opacity:.85">{"".join(rows)}</nav>')


def main(site):
    site = Path(site)
    builds = [(b, n, version(site, b)) for b, n in BUILDS if (site / b / "webapp").is_dir()]
    done = 0
    for base, _, _ in builds:
        for page, _ in PAGES:
            f = site / base / page / "index.html"
            if not f.exists():
                continue
            text = f.read_text(encoding="utf-8")
            text = re.sub(re.escape(MARK) + r"<nav.*?</nav>", "", text, flags=re.S)   # (run twice: one bar)
            m = re.search(r"<body[^>]*>", text)
            if not m:
                continue
            text = text[:m.end()] + "\n" + bar(site, base + page, builds) + text[m.end():]
            f.write_text(text, encoding="utf-8")
            done += 1
    print(f"site nav: {done} pages, builds: " + ", ".join(f"{n} {v}".strip() for _, n, v in builds))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
