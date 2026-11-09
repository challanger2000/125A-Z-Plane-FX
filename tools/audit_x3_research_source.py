#!/usr/bin/env python3
"""Read-only, source-scoped external X3 research DSP risk inventory.

This is NOT a proof of no malloc, exceptions, locks or unbounded work.
The reviewed source arrives by exact SHA-256-pinned original archive; no
third-party sources are copied to the distributable VST3 or evidence ZIP.
"""
import hashlib
import json
import pathlib
import re
import sys

def sha256(blob: bytes) -> str:
    return hashlib.sha256(blob).hexdigest()

def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: audit_x3_research_source.py <extracted-root> <output-json>")
    root = pathlib.Path(sys.argv[1]).resolve()
    destination = pathlib.Path(sys.argv[2]).resolve()
    extensions = {".h", ".hpp", ".hxx", ".c", ".cc", ".cpp", ".cxx", ".inl"}
    paths = sorted(p for p in root.rglob("*") if p.is_file() and p.suffix.lower() in extensions)
    if not paths or not any(p.name == "emux_extended.hpp" for p in paths):
        raise SystemExit("FAIL: expected exact external X3 source header missing")

    # Conservative broad inventory: do NOT assume every hit is reachable
    # from realtime code. Off-thread table-loading/parser hits are distinct.
    suspicious = re.compile(
        r"\b(?:new|delete|malloc|calloc|realloc|free|HeapAlloc|HeapFree|"
        r"VirtualAlloc|VirtualFree|fopen|fclose|fread|fwrite|"
        r"throw|try|catch|mutex|lock_guard|unique_lock|scoped_lock|"
        r"condition_variable|Sleep|CreateFile|ifstream|ofstream|"
        r"filesystem|vector|deque|list|map|unordered_map|"
        r"make_unique|make_shared|push_back|emplace_back|resize|reserve)\b",
        re.IGNORECASE,
    )
    methods = re.compile(
        r"\b(?:class\s+Filter|struct\s+Filter|set_coefficients|process_sample|"
        r"morph_coefficients|designer_coefficients|coefficients\s*\(|"
        r"reset\s*\(|from_table_file|from_table_files)\b",
    )
    result = {
        "status": "SOURCE_INVENTORY_ONLY_NOT_FULL_RT_AUDIT",
        "source_root_file_names": [p.name for p in paths],
        "complete_source_file_count": len(paths),
        "files": [],
        "risk_candidates": [],
        "realtime_method_contexts": [],
        "coverage_limits": [
            "Reachability of every candidate requires a human control-flow review",
            "C++ operator new was checked by a separate 1320-case native executable",
            "Direct malloc, Win32 HeapAlloc, and transitive CRT internals need independent evidence",
            "Exception paths, disk faults, concurrency, WCET and actual ASIO are not certified",
            "Sources are NOT redistributed by this inventory",
        ],
    }
    for path in paths:
        data = path.read_bytes()
        lines = data.decode("utf-8", errors="replace").splitlines()
        rel = str(path.relative_to(root)).replace("\\", "/")
        result["files"].append({"path": rel, "sha256": sha256(data), "lines": len(lines)})
        for i, line in enumerate(lines):
            stripped = line.strip()
            if suspicious.search(stripped):
                result["risk_candidates"].append({
                    "path": rel, "line": i + 1, "text": stripped[:220],
                    "method_proximity": any(methods.search(lines[k]) for k in
                                            range(max(0, i-6), min(len(lines), i+2))),
                })
            if methods.search(stripped) and not stripped.startswith("//"):
                result["realtime_method_contexts"].append({
                    "path": rel, "line": i + 1,
                    "text": "\n".join(
                        f"{j+1}: {lines[j].rstrip()}" for j in
                        range(max(0, i-1), min(len(lines), i+16))
                    ),
                })
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"X3 SOURCE INVENTORY: {len(paths)} source files, "
          f"{len(result['risk_candidates'])} lexical risk candidates, "
          f"{len(result['realtime_method_contexts'])} method/context anchors")
    for key in ("set_coefficients", "process_sample", "morph_coefficients",
                "designer_coefficients", "coefficients"):
        hits = [r for r in result["realtime_method_contexts"]
                if key in r["text"].splitlines()[1] if len(r["text"].splitlines()) > 1]
        print(f"METHOD {key}: {len(hits)} contexts")
        for item in hits[:4]:
            print(f"  {item['path']}:{item['line']}\n{item['text'][:2100]}")
    print(f"REPORT {destination}")
    print("STATUS: EVIDENCE ONLY; NOT A LICENSE, ORIGINAL HARDWARE OR ASIO AUDIT")
    return 0

if __name__ == "__main__":
    sys.exit(main())
