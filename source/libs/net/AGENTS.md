# AGENTS.md - ECS3DNet

Agents load this file when they read a file in `source/libs/net/`. It holds what a change here must keep. The full
notes are in [docs/architecture/transport.md](../../../docs/architecture/transport.md) and, for the server log,
[docs/architecture/logging.md](../../../docs/architecture/logging.md).

- C++ owns the wire format (`Protocol.h`). `NetServer`/`NetClient` hand the C# `Transport/` assembly opaque
  `(type byte, payload)` pairs, and the C# side owns the sockets (TCP and WebSocket backends, one chosen in
  `Transport.cs`).
- `Message::write`/`MessageReader::read` take integers, enums, `float` and `double`, and carry `bool` as a 0/1 byte.
  An aggregate crosses whole only through a `net::wirePackable` specialization that asserts it has no padding or
  pointer (`data`'s `WireTypes.h`). Pack fixed-width types; the wire carries host byte order.
- A connection's role is the one `TransportBackend.TryAuthorizeHandshake` granted, reported through
  `NetServer::authorize`. Authorization decisions read that role, not one a message claims.
- C# socket threads push inbound frames into the thread-safe `MessageQueue`, and the app loop drains it. Stop and
  disconnect join every socket thread before clearing the `g_activeServer`/`g_activeClient` pointers the callbacks
  read.
- `MessageQueue.cpp`, `CommandLineSplit.cpp`, `ClientConnectionState.cpp` and `EditorConnections.cpp` compile into
  the test suite, so they stay free of the CLR. `EditToken.h` (`net::generateEditToken`, 128 bits from
  `std::random_device`) is header-only for the same reason.
- `ServerProcess` launches a child server from one flag string. `CommandLineSplit` splits it on Linux and macOS,
  honoring double quotes the way the Windows C runtime does.
- The `Transport/` project builds through CMake only. A direct `dotnet build` or `dotnet publish` breaks the next CMake
  build with error CS0579.
