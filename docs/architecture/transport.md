# Transport and CLR hosting

The root `AGENTS.md` summarizes this topic and links here. Read this file before changing the code it describes,
and update it in the same change when that behavior moves.

`Protocol.h` defines the format. `Message::write`/`MessageReader::read` take
integers, enums, `float` and `double` only, and carry `bool` as an explicit 0/1 byte; an aggregate goes
across whole only by specializing `net::wirePackable`, which is a claim - assert the layout - that it has
neither padding nor a pointer in it. `data`'s `WireTypes.h` holds those specializations, for `glm::vec3`
and `uuids::uuid`, and any translation unit that reasons about the trait rather than just calling `write`/`read` includes
it, since otherwise it evaluates the primary template and disagrees with the rest of the program. Widths and byte order are still the caller's problem: pack fixed-width types, and the wire
carries host endianness. `NetServer`/`NetClient` own the format in C++ and hand `ECS3DNetTransport` (C#)
opaque `(type byte, payload)` pairs. Both transports refuse an inbound message over
`TransportBackend.MaxMessageBytes` and drop the connection - TCP on the length the peer declares,
WebSocket on what has actually arrived, since a fragmented message declares none. Neither backend sizes
its read buffer to that declared length up front: TCP grows a buffer to roughly what has actually arrived
(`TcpBackend.ReadBody` starts at 64 KiB and doubles each time the buffer fills, up to the declared
length) and WebSocket
grows its assembly buffer as fragments arrive, so a peer that declares a large frame and then trickles it
in a byte at a time pins only a small multiple of what has actually landed, not the whole declared size.
TCP backs that with a per-read progress timeout (`BodyReadTimeoutMs`, reset on every read rather than
covering the whole frame) so a body that stalls outright still drops just that connection (applied only when the stream `CanTimeout`;
otherwise the deadline alone bounds it). The handshake, the one
message read before a peer is authorized, gets the much smaller `MaxHandshakeBytes`, and both backends bound the whole
pre-authorization stage with one `HandshakeTimeoutMs` deadline, so a silent or trickling peer is dropped. For
WebSocket that stage is the upgrade request and the first message, and each blocking read is capped to the time
left; TCP checks the deadline between reads, so a peer can hold it up to one read timeout past the deadline. Oversize *outbound*
messages are refused at the sender, where there is something useful to say about them. Object nesting has
its own limit alongside the byte ones: `maxObjectDepth` (`data/objects/Object.h`) caps how deep
`Object::unpack`/`loadChildren` and `ObjectManager::reassignUUIDs` will recurse into a wire or JSON
payload, so a tree claiming more depth than any real hierarchy needs is refused rather than exhausting the
stack. A lost client connection is reported the same way: the transport calls
`Transport.DeliverClientDisconnect()` once its client receive loop exits, reaching `NetClient`
(`clientSetDisconnectCallback`) so the client and editor apps can surface it on screen. Before that call,
the loop also releases the connection's own resources (socket, or `WebSocketBackend`'s `ClientConnection`
bundle of socket, `HttpMessageInvoker` and `CancellationTokenSource`) instead of relying on a later
`ClientDisconnect` call, which the apps don't make on their own after a lost-connection notice. The loop
works from the instance it was started with: it clears the backing field with a compare-and-swap against
that instance before disposing it, so a connection a concurrent `ClientConnect` has already installed in
its place is left alone, and `ClientSend` guards its own read of the field with a matching
compare-and-swap plus an `ObjectDisposedException` catch for the same reason. The exiting loop sets
`_clientRunning` false as soon as it stops reading, ahead of that cleanup, so a fast reconnect can start
before the old loop finishes tearing down; the loop compares what the compare-and-swap reports the field
held against its own connection before delivering the notice, and skips delivery when that turns out to be
a different, live connection a concurrent `ClientConnect` already installed, so a reconnect racing the old
loop's exit is not reported as a loss.
The role a connection is actually granted at the handshake (`TransportBackend.TryAuthorizeHandshake`) is
reported to C++ separately from the messages it sends. That method checks the frame type, rejects an
unknown role, and compares the token in constant time. It reports a role it chose, never the byte the peer
sent. Once it succeeds, both backends call
`Transport.DeliverServerAuthorized(connId, role)`, which reaches `NetServer::authorize` and is remembered
in `NetServer::isEditor`. `ServerApp::handleClientMessage` enforces `net::isMutationMessage(type)` (via the tested `isMutationAuthorized` in `apps/server/ServerPolicy.h`, which also holds `planSceneControl`) against
that authorized role (in addition to edit-mode), never against a role a message merely claims - a
connection cannot mutate an edit-mode server's scene by sending a mutation type unless the transport
actually granted it `Role::editor`. `ManagedHost`
boots CoreCLR and resolves
managed statics as native function pointers; inbound frames are pushed from C# socket threads into a
thread-safe `MessageQueue` and drained by the app loop. The transport backend (TCP/WebSocket) is
selected by a single field in `Transport.cs`. `Transport.serverStart` never throws (an exception escaping an
`[UnmanagedCallersOnly]` export terminates the process): it returns the bound port (the OS-picked one for port
0) or a negative `ServerStartError` (-1 address in use, -2 port out of range, -3 anything else, logged on the
transport side), mirrored by `net::ServerStartError` in `NetServer.cpp`. A backend whose listener fails to start
is left not running, so a later start succeeds. `NetServer::start` turns a negative code into a
`std::runtime_error` and leaves the server stopped. The transport logs through a native `setLogCallback`
(`net::transportLog`, category `net`), falling back to the console before it is registered. A client
connect attempt is bounded to `ConnectTimeoutMs` in each backend so the apps' retry loops keep their
deadline instead of hanging on the OS connect timeout. Both backends
broadcast from a snapshot of the connection list taken under the lock and enqueue the one built message on
each connection's `SendQueue`; the tick thread never writes to a socket. Each authorized connection has its
own sender thread (`ecs3d-net-send`) that drains its queue. A peer is dropped when its own send fails or
times out, or when its queue already holds more than `MaxQueuedSendBytes` (one maximum-size message by
default; an empty queue always accepts one message). `SendTimeoutMs` bounds a whole WebSocket message, but
on TCP it bounds each underlying socket send, so it drops a peer that stops reading outright while one that
drips a few bytes at a time is caught by the queue cap instead. Either way a peer that stops reading
disconnects only itself and the tick never blocks on it. Messages still queued when a connection closes or
the server stops are discarded, not flushed.
Shutdown joins every socket thread it started before returning: `ServerStop` flips `_serverRunning` and
takes its snapshot of started threads in the same lock the accept loop checks that flag and registers a
new thread under, so a thread that starts is one this call goes on to join; it then closes the listener
and all connection sockets and joins the accept thread and each per-connection thread.
A connection thread closes its socket, delivers its disconnect, then closes its send queue and joins its
sender thread, and only then removes itself from that list, so a stop that returns has no disconnect
callback still to come. The disconnect goes first so a sender slow to leave a blocked send cannot hold it
past the stop's bounded join.
`ClientDisconnect` closes the connection then joins the receive thread the same way. Each join is bounded
by `ShutdownJoinTimeoutMs`, logging a warning if a thread has not exited by then instead of waiting past
it, and skips a thread that is the caller (joining that thread would deadlock). `NetServer::stop`/
`NetClient::disconnect` rely on that join completing before they clear the `g_activeServer`/
`g_activeClient` pointer the native callbacks read, so by the time a `NetServer`/`NetClient`'s teardown
clears that pointer, no socket thread remains that could still call back into it; both pointers are
`std::atomic` because they are written on the app thread and read on the socket threads with no other
synchronization between them.
