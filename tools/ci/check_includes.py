"""Compiles every first-party translation unit with -fsyntax-only to catch a missing include."""
import argparse
import concurrent.futures
import json
import os
import shlex
import subprocess
import sys

# Options that name an output or a dependency file, with the number of arguments each one takes.
DROPPED_WITH_VALUE = {"-o", "-MF", "-MT", "-MQ"}
DROPPED_FLAGS = {"-c", "-MD", "-MMD", "-Winvalid-pch"}


def strip_arguments(arguments):
    """Returns the compile arguments without output, dependency and precompiled-header options."""
    result = []
    skip_next = False
    for index, argument in enumerate(arguments):
        if skip_next:
            skip_next = False
            continue
        if argument in DROPPED_WITH_VALUE:
            skip_next = True
            continue
        if argument in DROPPED_FLAGS:
            continue
        if argument == "-include":
            # The forced include of a CMake precompiled header is the one thing this check must not use.
            following = arguments[index + 1] if index + 1 < len(arguments) else ""
            if "cmake_pch" in following:
                skip_next = True
                continue
        result.append(argument)
    result.append("-fsyntax-only")
    return result


def entry_arguments(entry):
    """Returns the argument list of a compile_commands.json entry."""
    if "arguments" in entry:
        return list(entry["arguments"])
    return shlex.split(entry["command"])


def is_first_party(path, source_root, launcher_root):
    """Returns True for a file under source/ that is not fetched code or the C# launcher."""
    if not path.startswith(source_root + os.sep):
        return False
    if path.startswith(launcher_root + os.sep):
        return False
    return "_deps" not in path.split(os.sep)


def select_entries(entries, root):
    """Returns the first-party entries, one per file, sorted by path."""
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
    """Compiles one entry front end only and returns (path, ok, output)."""
    path, entry = item
    arguments = strip_arguments(entry_arguments(entry))
    result = subprocess.run(arguments, cwd=entry["directory"], capture_output=True, text=True)
    return path, result.returncode == 0, result.stdout + result.stderr


def main():
    """Runs the check and returns the process exit code."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", help="build directory holding compile_commands.json")
    parser.add_argument("root", help="repository root")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    args = parser.parse_args()

    with open(os.path.join(args.build_dir, "compile_commands.json"), encoding="utf-8") as file:
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
