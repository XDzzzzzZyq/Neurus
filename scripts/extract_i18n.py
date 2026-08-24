#!/usr/bin/env python3
"""
extract_i18n.py — Neurus translation extraction / merge / check tool.

Blender-style gettext workflow: scans the UI layer for I18n translation
calls and merges the discovered (context, msgid) keys into the PO catalogs
in res/i18n/. New strings appear automatically, removed strings are marked
obsolete (#~), and existing translations are preserved. Mirrors Blender's
`update_pot.py` + `msgmerge` pipeline in a single dependency-free script.

Usage:
  python3 scripts/extract_i18n.py                 # update all catalogs
  python3 scripts/extract_i18n.py --check         # fail if anything untranslated
  python3 scripts/extract_i18n.py --min-coverage 95   # fail below threshold
  python3 scripts/extract_i18n.py --verbose       # print every change
"""

import argparse
import os
import re
import sys

# ---------------------------------------------------------------------------
# Locations
# ---------------------------------------------------------------------------

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(SCRIPT_DIR)
UI_DIR = os.path.join(ROOT, "src", "ui")
I18N_DIR = os.path.join(ROOT, "res", "i18n")

# ---------------------------------------------------------------------------
# Key discovery
# ---------------------------------------------------------------------------

# translate("key")                    -> context ""
# translateCtx("key", "ctx")          -> context "ctx"
_TRANSLATE_RE = re.compile(
    r'(?<![A-Za-z0-9_])(?:translate|translateCtx)\(\s*'
    r'"((?:[^"\\]|\\.)*)"(?:\s*,\s*"((?:[^"\\]|\\.)*)")?\s*\)'
)

# N_("key") — no-op marker (gettext N_ convention) for keys translated at
# runtime through I18n but not written as translate("...") literals
# (e.g. menu keys forwarded to menu-builder helpers).
_N_KEY_RE = re.compile(r'N_\(\s*"((?:[^"\\]|\\.)*)"\s*\)')

# Dock titles are stored as translation keys in UIPanel::DefaultNameKey:
#   case PanelType::Viewport: return "Viewport";
_DOCK_KEY_RE = re.compile(r'case\s+PanelType::\w+:\s*return\s*"([^"]+)";')

# Runtime keys that never appear as translate() literals in the UI layer:
# scene-layer data strings resolved through I18n at runtime
# (e.g. Light::ParseLightName type names).
EXTRA_KEYS = [
    ("", "Point Light"),
    ("", "Sun Light"),
    ("", "Spot Light"),
    ("", "Area Light"),
    ("", "None"),
]


