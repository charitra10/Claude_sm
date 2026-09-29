#!/usr/bin/env python3
"""refresh_doc_lines.py [DOC] [CODE]: update `main.cpp:NNNN` references in what_the_code_does.md to the current line of the
function named just before each one (`name()` in backticks within 200 characters). References inside decide_core() are
anchored by the code patterns in ANCHORS. Prints what it could not map."""
import re, sys
doc_path = sys.argv[1] if len(sys.argv) > 1 else 'what_the_code_does.md'
code_path = sys.argv[2] if len(sys.argv) > 2 else 'v5.9/main.cpp'
code = open(code_path).read().split('\n')
defs = {}
for i, l in enumerate(code, 1):
    m = re.match(r'^    (?:[\w:<>,&\s\*]+?)\b(\w+)\((.*)\)\s*(?:const\s*)?\{\s*$', l)
    if m and m.group(1) not in ('if', 'for', 'while', 'switch'): defs.setdefault(m.group(1), i)
    m2 = re.match(r'^(?:struct|class) (\w+)', l)
    if m2: defs.setdefault(m2.group(1), i)
    if l.startswith('int main('): defs['main'] = i
def find(pat):
    for i, l in enumerate(code, 1):
        if pat in l: return i
    return None
ANCHORS = {  # doc wording just before the reference -> code pattern (single lines)
    'Chambers: camping and eviction': 'if (F_PORTAL_EVICT && !s.evacuating && round > s.born) {',
    'Delivery': '// Feeder delivery: suicide',
    'References such as `': 'Action decide_core() {',
}
RANGES = {  # doc wording just before the reference -> (first-line pattern, last-line pattern)
    '## 21. The movement scorer': ('double best = -1e30;', 'auto sacrifice = [&]'),
    '## 22. Last-resort fallbacks': ('auto sacrifice = [&]', 'return {{c.get_dir()}, 0, Mode::Trapped, true};'),
    '## 20. `decide()`: the full priority cascade': ('Action decide_core() {', 'int beams_into_child(int child) const {'),
    'switch (`': ('constexpr bool F_HAZARD = true;', 'constexpr bool EXIT_CLEAR_ON'),
    '## 6. Geometry helpers': ('int index(Position p) const', 'bool empty(Position p) const {'),
}
doc = open(doc_path).read()
out, pos, unmapped = [], 0, []
for m in re.finditer(r'main\.cpp:(\d+)(?:-(\d+))?', doc):
    before = doc[max(0, m.start() - 200):m.start()]
    new = None; rng = None
    if m.group(2):
        for k, (a, b) in RANGES.items():
            if k in before[-150:]: rng = (find(a), find(b))
    for k, pat in ANCHORS.items():
        if k in before[-120:]: new = find(pat)
    if new is None and rng is None:
        names = re.findall(r'`(\w+)\(', before[-120:]) or re.findall(r'`(\w+)`', before[-120:])
        names = [n for n in names if n in defs]
        if names: new = defs[names[-1]]
    rep = m.group(0)
    if rng and rng[0] and rng[1]:
        rep = f'main.cpp:{rng[0]}-{rng[1] - 1}'
    elif new is not None and not m.group(2):
        rep = f'main.cpp:{new}'
    else:
        unmapped.append((m.group(0), before[-60:].replace('\n', ' ')))
    out.append(doc[pos:m.start()] + rep); pos = m.end()
out.append(doc[pos:])
open(doc_path, 'w').write(''.join(out))
for u in unmapped: print('unmapped:', u)
