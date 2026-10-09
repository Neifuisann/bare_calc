"""Audit fixed calculator row strings against the actual chrome glyph metrics.

Includes all row builders, catalog data, statistics catalog/regression labels,
settings arrays, constant groups, and generated conversion pairs. Uses the
narrower submenu budget (badge + arrow) for every fixed label.
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
source = root / 'src' / 'calculator'
menu = (source / 'ui/calculator_menu.cpp').read_text()
alias_body = menu.split('static const Alias aliases[]={', 1)[1].split('};', 1)[0]
aliases = dict(re.findall(r'\{"([^"]+)","([^"]+)"\}', alias_body))
glyphs = {}
for match in re.finditer(r'\{\d+, (\d+), \d+, (\d+), (-?\d+), -?\d+\},? // 0x([0-9A-F]+)',
                         (root / 'src' / 'ui' / 'font_classwiz15.h').read_text()):
    width, advance, offset, code = match.groups()
    glyphs[chr(int(code, 16))] = (int(width), int(advance), int(offset))

def width(text):
    cursor = right = 0
    for char in text:
        w, advance, offset = glyphs[char]
        right = max(right, cursor + offset + w)
        cursor += advance
    return right

def strings(body):
    body = re.sub(r'//[^\n]*|/\*.*?\*/', '', body, flags=re.S)
    return re.findall(r'"([^"\n]*)"', body)

def body_at(text, offset):
    start = text.index('{', offset)
    depth = 0
    for token in re.finditer(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*.*?\*/|[{}]', text[start:], re.S):
        if token[0] == '{': depth += 1
        elif token[0] == '}':
            depth -= 1
            if not depth: return text[start:start + token.end()]
    raise ValueError('unclosed function')

labels = set()
for name in ('menu', 'statistics', 'distribution', 'table', 'ratio', 'matrix', 'equation'):
    group = 'ui' if name == 'menu' else 'apps'
    text = (source / group / f'calculator_{name}.cpp').read_text()
    for match in re.finditer(r'(?:std::vector<MenuItem>\s+\w+\([^;{}]*\)|'
                             r'const std::vector<CatalogGroup>& catalogData\(\)|'
                             r'std::vector<CatalogEntry> catalogEntries\(\)|'
                             r'const char\* regressionEquation\([^;{}]*\))\s*\{', text):
        labels.update(strings(body_at(text, match.start())))
for group, label in re.findall(r'\{"([^"]+)","([^"]+)"', (source / 'assets/calculator_constants.h').read_text()):
    labels.update((group, label))
for group, a, b in re.findall(r'CONVERSION_PAIR\("([^"]+)","([^"]+)","([^"]+)"', (source / 'math/calculator_conversions.h').read_text()):
    labels.update((group, a + ' -> ' + b, b + ' -> ' + a))

errors = []
changed = []
for label in sorted(labels):
    display = aliases.get(label, label)
    if display != label: changed.append((label, display))
    if any(c not in glyphs for c in display):
        errors.append((label, 'unsupported glyph'))
    elif width(display) > 161 or '...' in display:
        errors.append((label, f'{display!r}: {width(display)}px > 161px'))
for old, new in changed: print(f'{old} -> {new}')
for label, reason in errors: print(f'FAIL {label}: {reason}')
print(f'{len(labels)} fixed strings checked; {len(changed)} shortened; {len(errors)} failures')
raise SystemExit(bool(errors))
