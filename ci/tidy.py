#!/usr/bin/env python3
"""clang-tidy over the program's own sources (src/, tests/) of a build with compile_commands.json.

    python ci/tidy.py <build> [-j N] [--clang-tidy PATH] [--out report.txt] [--only REGEX] [--strict]

The checks are those of .clang-tidy in the root. A warning in a header is printed once, not once per
file that includes it. Prints the warnings and a count per check; the exit code is 1 with --strict when
there are warnings (and always when clang-tidy itself fails on a file).
MinGW: clang-tidy gets --target=x86_64-w64-mingw32 and the compiler's folder first in PATH, so that it
finds the MinGW headers.
"""
import argparse
import collections
import concurrent.futures
import json
import os
import re
import shlex
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIAG = re.compile(r'^(?P<file>.+?):(?P<line>\d+):(?P<col>\d+): (?P<kind>warning|error): (?P<msg>.*?)'
                  r'(?: \[(?P<check>[^\]]+)\])?$')


def norm(path):
    return os.path.normpath(path).replace('\\', '/')


def find_clang_tidy(explicit):
    for candidate in (explicit, os.environ.get('CLANG_TIDY'), shutil.which('clang-tidy')):
        if candidate and os.path.exists(candidate):
            return candidate
    # The pip package (pip install clang-tidy) puts the binary beside its module.
    try:
        import clang_tidy  # noqa
        path = os.path.join(os.path.dirname(clang_tidy.__file__), 'data', 'bin',
                            'clang-tidy.exe' if os.name == 'nt' else 'clang-tidy')
        if os.path.exists(path):
            return path
    except ImportError:
        pass
    sys.exit('clang-tidy not found: put it in PATH, set CLANG_TIDY or pass --clang-tidy')


def compiler_of(entry):
    if 'arguments' in entry:
        return entry['arguments'][0]
    return shlex.split(entry['command'], posix=os.name != 'nt')[0]


def run_one(clang_tidy, build, entry):
    args = [clang_tidy, '-p', build, '--quiet']
    env = os.environ.copy()
    compiler = compiler_of(entry)
    if os.name == 'nt' and re.search(r'(g\+\+|gcc|c\+\+)(\.exe)?$', compiler, re.I):
        args.append('--extra-arg=--target=x86_64-w64-mingw32')
        env['PATH'] = os.path.dirname(compiler) + os.pathsep + env.get('PATH', '')
    args.append(entry['file'])
    proc = subprocess.run(args, capture_output=True, text=True, encoding='utf-8', errors='replace', env=env,
                          cwd=entry.get('directory', build))
    return entry['file'], proc.returncode, proc.stdout, proc.stderr


def parse(output):
    """Diagnostics of one file: (file, line, col, kind, check, message, the lines that follow)."""
    result, current = [], None
    for line in output.splitlines():
        m = DIAG.match(line)
        if m and not line.startswith(' '):
            current = [norm(m['file']), int(m['line']), int(m['col']), m['kind'], m['check'] or 'clang-diagnostic',
                       m['msg'], []]
            result.append(current)
            continue
        if current is not None and not re.match(r'^\d+ warnings? (and \d+ errors? )?generated', line) \
                and not line.startswith('Suppressed '):
            current[6].append(line)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('build')
    parser.add_argument('-j', type=int, default=os.cpu_count() or 4)
    parser.add_argument('--clang-tidy')
    parser.add_argument('--out', help='write the report here too')
    parser.add_argument('--only', help='only the sources whose path (with /) matches this regex')
    parser.add_argument('--strict', action='store_true', help='exit code 1 when there are warnings')
    parser.add_argument('--github', action='store_true', help='also print GitHub Actions annotations')
    opt = parser.parse_args()

    build = os.path.abspath(opt.build)
    db_path = os.path.join(build, 'compile_commands.json')
    if not os.path.exists(db_path):
        sys.exit(f'{db_path} not found: configure with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON')
    clang_tidy = find_clang_tidy(opt.clang_tidy)

    root = norm(ROOT) + '/'
    mine = re.compile(r'^' + re.escape(root) + r'(src|tests)/', re.IGNORECASE if os.name == 'nt' else 0)
    only = re.compile(opt.only) if opt.only else None
    entries, seen = [], set()
    for entry in json.load(open(db_path, encoding='utf-8')):
        path = os.path.join(entry.get('directory', build), entry['file'])
        key = norm(path)
        if not mine.match(key) or key in seen or (only and not only.search(key)):
            continue
        seen.add(key)
        entries.append(dict(entry, file=path))
    entries.sort(key=lambda e: norm(e['file']))
    print(f'clang-tidy: {clang_tidy}, {len(entries)} files', file=sys.stderr)

    diags, failed = {}, []
    with concurrent.futures.ThreadPoolExecutor(max_workers=opt.j) as pool:
        for done, (file, code, out, err) in enumerate(
                pool.map(lambda e: run_one(clang_tidy, build, e), entries), 1):
            found = parse(out)
            for d in found:
                diags.setdefault(tuple(d[:6]), d[6])
            if code != 0 and not found:
                failed.append((file, (err or out).strip()))
            print(f'[{done}/{len(entries)}] {os.path.relpath(file, ROOT)}', file=sys.stderr)

    lines, annotations, errors = [], [], 0
    for (file, line, col, kind, check, msg), tail in sorted(diags.items()):
        if not mine.match(file):
            continue
        rel = os.path.relpath(file, ROOT).replace('\\', '/')
        lines.append(f'{rel}:{line}:{col}: {kind}: {msg} [{check}]')
        lines.extend(tail)
        annotations.append(f'::{kind} file={rel},line={line},col={col},title={check}::{msg}')
        errors += kind == 'error'  # the file did not compile: it was not analysed
    counts = collections.Counter(check for (file, _, _, _, check, _) in diags if mine.match(file))
    lines.append('')
    lines.append(f'{sum(counts.values())} warnings:')
    lines.extend(f'  {n:5}  {check}' for check, n in counts.most_common())
    for file, err in failed:
        lines.append(f'clang-tidy failed on {file}:\n{err}')
    report = '\n'.join(lines) + '\n'
    sys.stdout.write(report)
    if opt.github:
        print('\n'.join(annotations))
    if opt.out:
        with open(opt.out, 'w', encoding='utf-8') as f:
            f.write(report)
    return 1 if failed or errors or (opt.strict and counts) else 0


if __name__ == '__main__':
    sys.exit(main())
