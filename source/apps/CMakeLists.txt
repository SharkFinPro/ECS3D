include("${CMAKE_CURRENT_LIST_DIR}/AppIcon.cmake")

# Window and executable icons for the apps that have a window. A game built on the engine points these at its
# own .ico files.
set(ECS3D_EDITOR_ICON "${CMAKE_CURRENT_LIST_DIR}/icons/editor.ico" CACHE FILEPATH "Icon for ECS3DEditor (Windows)")
set(ECS3D_CLIENT_ICON "${CMAKE_CURRENT_LIST_DIR}/icons/client.ico" CACHE FILEPATH "Icon for ECS3DClient (Windows)")

# Server is added first so its target exists when the editor/client declare a build-order
# dependency on it (they spawn ECS3DServer as a separate process at runtime). Keep it before them.
add_subdirectory(server)
add_subdirectory(client)
add_subdirectory(editor)

# The launcher is a standalone Avalonia (C#) app — independent of the server/client/editor and their
# ordering. It builds via `dotnet publish` (see launcher/CMakeLists.txt), not the C++ toolchain.
add_subdirectory(launcher)
