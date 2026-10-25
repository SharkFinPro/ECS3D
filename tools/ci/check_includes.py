"""Compile every first-party translation unit with -fsyntax-only to catch a missing include."""
import argparse
import concurrent.futures
import json
import os
import shlex
# The compile commands it runs come from the build's own compile_commands.json, never from outside input.
import subprocess  # nosec B404
import sys

# Options that name an output or dependency file and take the following argument as their value.
DROPPED_WITH_VALUE = {"-o", "-MF", "-MT", "-MQ"}
DROPPED_FLAGS = {"-c", "-MD", "-MMD"}
# A file compiled through CMake's precompiled header would pass on includes it does not make itself.
PCH_MARKER = "cmake_pch"
# Long enough for the heaviest translation unit's front end, short enough that a hung compile fails the row.
COMPILE_TIMEOUT_SECONDS = 600


def strip_arguments(arguments):
    """Return the compile arguments without output and dependency options, with -fsyntax-only appended."""
    result = []
    skip_next = False
    for argument in arguments:
        if skip_next:
            skip_next = False
            continue
        if argument in DROPPED_WITH_VALUE:
            skip_next = True
            continue
        if argument in DROPPED_FLAGS:
            continue
        result.append(argument)
    result.append("-fsyntax-only")
    return result


def entry_arguments(entry):
    """Return the argument list of a compile_commands.json entry."""
    if "arguments" in entry:
        return list(entry["arguments"])
    return shlex.split(entry["command"])


def is_first_party(path, source_root, launcher_root):
    """Return True for a file under source/ that is not fetched code or the C# launcher."""
    if not path.startswith(source_root + os.sep):
        return False
    if path.startswith(launcher_root + os.sep):
        return False
    return "_deps" not in path.split(os.sep)


def select_entries(entries, root):
    """Return the first-party entries, one per file, sorted by path."""
    source_root = os.path.normcase(os.path.realpath(os.path.join(root, "source")))
    launcher_root = os.path.join(source_root, "apps", "launcher")
    selected = {}
    for entry in entries:
        path = entry["file"]
        if not os.path.isabs(path):
            path = os.path.join(entry["directory"], path)
        path = os.path.normcase(os.path.realpath(path))
        if is_first_party(path, source_root, launcher_root):
            selected.setdefault(path, entry)
    return sorted(selected.items())


def check(item):
    """Compile one entry front end only and return (path, ok, output)."""
    path, entry = item
    arguments = strip_arguments(entry_arguments(entry))
    if any(PCH_MARKER in argument for argument in arguments):
        return path, False, "compiled with a precompiled header; configure with ECS3D_PRECOMPILED_HEADERS=OFF"
    try:
        result = subprocess.run(  # nosec B603
            arguments, cwd=entry["directory"], capture_output=True, text=True, timeout=COMPILE_TIMEOUT_SECONDS)
    except (OSError, subprocess.TimeoutExpired) as error:
        return path, False, str(error)
    return path, result.returncode == 0, result.stdout + result.stderr


def main():
    """Run the check and return the process exit code."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", help="build directory holding compile_commands.json")
    parser.add_argument("root", help="repository root")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    args = parser.parse_args()

    commands_path = os.path.join(args.build_dir, "compile_commands.json")
    if not os.path.isfile(commands_path):
        print(f"error: {commands_path} not found; configure with a Ninja or Makefile generator first", flush=True)
        return 1
    with open(commands_path, encoding="utf-8") as file:
        entries = json.load(file)

    selected = select_entries(entries, args.root)
    if not selected:
        print("error: no first-party translation units found in compile_commands.json", flush=True)
        return 1

    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        results = list(pool.map(check, selected))

    failures = sorted((path, output) for path, ok, output in results if not ok)
    for path, output in failures:
        print(f"FAILED: {os.path.relpath(path, args.root)}", flush=True)
        print(output, flush=True)

    print(f"{len(failures)} of {len(results)} translation units failed", flush=True)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
