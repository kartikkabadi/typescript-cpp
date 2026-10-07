#!/usr/bin/env python3
"""Reorder C++ designated initializers to match struct declaration order.

Batch-D's Go→C++ transpiler emitted `{.B = ..., .A = ...}` in Go field
order, which is ill-formed C++ (GCC errors, clang warns). For each
`TypeName{ ... }` where TypeName has a known field order, recursively fix
inner inits first, then stable-sort the top-level designator list.
"""
import json
import re
import sys

ORDERS = {
    "Diagnostic": ["Range", "Severity", "Code", "CodeDescription", "Source",
                   "Message", "Tags", "RelatedInformation", "Data"],
    "TextEdit": ["Range", "NewText"],
    "CompletionItem": ["Label", "LabelDetails", "Kind", "Tags", "Detail",
                       "Documentation", "Deprecated", "Preselect", "SortText",
                       "FilterText", "InsertText", "InsertTextFormat",
                       "InsertTextMode", "TextEdit", "TextEditText",
                       "AdditionalTextEdits", "CommitCharacters", "Command",
                       "Data"],
    "IntegerOrString": ["Integer", "String"],
    "VerifyWorkspaceSymbolCase": ["Pattern", "Includes", "Exact",
                                  "Preferences"],
    "CompletionsExpectedItems": ["Includes", "Excludes", "Exact", "Unsorted"],
    "CompletionsExpectedList": ["IsIncomplete", "ItemDefaults", "Items",
                                "UserPreferences"],
}

# Auto-discovered struct field orders (hardcoded map above wins on conflicts
# like core `Diagnostic` vs `lsproto::Diagnostic`).
for k, v in json.load(open("/tmp/struct_orders.json")).items():
    ORDERS.setdefault(k, v)

TYPES_RE = re.compile(r"\b(" + "|".join(ORDERS) + r")\{")

fixes = 0


def find_matching_brace(text, open_idx):
    depth = 0
    i = open_idx
    n = len(text)
    while i < n:
        c = text[i]
        if c == "R" and text.startswith('"', i + 1):
            # C++ raw string R"delim(...)delim"
            rm = re.match(r'R"([A-Za-z]{0,16})\(', text[i:])
            if rm:
                i += len(rm.group(0))
                endmark = ")" + rm.group(1) + '"'
                j = text.find(endmark, i)
                i = j + len(endmark) if j >= 0 else n
                continue
            i += 1
            continue
        if c in "\"'":
            q = c
            i += 1
            while i < n and text[i] != q:
                if text[i] == "\\":
                    i += 1
                i += 1
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def split_top_commas(body):
    parts = []
    depth = 0
    start = 0
    i = 0
    n = len(body)
    while i < n:
        c = body[i]
        if c == "R" and i + 1 < n and body[i + 1] == '"':
            rm = re.match(r'R"([A-Za-z]{0,16})\(', body[i:])
            if rm:
                i += len(rm.group(0))
                endmark = ")" + rm.group(1) + '"'
                j = body.find(endmark, i)
                i = j + len(endmark) if j >= 0 else n
                continue
            i += 1
            continue
        if c in "\"'":
            q = c
            i += 1
            while i < n and body[i] != q:
                if body[i] == "\\":
                    i += 1
                i += 1
        elif c in "{([":
            depth += 1
        elif c in "})]":
            depth -= 1
        elif c == "," and depth == 0:
            parts.append(body[start:i])
            start = i + 1
        i += 1
    parts.append(body[start:])
    return parts


def fix_region(text, start, end):
    """Recursively fix Type{...} initializers within text[start:end].

    Returns the rewritten region. Operates left to right; for each match,
    the body is fixed first (recursion), then its designators sorted."""
    global fixes
    out = []
    pos = start
    while True:
        m = TYPES_RE.search(text, pos, end)
        if not m:
            out.append(text[pos:end])
            break
        ty = m.group(1)
        open_idx = m.end() - 1
        close_idx = find_matching_brace(text, open_idx)
        if close_idx < 0 or close_idx >= end:
            # unbalanced or extends past region — emit as-is, move on
            out.append(text[pos:m.end()])
            pos = m.end()
            continue
        out.append(text[pos:open_idx + 1])  # up to and incl '{'
        body = fix_region(text, open_idx + 1, close_idx)  # inner first
        # now sort this level's designators
        order = ORDERS[ty]
        parts = split_top_commas(body)
        keyed = []
        ok = "=" in body
        for p in parts:
            fm = re.match(r"\s*\.(\w+)\s*=", p)
            if fm:
                keyed.append((order.index(fm.group(1))
                              if fm.group(1) in order else len(order), p))
            elif p.strip() == "":
                keyed.append((len(order) + 1, p))  # trailing-comma tail stays last
            else:
                ok = False
                break
        if ok and keyed:
            new_body = ",".join(
                t[1][1] for t in sorted(enumerate(keyed),
                                        key=lambda t: (t[1][0], t[0])))
            if new_body != body:
                fixes += 1
                body = new_body
        out.append(body)
        pos = close_idx  # '}' itself is re-emitted via text[pos:...]
    return "".join(out)


for path in sys.argv[1:]:
    src = open(path, newline='').read()
    new = fix_region(src, 0, len(src))
    if new != src:
        open(path, 'w', newline='').write(new)
    print(f"{path}: {fixes} initializer(s) reordered")
    fixes = 0
