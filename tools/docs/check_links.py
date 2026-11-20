#!/usr/bin/env python3
"""Checks that every relative link and image path in the documentation exists.

Usage: tools/docs/check_links.py [--all] [file.md ...]

Without arguments it checks doc/**/*.md (tracked, or new and not ignored);
--all adds every other such Markdown file outside thirdparty/. Looked at: inline links and images
[text](target) and ![alt](target), reference definitions [id]: target, and
HTML src="..." / href="..." attributes. Skipped: URLs with a scheme
(https:, mailto: ...), pure #anchors, and anything inside code spans or fenced
code blocks. A target is resolved against the file's directory (a leading /
means the repository root), after dropping #anchor and ?query and
percent-decoding. It must exist in the work tree; a target that exists but is
ignored by git (so not in a clone) is reported too.

Exit status 0 when everything resolves, 1 otherwise (one line per problem:
file:line: target).
"""
import os
import re
import subprocess
import sys
import urllib.parse

ROOT = subprocess.run(['git', 'rev-parse', '--show-toplevel'], capture_output=True,
                      text=True, check=True).stdout.strip()

INLINE = re.compile(r'!?\[(?:[^\[\]]|\[[^\[\]]*\])*\]\(\s*(<[^>]*>|[^()\s]*(?:\([^()\s]*\)[^()\s]*)*)(?:\s+"[^"]*"|\s+\'[^\']*\')?\s*\)')
REFDEF = re.compile(r'^\s{0,3}\[[^\]]+\]:\s*(<[^>]*>|\S+)')
HTML = re.compile(r'\b(?:src|href)\s*=\s*"([^"]*)"')
SCHEME = re.compile(r'^[A-Za-z][A-Za-z0-9+.-]*:')
FENCE = re.compile(r'^\s{0,3}(`{3,}|~{3,})')


def strip_code_spans(line):
    # Removes `...` spans (any run length), keeping the line's length irrelevant.
    out, i = [], 0
    while i < len(line):
        if line[i] == '`':
            j = i
            while j < len(line) and line[j] == '`':
                j += 1
            ticks = line[i:j]
            k = line.find(ticks, j)
            if k < 0:
                out.append(line[i:])
                break
            i = k + len(ticks)
        else:
            out.append(line[i])
            i += 1
    return ''.join(out)


def targets(path):
    fence = None
    with open(path, encoding='utf-8') as f:
        for n, line in enumerate(f, 1):
            m = FENCE.match(line)
            if m:
                mark = m.group(1)
                if fence is None:
                    fence = mark[0] * len(mark)
                elif mark.startswith(fence):
                    fence = None
                continue
            if fence is not None:
                continue
            text = strip_code_spans(line)
            for m in INLINE.finditer(text):
                yield n, m.group(1)
            m = REFDEF.match(text)
            if m:
                yield n, m.group(1)
            for m in HTML.finditer(text):
                yield n, m.group(1)


def resolve(md, target):
    t = target.strip()
    if t.startswith('<') and t.endswith('>'):
        t = t[1:-1]
    if not t or t.startswith('#') or SCHEME.match(t):
        return None
    t = t.split('#', 1)[0].split('?', 1)[0]
    if not t:
        return None
    t = urllib.parse.unquote(t)
    if t.startswith('/'):
        return os.path.normpath(os.path.join(ROOT, t.lstrip('/')))
    return os.path.normpath(os.path.join(os.path.dirname(md), t))


def ignored(paths):
    if not paths:
        return set()
    rel = [os.path.relpath(p, ROOT) for p in paths]
    r = subprocess.run(['git', '-C', ROOT, 'check-ignore', '--stdin'], input='\n'.join(rel),
                       capture_output=True, text=True)
    return {os.path.join(ROOT, p) for p in r.stdout.split('\n') if p}


def tracked_markdown(all_files):
    # Tracked files and new ones not yet added (but not ignored ones).
    r = subprocess.run(['git', '-C', ROOT, 'ls-files', '--cached', '--others', '--exclude-standard',
                        '*.md'], capture_output=True, text=True, check=True)
    files = []
    for p in r.stdout.split('\n'):
        if not p or p.startswith('thirdparty/'):
            continue
        if all_files or p.startswith('doc/'):
            files.append(os.path.join(ROOT, p))
    return files


def main(argv):
    all_files = '--all' in argv
    args = [a for a in argv if a != '--all']
    files = [os.path.abspath(a) for a in args] if args else tracked_markdown(all_files)
    problems, found = [], []
    checked = 0
    for md in files:
        for n, target in targets(md):
            p = resolve(md, target)
            if p is None:
                continue
            checked += 1
            if os.path.exists(p):
                found.append((md, n, target, p))
            else:
                problems.append((md, n, target, 'missing'))
    bad = ignored([p for _, _, _, p in found])
    for md, n, target, p in found:
        if p in bad:
            problems.append((md, n, target, 'exists here but is ignored by git'))
    for md, n, target, why in sorted(problems):
        print(f'{os.path.relpath(md, ROOT)}:{n}: {target} ({why})')
    print(f'{len(files)} files, {checked} relative links, {len(problems)} problems',
          file=sys.stderr)
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
