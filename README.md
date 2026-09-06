# ECS3D

This project focuses on developing a robust 3D Entity Component System, which is seamlessly integrated with a high-performance 3D rendering engine.

## Build Instructions

### Prerequisites

Before building, ensure you have the following dependencies installed:

1. **CMake** (version 3.29 or higher)
2. **Vulkan SDK** (latest version recommended)
3. **Git** (for cloning the repository)

### Cloning the Repository and Building the Project

1. Clone the Repository

First, clone the repository to your local machine:

```bash
git clone https://github.com/SharkFinPro/ECS3D.git
cd ECS3D
```

2. Configure and Build

`CMakePresets.json` at the repo root carries the generator, the build directory and the build type, so
there is nothing to remember and nothing to pass:

```bash
cmake --preset debug
cmake --build --preset debug
```

`release` is the same pair with optimizations on. Each preset writes to its own directory
(`cmake-build-debug`, `cmake-build-release`), so the two can coexist. `ninja` is required; the presets
pin it as the generator so a build behaves the same everywhere.

The C# projects are built **through CMake**, never directly. Running `dotnet build` or `dotnet publish`
on them produces a second set of generated attributes and the next CMake build fails with `CS0579:
Duplicate attribute`.

3. Run the Tests

`check` builds the test suite and runs it through CTest:

```bash
cmake --build --preset debug-check
```

To re-run the tests without rebuilding:

```bash
ctest --preset debug
```

4. Run the Executable

Everything is written to the preset's `bin` directory. You can run the editor with:

```bash
cd cmake-build-debug/bin
./ECS3DEditor
```

### Building with Sanitizers

The `sanitize` preset is `debug` plus AddressSanitizer, and UndefinedBehaviorSanitizer on the toolchains
that have it (Clang and GCC; MSVC ships ASan only):

```bash
cmake --preset sanitize
cmake --build --preset sanitize-check
```

It is aimed at the **test suite**, which is headless and links no CLR. Running the editor or the server
under ASan is a different problem: they host CoreCLR, which does its own memory management and needs
sanitizer options set before it starts. Treat an app crash under this preset as a question about the
setup, not as a finding, until the suite itself is clean.
