#!/usr/bin/env python3
"""Extract translatable strings from chiaki-ng GUI sources into .ts files.

Replicates lupdate's context assignment:
  - QML files: context = file basename (Main.qml -> Main)
  - C++ files: context = enclosing class name
Usage:
  python scripts/i18n_extract.py
Outputs gui/translations/chiaki_en.ts and chiaki_zh_CN.ts (translation placeholders).
"""
import re, sys, os, xml.etree.ElementTree as ET

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
GUI = os.path.join(ROOT, "gui")
OUT = os.path.join(GUI, "translations")

QML_DIRS = ["src/qml", "src/qml/controls"]
CPP_DIRS = ["src", "include"]

QSTR = re.compile(r'qsTr\("((?:[^"\\]|\\.)*)"')
TR = re.compile(r'(?<!\w)tr\("((?:[^"\\]|\\.)*)"')
CLASS_RE = re.compile(r'\bclass\s+(\w+)')

def unescape(s):
    return s.replace('\\"', '"').replace("\\\\", "\\").replace("\\n", "\n").replace("\\t", "\t")

def scan_qml():
    messages = {}
    for d in QML_DIRS:
        base = os.path.join(GUI, d)
        for fn in sorted(os.listdir(base)):
            if not fn.endswith(".qml"):
                continue
            ctx = os.path.splitext(fn)[0]
            path = os.path.join(base, fn)
            with open(path, encoding="utf-8") as f:
                src = f.read()
            for m in QSTR.finditer(src):
                messages.setdefault(ctx, set()).add(unescape(m.group(1)))
    return messages

def scan_cpp():
    messages = {}
    for d in CPP_DIRS:
        base = os.path.join(GUI, d)
        if not os.path.isdir(base):
            continue
        for fn in sorted(os.listdir(base)):
            if not (fn.endswith(".cpp") or fn.endswith(".h")):
                continue
            path = os.path.join(base, fn)
            with open(path, encoding="utf-8") as f:
                lines = f.readlines()
            cur_class = ""
            for line in lines:
                cls = CLASS_RE.search(line)
                if cls and ("Q_OBJECT" in "\n".join(lines) or "public" in line):
                    cur_class = cls.group(1)
                for m in TR.finditer(line):
                    msg = unescape(m.group(1))
                    ctx = cur_class or os.path.splitext(fn)[0]
                    messages.setdefault(ctx, set()).add(msg)
    return messages

def write_ts(path, language, messages, source_lang="en"):
    root = ET.Element("TS", {"version": "2.1", "language": language, "sourcelanguage": source_lang})
    for ctx in sorted(messages):
        c = ET.SubElement(root, "context")
        ET.SubElement(c, "name").text = ctx
        for msg in sorted(messages[ctx]):
            m = ET.SubElement(c, "message")
            ET.SubElement(m, "source").text = msg
            ET.SubElement(m, "translation").text = msg if language == source_lang else ""
    tree = ET.ElementTree(root)
    ET.indent(tree, space="\t")
    tree.write(path, encoding="utf-8", xml_declaration=True)

def main():
    os.makedirs(OUT, exist_ok=True)
    messages = scan_qml()
    messages.update(scan_cpp())
    write_ts(os.path.join(OUT, "chiaki_en.ts"), "en", messages, source_lang="en")
    write_ts(os.path.join(OUT, "chiaki_zh_CN.ts"), "zh_CN", messages, source_lang="en")
    total = sum(len(v) for v in messages.values())
    print(f"Extracted {total} messages across {len(messages)} contexts -> {OUT}")

if __name__ == "__main__":
    main()
