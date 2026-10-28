"""Check the two native build scripts agree with each other and with the tree.

The Windows and Linux builds keep separate source lists by hand, which is how a
file that is in one and not the other becomes a link error on a machine nobody
is sitting at. This walks both, and fails if a listed source is missing, if an
object is linked that nothing compiles, or if the two lists disagree about the
client.

The quoting failure this was written after: `set "CLIENT_COMMON=a.obj b.obj"` is
one quoted string, so a path added with quotes of its own closes the string
early and the linker is handed three paths glued into one. That is checked too.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent


def windows_scripts():
    script = (ROOT / 'native' / 'build.cmd').read_text(errors='replace')
    sources = set()
    for block in re.findall(r'for %%S in \(([^)]*)\)', script):
        for token in block.split():
            if token.endswith('.cpp'):
                sources.add(token.replace('\\', '/'))
    objects = set()
    problems = []
    for line in script.splitlines():
        if line.lstrip().startswith('set "CLIENT_COMMON='):
            if line.count('"') != 2:
                problems.append('the common object list must stay one quoted string; a path with '
                                'quotes of its own closes it early and the linker gets one long path')
            for token in re.findall(r'%ROOT%\\build\\obj\\[^\\ ]+\\([\w.]+)\.obj', line):
                objects.add(token)
    return script, sources, objects, problems


def linux_scripts():
    script = (ROOT / 'native' / 'build.sh').read_text(errors='replace')
    sources = set()
    for block in re.findall(r'SOURCES=\(([^)]*)\)|CLIENT_SOURCES=\(([^)]*)\)', script):
        for part in block:
            for token in part.split():
                if token.endswith('.cpp'):
                    sources.add(token.replace('\\', '/'))
    return script, sources


def main():
    failures = []
    script, sources, objects, problems = windows_scripts()
    failures.extend(problems)

    for source in sorted(sources):
        if not (ROOT / source).exists():
            failures.append(f'build.cmd compiles a file that is not there: {source}')
    for name in sorted(objects):
        if not any(pathlib.Path(source).stem == name for source in sources):
            failures.append(f'build.cmd links an object nothing compiles: {name}.obj')

    for line in script.splitlines():
        stripped = line.strip()
        if stripped.startswith('cl.exe') and '!CLIENT_COMMON!' in stripped:
            if '"!CLIENT_COMMON!' in stripped or 'CLIENT_COMMON!"' in stripped:
                failures.append('CLIENT_COMMON must stay unquoted where it is expanded')
        if stripped.startswith('cl.exe') and '/link' in stripped:
            for quoted in re.findall(r'"(%ROOT%\\build\\obj\\[^"]*)"', stripped):
                if quoted.count('.obj') > 1:
                    failures.append(f'several objects inside one pair of quotes: {quoted[:60]}')

    _, linux_sources = linux_scripts()
    for source in sorted(linux_sources):
        if not (ROOT / source).exists():
            failures.append(f'build.sh compiles a file that is not there: {source}')
    only_windows = {source for source in sources if source.split('/')[-1] in {'window_win32.cpp'}}
    only_linux = {source for source in linux_sources if source.split('/')[-1] in {'window_x11.cpp'}}
    for source in sorted(linux_sources - sources - only_linux):
        failures.append(f'build.sh has {source}, build.cmd does not')
    for source in sorted(sources - linux_sources - only_windows):
        failures.append(f'build.cmd has {source}, build.sh does not')

    if failures:
        for failure in failures:
            print('[build] ' + failure)
        print('build scripts: FAIL')
        return 1
    print(f'build scripts: PASS ({len(sources)} client sources, {len(objects)} linked objects, both platforms agree)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
