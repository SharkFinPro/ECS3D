# Server log forwarding

The root `AGENTS.md` summarizes this topic and links here. Read this file before changing the code it describes,
and update it in the same change when that behavior moves.

The server is headless, so its own log (and, via `LogBindings`, the scripts running on it) is
forwarded to connected editors rather than only reaching its console window/log file. `ServerApp` registers
a `RemoteLogSink` with `Log` alongside its file/console sinks, but only in edit mode: a play server admits an
editor without a token, so forwarding there would leak its log. Each `run()` iteration (not gated on a fixed
tick, so a line still gets out while the scene is stopped/paused) drains it - capped by both entry count and
an estimated byte size (`RemoteLogSink::drain`'s two limits, set in `ServerPolicy.h` with `forwardDrainedLog`; a
single oversized message is truncated with a marker at `write()` time, so one huge entry can neither dominate the
sink's memory nor stall the queue for everyone behind it) - and forwards what comes out as a
`MessageType::serverLog` (`net::packServerLog`/`unpackServerLog`) via `NetServer::sendToEditors`, skipping the send
entirely when there is nothing queued and no one to send it to. That copies out the current editor connection ids
under `m_editorMutex` and releases the lock before calling the transport, then makes one call
(`Transport.serverSendToMany`, alongside `serverBroadcast`) for the whole batch of editor connections rather than
one call per editor. The transport queues the batch per connection, so a stalled editor connection never blocks the
authoritative tick thread,
the same as for `ServerBroadcast`. Only connections authorized as `Role::editor` are named, so play
clients do not receive it. `RemoteLogSink::drain` also reports how many entries it had to evict (queue full, nobody
draining fast enough); the editor surfaces that count as a warning instead of silently missing history.
`EditorApp::handleServerLog` writes each forwarded entry straight into its own `RingBufferSink` (bypassing
`Log::write`, which would otherwise stamp it with arrival time instead of the time it carried on the wire),
message-prefixed `"[server] "` so it reads apart from the editor's own logging in the same `LogCategory`
(both sides log under `net`, for instance) without collapsing every entry into one category and losing the
Console panel's level/category filters. This works for an edit-mode server the
editor spawned *and* one it only connected to (`--host`) — it rides the same connection, not a pipe to a
child process. The separate `--console`/`showServerConsole` window some launches show is unrelated and
keeps behaving exactly as before.
