# Profiling Your Game

This guide shows how to find out where a frame or a server tick spends its time, using the editor's Profiler
panel, the client's stats overlay and the server's stats log line. It assumes the editor is built and running.

Two things are measured separately. The **editor lane** is the editor's own frame: how long each pass of its main
loop takes. The **server lane** is the simulation: how long each server tick takes. A slow game can be slow in
either one, and they have different fixes.

## Opening the Profiler

Press F3, or open Window, then Profiler. The panel is closed by default. Press F3 again, or close the window, to hide
it. While the panel is open, the editor asks the server to stream its statistics, and stops asking when the panel
closes and the corner readout is off, so a closed Profiler costs the server nothing.

## Reading the editor lane

The top chart is the editor's frame lane. Each column is one frame, oldest on the left, and its height is the frame
time. The column is split into colored groups, from the bottom up:

- Network in: applying what the server sent, including snapshots.
- Input: reading the keyboard and mouse.
- Editor UI: drawing the editor panels.
- Scene prep: getting the scene ready to draw.
- Renderer: drawing the frame. See [What the Renderer block means](#what-the-renderer-block-means).
- Profiler: the cost of drawing the Profiler and the corner readout themselves.
- Other: scopes that none of the other groups claim.

The line above the chart gives the average, 95th percentile (p95) and maximum frame time over the window, the frames
per second, and the number of hitches. A hitch is a frame that took more than twice the window's median frame time.
A hitch column is marked in the chart so a single spike is easy to find.

The header has these controls:

- Window: how much history the chart shows, 1, 5 or 10 seconds.
- Scale: the chart's full height. Auto picks the smallest fixed scale (16.7, 33.3, 50 or 100 ms) that holds nearly
  every frame. A fixed scale keeps the chart steady while you compare changes.
- The legend under each chart: click a group to hide it. Hidden groups are remembered, and they are left out of the
  chart only.

## Reading the server lane

Below the editor lane is the server's tick lane. The server runs a fixed 50 Hz tick, so every tick has a 20 ms budget,
drawn as a line across the chart. A column that crosses the line is a tick that took longer than the time it had.

Its groups are Scripts, Physics, Collisions, Replication, Network in and Other. The heading shows the average, p95 and
maximum tick time, whether the server is local or remote, and "(debug build)" when the server is a debug build,
which is much slower than a release build. The line under the chart counts dropped steps. A dropped step is a
fixed step the server discarded because it fell too far behind to catch up. Seeing any means the server could not keep
up.

The lane shows a short notice when there is no chart to draw: "Waiting for server statistics...", "This server does
not report statistics.", "Disconnected", or "Scene stopped" when no scene is running.

## Pause and inspect

Click Pause, or click any column, to freeze both lanes. Clicking a column also selects it. The Breakdown table
shows that frame or tick: click a column in the server lane to see the server's table, and click in the editor lane to
go back. Click the selected column again, or click Resume, to return to live. Closing the panel also returns to live.

While paused, Save capture writes the frozen data (see below).

## The breakdown table

The Breakdown section lists the scopes of the lane you last clicked as a tree. For each scope it shows the
selected sample's time (or the newest sample's, when none is selected), and the average, p95, maximum and share of
the window's average frame or tick. A scope that did no work in the window is shown only when something inside it did.
Use it to go from "Scripts is the tall group" to the specific scope inside it.

## The Network section

The Network section appears whenever the server lane does. Over the chosen window it shows:

- Sent and Received: bytes per second at the server, with the message types that make up the most.
- Editor received: what the editor itself receives per second.
- Snapshots: the last snapshot's size, why it was sent and how long ago, the largest one held and how many were sent in
  the last minute. A table below lists the last ten with their size, reason, build time and recipients. A snapshot is
  the whole project sent at once, so a large or frequent one is worth explaining.
- Connections, and how many of them are editors.

