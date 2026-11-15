#!/usr/bin/env python3
"""
Build src/greeter/catalog.h, the App Store's list of apps.

Every app is a single, statically linked x86-64 program published by its own project. For each
one this downloads the release archive, checks the program is inside it, and records the size and
SHA-256 of the archive. SilverOS refuses any download that does not match, so a changed or
tampered file can never be installed.

    scripts/make-app-catalog.py            (needs network access; writes src/greeter/catalog.h)
"""
import hashlib, io, os, sys, tarfile, urllib.request, zipfile, bz2, gzip

APPS = [
    # id, name, kind, tagline, description, url, path of the program in the archive, command
    ("btop", "btop", "System", "A beautiful system monitor",
     "Processes, CPU, memory, disks and network, live, with graphs. Arrow keys to move, q to quit.",
     "https://github.com/aristocratos/btop/releases/download/v1.4.5/btop-x86_64-linux-musl.tbz", "./btop/bin/btop", "btop"),
    ("bottom", "bottom", "System", "Another live system monitor",
     "Graphs for CPU, memory, network and temperature, and a process list you can sort and search. q quits.",
     "https://github.com/ClementTsang/bottom/releases/download/0.10.2/bottom_x86_64-unknown-linux-musl.tar.gz", "btm", "btm"),
    ("micro", "micro", "Editor", "A text editor that feels normal",
     "Ctrl+S saves, Ctrl+Q quits, Ctrl+C and Ctrl+V copy and paste, mouse works. Good for notes and code.",
     "https://github.com/zyedidia/micro/releases/download/v2.0.14/micro-2.0.14-linux64-static.tar.gz", "micro-2.0.14/micro", "micro"),
    ("yazi", "Yazi", "Files", "A fast file manager for the terminal",
     "Browse folders with previews, copy, move and rename files. Arrow keys or h j k l, q quits.",
     "https://github.com/sxyazi/yazi/releases/download/v25.5.31/yazi-x86_64-unknown-linux-musl.zip", "yazi-x86_64-unknown-linux-musl/yazi", "yazi"),
    ("gdu", "gdu", "Files", "See what is filling your disk",
     "Scans a folder and shows what takes the most space, biggest first. Enter opens a folder, q quits.",
     "https://github.com/dundee/gdu/releases/download/v5.31.0/gdu_linux_amd64_static.tgz", "gdu_linux_amd64_static", "gdu"),
    ("glow", "Glow", "Reading", "Read Markdown, nicely",
     "Opens .md files with headings, lists and code laid out for reading. Shows the files in this folder.",
     "https://github.com/charmbracelet/glow/releases/download/v2.1.1/glow_2.1.1_Linux_x86_64.tar.gz", "glow_2.1.1_Linux_x86_64/glow", "glow"),
    ("fzf", "fzf", "Tools", "Find anything by typing a few letters",
     "Lists every file below this folder; type to narrow it down. Esc quits.",
     "https://github.com/junegunn/fzf/releases/download/v0.62.0/fzf-0.62.0-linux_amd64.tar.gz", "fzf", "fzf"),
    ("ripgrep", "ripgrep", "Tools", "Search inside files, very fast",
     "Command-line search: rg word finds every line containing it in this folder and below.",
     "https://github.com/BurntSushi/ripgrep/releases/download/14.1.1/ripgrep-14.1.1-x86_64-unknown-linux-musl.tar.gz", "ripgrep-14.1.1-x86_64-unknown-linux-musl/rg", "rg"),
    ("fd", "fd", "Tools", "Find files by name",
     "Command-line finder: fd report lists every file and folder whose name contains report.",
     "https://github.com/sharkdp/fd/releases/download/v10.2.0/fd-v10.2.0-x86_64-unknown-linux-musl.tar.gz", "fd-v10.2.0-x86_64-unknown-linux-musl/fd", "fd"),
    ("bat", "bat", "Reading", "Show files with colours and line numbers",
     "Like cat, but with syntax highlighting. bat notes.txt shows a file; q quits the pager.",
     "https://github.com/sharkdp/bat/releases/download/v0.25.0/bat-v0.25.0-x86_64-unknown-linux-musl.tar.gz", "bat-v0.25.0-x86_64-unknown-linux-musl/bat", "bat"),
    ("eza", "eza", "Files", "A friendlier ls",
     "Lists files with colours, icons for folders and sizes you can read. eza -l for details.",
     "https://github.com/eza-community/eza/releases/download/v0.21.4/eza_x86_64-unknown-linux-musl.tar.gz", "./eza", "eza"),
]

def member(data, kind, path):
    if kind == "zip":
        return zipfile.ZipFile(io.BytesIO(data)).read(path)
    raw = bz2.decompress(data) if kind == "tbz" else gzip.decompress(data)
    t = tarfile.open(fileobj=io.BytesIO(raw))
    return t.extractfile(path).read()

def c(s): return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'

out = []
for app_id, name, kind_label, tag, desc, url, path, cmd in APPS:
    kind = "zip" if url.endswith(".zip") else "tbz" if url.endswith(".tbz") else "tgz"
    req = urllib.request.Request(url, headers={"User-Agent": "SilverOS catalog builder"})
    data = urllib.request.urlopen(req, timeout=120).read()
    prog = member(data, kind, path)
    assert prog[:4] == b"\x7fELF", f"{app_id}: {path} is not a program"
    sha = hashlib.sha256(data).hexdigest()
    print(f"{app_id:8} {len(data):9} bytes  {sha[:16]}…  program {len(prog)} bytes", file=sys.stderr)
    out.append(f"\t{{{c(app_id)}, {c(name)}, {c(kind_label)}, {c(tag)}, {c(desc)},\n\t {c(url)},\n\t {c(sha)}, {len(data)}, {c(kind)}, {c(path)}, {c(cmd)}}},")

hdr = os.path.join(os.path.dirname(__file__), "..", "src", "greeter", "catalog.h")
with open(hdr, "w") as f:
    f.write("/* The App Store catalog. Generated by scripts/make-app-catalog.py: do not edit by hand. */\n#pragma once\n")
    f.write("typedef struct { const char *id, *name, *kind, *tag, *desc, *url, *sha256; long size; const char *archive, *path, *cmd; } CatApp;\n")
    f.write("static const CatApp CATALOG[] = {\n" + "\n".join(out) + "\n};\n#define NCAT ((int)(sizeof CATALOG / sizeof CATALOG[0]))\n")
print("wrote", hdr, file=sys.stderr)