def decode_c_literal(text: str) -> str:
    """Decodes C/C++ escape sequences inside a source string literal."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        if ch != "\\" or i + 1 >= n:
            out.append(ch)
            i += 1
            continue
        esc = text[i + 1]
        simple = {"n": "\n", "t": "\t", "r": "\r", "\\": "\\",
                  '"': '"', "'": "'", "0": "\0", "a": "\a", "b": "\b",
                  "f": "\f", "v": "\v"}
        if esc in simple:
            out.append(simple[esc])
            i += 2
        elif esc == "u" and i + 5 < n:
            out.append(chr(int(text[i + 2:i + 6], 16)))
            i += 6
        elif esc == "x" and i + 3 < n:
            j = i + 2
            while j < n and text[j] in "0123456789abcdefABCDEF":
                j += 1
            out.append(chr(int(text[i + 2:j], 16)))
            i = j
        else:
            out.append(esc)
            i += 2
    return "".join(out)


def encode_c_literal(text: str) -> str:
    """Encodes a string for output inside a PO msgid/msgstr quoted line."""
    out = []
    for ch in text:
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\t":
            out.append("\\t")
        elif ch == "\r":
            out.append("\\r")
        elif ord(ch) < 32:
            out.append("\\x%02x" % ord(ch))
        else:
            out.append(ch)
    return "".join(out)


def find_translation_keys() -> list:
    """Returns the sorted list of (context, msgid) keys used by the code."""
    keys = set()
    for dirpath, _dirs, files in os.walk(UI_DIR):
        for name in files:
            if not name.endswith((".h", ".cpp")):
                continue
            with open(os.path.join(dirpath, name), encoding="utf-8") as f:
                src = f.read()
            for m in _TRANSLATE_RE.finditer(src):
                msgid = decode_c_literal(m.group(1))
                ctx = decode_c_literal(m.group(2)) if m.group(2) else ""
                if msgid:
                    keys.add((ctx, msgid))
            for m in _N_KEY_RE.finditer(src):
                msgid = decode_c_literal(m.group(1))
                if msgid:
                    keys.add(("", msgid))
            # Dock-title keys live in UIPanel.h (PanelName translates them).
            if name == "UIPanel.h":
                for m in _DOCK_KEY_RE.finditer(src):
                    keys.add(("Dock", decode_c_literal(m.group(1))))
    keys.update(EXTRA_KEYS)
    return sorted(keys, key=lambda kv: (kv[0], kv[1]))


# ---------------------------------------------------------------------------
# PO catalog parsing / writing
# ---------------------------------------------------------------------------

class POEntry:
    __slots__ = ("context", "msgid", "msgstr", "obsolete")

    def __init__(self, context="", msgid="", msgstr="", obsolete=False):
        self.context = context
        self.msgid = msgid
        self.msgstr = msgstr
        self.obsolete = obsolete

    def key(self):
        return (self.context, self.msgid)


def _parse_quoted(line, keyword):
    """Parses 'msgid "..."' / 'msgstr "..."' continuation lines into text."""
    parts = []
    for m in re.finditer(r'"((?:[^"\\]|\\.)*)"', line):
        parts.append(decode_c_literal(m.group(1)))
    return "".join(parts)


def parse_po(text: str) -> list:
    """Parses a PO file into a list of POEntry (header entry included)."""
    entries = []
    cur = None
    field = None

    def finalize():
        nonlocal cur
        if cur is not None:
            if cur.msgid or cur.msgstr or cur.context:
                entries.append(cur)
            cur = None

    for raw in text.splitlines():
        line = raw.strip()
        if not line:
            finalize()
            continue
        if line.startswith("#~"):
            # Obsolete entry: (re)start one flagged as obsolete.
            if cur is None:
                cur = POEntry(obsolete=True)
            continue
        if line.startswith("#"):
            continue
        if cur is None:
            cur = POEntry()
        if line.startswith("msgctxt"):
            field = "context"
            cur.context = _parse_quoted(line, "msgctxt")
        elif line.startswith("msgid"):
            field = "msgid"
            cur.msgid = _parse_quoted(line, "msgid")
        elif line.startswith("msgstr"):
            field = "msgstr"
            cur.msgstr = _parse_quoted(line, "msgstr")
        elif line.startswith('"') and field:
            value = _parse_quoted(line, None)
            if field == "context":
                cur.context += value
            elif field == "msgid":
                cur.msgid += value
            elif field == "msgstr":
                cur.msgstr += value
    finalize()
    return entries


def render_po(header: str, entries: list, obsolete: list) -> str:
    """Renders header + active entries + obsolete (#~) entries."""
    lines = [header.rstrip("\n"), ""]

    def emit(entry, prefix=""):
        if entry.context:
            lines.append('%smsgctxt "%s"' % (prefix, encode_c_literal(entry.context)))
        lines.append('%smsgid "%s"' % (prefix, encode_c_literal(entry.msgid)))
        lines.append('%smsgstr "%s"' % (prefix, encode_c_literal(entry.msgstr)))

    for entry in entries:
        emit(entry)
        lines.append("")
    for entry in obsolete:
        emit(entry, "#~ ")
        lines.append("")

    return "\n".join(lines).rstrip("\n") + "\n"


def load_header(text: str) -> str:
    """Extracts the header entry (msgid \"\") block for preservation."""
    entries = parse_po(text)
    for e in entries:
        if e.msgid == "":
            lines = []
            # Rebuild the header block from the parsed context/msgstr.
            lines.append('msgid ""')
            for chunk in _split_multiline(e.msgstr):
                lines.append('msgstr "%s"' % encode_c_literal(chunk))
            return "\n".join(lines) + "\n"
    return 'msgid ""\nmsgstr "Content-Type: text/plain; charset=UTF-8\\n"\n'


def _split_multiline(text: str, width=72) -> list:
    """Splits a long string into quoted continuation chunks (best effort)."""
    if len(text) <= width:
        return [text]
    chunks = []
    for i in range(0, len(text), width):
        chunks.append(text[i:i + width])
    return chunks


# ---------------------------------------------------------------------------
# Catalog update
# ---------------------------------------------------------------------------

def update_catalog(po_path: str, keys: list, verbose: bool) -> dict:
    """Merges code keys into one catalog; returns coverage stats."""
    old_entries = []
    if os.path.exists(po_path):
        with open(po_path, encoding="utf-8") as f:
            old_text = f.read()
        old_entries = parse_po(old_text)
        header = load_header(old_text)
    else:
        header = ('msgid ""\n'
                  'msgstr "Content-Type: text/plain; charset=UTF-8\\n"\n')

    old_by_key = {}
    old_obsolete_by_key = {}
    for e in old_entries:
        if e.msgid == "":
            continue
        if e.obsolete:
            old_obsolete_by_key[e.key()] = e
        else:
            old_by_key[e.key()] = e

    new_entries = []
    missing = 0
    for ctx, msgid in keys:
        # Prefer the active entry; fall back to an obsolete one (a key that
        # was removed and then re-added keeps its old translation).
        prev = old_by_key.get((ctx, msgid)) or old_obsolete_by_key.get((ctx, msgid))
        msgstr = prev.msgstr if prev else ""
        if not msgstr:
            missing += 1
            if verbose and prev is None:
                print("  + new key: [%s] %s" % (ctx or "IFACE", msgid))
        new_entries.append(POEntry(ctx, msgid, msgstr))

    code_keys = {e.key() for e in new_entries}
    obsolete = [e for e in old_entries
                if e.obsolete and e.msgid != "" and e.key() not in code_keys]
    for e in old_entries:
        if not e.obsolete and e.msgid != "" and e.key() not in code_keys:
            obsolete.append(e)
            if verbose:
                print("  ~ obsolete: [%s] %s" % (e.context or "IFACE", e.msgid))

    with open(po_path, "w", encoding="utf-8") as f:
        f.write(render_po(header, new_entries, obsolete))

    total = len(new_entries)
    coverage = 100.0 * (total - missing) / total if total else 100.0
    return {"total": total, "missing": missing, "obsolete": len(obsolete),
            "coverage": coverage}


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(description="Neurus translation extractor")
    parser.add_argument("--check", action="store_true",
                        help="fail (exit 1) if any string is untranslated")
    parser.add_argument("--min-coverage", type=float, default=0.0,
                        help="fail if a language's coverage drops below this %%")
    parser.add_argument("--verbose", action="store_true",
                        help="print every added/obsoleted key")
    args = parser.parse_args()

    keys = find_translation_keys()
    print("[extract_i18n] %d translation keys found in src/ui/" % len(keys))

    if not os.path.isdir(I18N_DIR):
        os.makedirs(I18N_DIR, exist_ok=True)

    po_files = sorted(f for f in os.listdir(I18N_DIR) if f.endswith(".po"))
    if not po_files:
        print("[extract_i18n] no .po catalogs in res/i18n/, nothing to update",
              file=sys.stderr)
        return 1 if args.check else 0

    failed = False
    for name in po_files:
        path = os.path.join(I18N_DIR, name)
        lang = name[:-3]
        stats = update_catalog(path, keys, args.verbose)
        status = "OK"
        if stats["missing"]:
            status = "MISSING %d" % stats["missing"]
            if args.check or args.min_coverage > stats["coverage"]:
                failed = True
        print("[extract_i18n] %-6s %3d/%3d translated (%.1f%%)  %s  "
              "%d obsolete"
              % (lang, stats["total"] - stats["missing"], stats["total"],
                 stats["coverage"], status, stats["obsolete"]))
        if args.min_coverage > stats["coverage"]:
            failed = True

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
