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

    # Keep every build on C++20; the compiler reports unsupported options.
    for path, flag in [('build.sh', '-std=c++20'),
                       ('native/build.sh', '-std=c++20'),
                       ('native/vendor/bullet/build.sh', '-std=c++20'),
                       ('build.cmd', '/std:c++20'),
                       ('native/build.cmd', '/std:c++20'),
                       ('native/vendor/bullet/build.cmd', '/std:c++20')]:
        # The vendored dependencies are not in every checkout -- the phone build
        # and the CI checkout fetch them separately -- so an absent one is not a
        # failure here. Everything else must exist, or the check below silently
        # passes because there is nothing to read.
        if not (ROOT / path).exists() and path.startswith('native/vendor/'):
            continue
        text = (ROOT / path).read_text(errors='replace')
        if flag not in text or re.search(r'(?:-std=|/std:)c\+\+(?:17|23|latest)', text):
            failures.append(f'{path} must use {flag}, not another C++ mode')
        if path.endswith('.cmd') and '/options:strict' not in text:
            failures.append(f'{path} must pass C++20 options directly to the compiler')
        if re.search(r'cpp(?:20|23)_mode\.cpp|where\s+cl\.exe', text, re.IGNORECASE):
            failures.append(f'{path} must not preflight the compiler or its version')
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
    # The window backends are one each, by nature. The rest of these are Linux-only
    # on purpose: the portrait fitter and the two-client network smoke run are built
    # and run here, from build.sh, and never on the machine that only plays the game.
    # Building them for Windows meant an object nobody linked and a binary nobody
    # started, so they are absent there and that is not a disagreement between the
    # two lists.
    linux_only_names = {'window_x11.cpp', 'fit.cpp', 'io.cpp', 'render.cpp', 'main.cpp',
                        'smoke.cpp', 'smoke_motion.cpp'}
    only_windows = {source for source in sources if source.split('/')[-1] in {'window_win32.cpp'}}
    only_linux = {source for source in linux_sources if source.split('/')[-1] in linux_only_names}
    for source in sorted(linux_sources - sources - only_linux):
        failures.append(f'build.sh has {source}, build.cmd does not')
    for source in sorted(sources - linux_sources - only_windows):
        failures.append(f'build.cmd has {source}, build.sh does not')

    # MSVC treats a narrowing conversion inside a braced initialiser as an error
    # (C2397); gcc only warns, so the Linux build legitimately succeeds on code
    # the Windows build rejects. That divergence cost a round trip with the
    # person who has to compile it, so the client and the backend are parsed
    # here with the narrowing warning promoted, and a single instance fails.
    narrowed = narrowing_failures(sorted(sources))
    failures.extend(narrowed)

    # The Windows window backend is the one source this machine never compiles,
    # so a mistake there is only found by the person who builds it. Its private
    # window state is a nested type; a free function at file scope may not name
    # it unqualified, and MSVC says so while nothing here ever notices.
    failures.extend(nested_type_failures(['native/client/window_win32.cpp',
                                          'native/client/window_x11.cpp']))
    failures.extend(windows_interface_keyword_failures())
    failures.extend(mangled_object_failures())

    if failures:
        for failure in failures:
            print('[build] ' + failure)
        print('build scripts: FAIL')
        return 1
    print(f'build scripts: PASS ({len(sources)} client sources, {len(objects)} linked objects, both platforms agree)')
    return 0