Byte counts leave out the transport's own framing, which adds 4 bytes per message on TCP.

## The corner readout

Turn on Corner readout in the Profiler header, or Stats readout in Settings, then Viewport, to draw a small box in the
top-right corner of the viewport. It shows the frame time and frames per second over the last second, the server's
tick time, and the server's send rate. It is drawn on the viewport itself, so it stays visible when you press
F10 to hide the editor panels. Turning it on also makes the editor ask the server for statistics, even with the Profiler
closed.

## The client overlay and the server log line

The standalone client has its own overlay. Press F3 to cycle it through off, compact and detailed. Compact shows the
frame time, frames per second and the inbound network rate. Detailed adds a five-second frame chart, the time between
the server's state updates, the largest incoming message types and the size of the last snapshot. Start the client with
`--stats` to begin in compact.

The server writes its own summary to its log every ten seconds. Change the interval with `--stats-interval <seconds>`
on the server (a whole number from 0 to 3600), or use 0 to turn the line off. Each line gives the tick average, p50,
p95,
p99 and maximum, the tick and dropped step counts, the average time in scripts, physics, collisions and replication, the
object count and the network figures. When the server saw no ticks it logs "stats: idle (no ticks)" instead.

## Saving a capture

Click Save capture... in the Profiler header to write what the panel is showing to disk, for comparing runs or sharing
with someone who can read a spreadsheet. If the panel is paused, it writes the frozen data. It saves the editor lane
and, when the server's statistics have arrived, the server lane, for the number of seconds in the Window setting.

The files go in a `captures` folder inside the editor's per-user data directory (`%APPDATA%/ECS3D` on Windows,
`~/Library/Application Support/ECS3D` on macOS, `~/.config/ECS3D` elsewhere), named by the time in UTC, for example
`editor-20261103-143000.csv` and `editor-20261103-143000.json`. After a save the panel shows the path with an
Open folder button, or the reason the save failed. Two saves in the same second share a name, and the second replaces
the first.

The CSV has one row per sample: the editor lane's rows first, then the server lane's, oldest first within each. It
uses LF line endings and plain integers, so it reads the same under any locale. The columns are:

- `lane`: `editor` or `server`.
- `sequence`: the sample's number within its lane.
- `wallMicros`: the whole frame or tick in microseconds.
- One column per group for the editor lane, then one per group for the server lane, each holding the microseconds the
  sample spent in that group: `frame.networkIn`, `frame.input`, `frame.editorUi`, `frame.scenePrep`, `frame.renderer`,
  `frame.profiler`, `frame.other`, then `server.scripts`, `server.physics`, `server.collisions`,
  `server.replication`, `server.networkIn`, `server.other`. Times are exclusive: a scope's time minus its child
  scopes' time, summed
  into its group. Hidden legend groups are still written. The other lane's columns are left empty.
- `ticks` and `droppedSteps`: the server lane's counts for that sample, empty on editor rows. One sample can cover
  more than one tick.

The JSON sidecar has the same base name. It records the engine version, build type (Debug or Release), platform, app,
scene name, object count, connection count and capture time, then for each lane its name, kind, sample count and scope
table (`id`, `parent`, `name`), and the snapshots table from the Network section. The scene name and object count are
the only project content in either file. There are no object, script or asset names, and no file paths from your
project.

When comparing two captures, check that the sidecars agree on the build type, because a Debug run is much slower than
a Release run and the numbers do not compare.

## What the Renderer block means

Renderer is the time the editor spends in its render call, and that is not only drawing work. The call also waits
for the previous frame's GPU work to finish and for the display to be ready for the next image. So if the display
is limited to its refresh rate, or the GPU is the slow part, the time shows up as a tall Renderer block even though the
CPU was idle. A frame that is mostly Renderer is limited by the GPU or the display, so cutting script or scene
work will not make it faster. A frame with a small Renderer block and a tall block elsewhere is limited by the CPU
in that block.
