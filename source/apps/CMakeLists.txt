# Server is added first so its target exists when the editor/client declare a build-order
# dependency on it (they spawn ECS3DServer as a separate process at runtime). Keep it before them.
add_subdirectory(server)
add_subdirectory(client)
add_subdirectory(editor)

# A headless physics benchmark; it is EXCLUDE_FROM_ALL and only needs the data, sim and log libraries.
add_subdirectory(bench)

# The launcher is a standalone Avalonia (C#) app — independent of the server/client/editor and their
# ordering. It builds via `dotnet publish` (see launcher/CMakeLists.txt), not the C++ toolchain.
add_subdirectory(launcher)