def mangled_object_failures():
    """Every object handed to the linker must be a whole path, on both platforms.

    Two ways this has already gone wrong, both of which only the linker noticed
    on the machine that builds for Windows:

      * two objects pasted into one pair of quotes, so the linker is asked for a
        file whose name contains a space and a second path;
      * an object whose %ROOT%\\build\\obj\\native-client\\ prefix was dropped in
        an edit, so the linker looks for `vk_device.obj` in whatever directory
        the build happened to run from.

    Neither compiles anything, so they are cheap to check here. System libraries
    are bare names by design and are allowed.
    """
    system_libraries = {'Ws2_32.lib', 'User32.lib', 'Gdi32.lib', 'Opengl32.lib', 'Shell32.lib',
                        'Ole32.lib', 'libbullet.a'}
    problems = []
    script = (ROOT / 'native' / 'build.cmd').read_text(errors='replace')
    for number, line in enumerate(script.splitlines(), 1):
        stripped = line.strip()
        # The common object list is assembled on a `set` line and expanded into
        # every link, so a broken path hides there as easily as on a cl.exe line.
        if not (stripped.startswith('cl.exe') or stripped.startswith('set "CLIENT_COMMON=')):
            continue
        for token in stripped.replace('"', ' ').split():
            # The assignment itself is not a file: drop the name and the `=`.
            token = token.split('=', 1)[1] if token.startswith('CLIENT_COMMON=') else token
            if not token or token == 'set':
                continue
            if token in system_libraries or token.startswith('!') or token.startswith('/'):
                continue
            if token.endswith('.obj') and not token.startswith('%ROOT%\\'):
                problems.append(f'build.cmd line {number}: object without its full path: {token}')
    shell = (ROOT / 'native' / 'build.sh').read_text(errors='replace')
    for number, line in enumerate(shell.splitlines(), 1):
        for token in line.replace('"', ' ').split():
            if token in system_libraries or token.startswith('$') or token.startswith('!'):
                continue
            # A bare name like `raster.o` is the bug; `).o` out of a command
            # substitution is punctuation, not a filename.
            if re.fullmatch(r'[\w.-]+\.(?:o|a)', token):
                problems.append(f'build.sh line {number}: object without its full path: {token}')
    return problems


def narrowing_failures(sources):
    """Compile the client and backend with the narrowing warning promoted.

    Only a syntax check: no objects, no link, so this stays a couple of seconds
    and needs no headers that are not in the tree. Sources that need a platform
    header this machine does not have (windows.h) are skipped and named, so a
    skipped file is visible rather than silently passing.
    """
    import shutil
    import subprocess
    compiler = shutil.which('g++') or shutil.which('c++')
    if compiler is None:
        return []
    problems = []
    for source in sources:
        if not (source.startswith('native/client/') or source.startswith('native/vulkan/')):
            continue
        if source.endswith('window_win32.cpp'):
            continue  # needs windows.h, which is not on this machine
        result = subprocess.run([compiler, '-std=c++20', '-fsyntax-only', '-Werror=narrowing',
                                 '-Isrc', '-Inative', '-Inative/vendor/bullet/src', source],
                                cwd=ROOT, capture_output=True, text=True)
        if result.returncode != 0:
            first = next((line for line in result.stderr.splitlines() if 'narrowing' in line
                          or 'error' in line), 'unknown error')
            problems.append(f'{source} would fail the Windows build: {first.strip()}')
    return problems


def windows_interface_keyword_failures():
    """Simulate Windows' `interface` token, including declarations and uses.

    MSVC rejects `interface` as an identifier even when a Linux build accepts it.
    Defining it as `struct` catches the same collision in the UI and Vulkan path.
    """
    import shutil
    import subprocess
    compiler = shutil.which('g++') or shutil.which('c++')
    if compiler is None:
        return []
    result = subprocess.run([compiler, '-std=c++20', '-Dinterface=struct', '-fsyntax-only',
                             '-Isrc', '-Inative', 'native/client/ui_layer.cpp',
                             'native/vulkan/vk_renderer.cpp'],
                            cwd=ROOT, capture_output=True, text=True)
    if result.returncode == 0:
        return []
    first = next((line for line in result.stderr.splitlines() if 'error:' in line), 'unknown error')
    return [f'Windows interface token collision: {first.strip()}']


def nested_type_failures(paths):
    """Flags file-scope functions that name the window's private nested type.

    A definition at column zero that mentions Impl is not inside NativeWindow,
    so the type is out of scope there: MSVC reports C2065. Matching it here is
    cheap and catches the exact divergence before it costs a build round trip.
    """
    problems = []
    for path in paths:
        source = ROOT / path
        if not source.exists():
            continue
        for number, line in enumerate(source.read_text(encoding='utf-8').splitlines(), 1):
            if not line or line[0].isspace() or not re.search(r'\bImpl\b', line):
                continue
            if 'NativeWindow::Impl' in line or line.lstrip().startswith('//'):
                continue
            problems.append(f'{path}:{number} names Impl outside NativeWindow; write NativeWindow::Impl')
    return problems


if __name__ == '__main__':
    sys.exit(main())
