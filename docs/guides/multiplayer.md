# Multiplayer

This guide hosts a game and joins it from other machines, using the Coin Rush sample. It assumes ECS3D is built
and you have read [Getting started](getting-started.md).

## How it works

A server owns the simulation: it runs the scripts and physics and decides what happens. Each client draws the
world the server sends it and sends back its keyboard and mouse input. Playing alone uses the same design, with
the client starting a server on your machine as a child process. Hosting a game means running that server
yourself and having other clients connect to it.

## Host Coin Rush

Coin Rush is a 1-4 player arena. Each player spawns at a corner, and walking through a coin scores a point.
Pushers, rolling boulders and the edge of the floor make knocking rivals off a strategy. A round lasts 90
seconds, the highest score wins, and the next round starts after a short break. The engine has no on-screen
text, so each score is a pillar in the player's color beside their spawn point, and the scores are also written
to the log.

From `bin/`, start the server on the sample folder:

```
ECS3DServer --project templates/coin-rush --port 3000
```

The server has no window of its own. It prints its log in the terminal that started it. Port 3000 is the
default, so `--port` can be left out. Then open your own window as a client of that server:

```
ECS3DClient --host 127.0.0.1 --port 3000
```

Each player who joins gets a body at a spawn point. The controls are the same as in
[Getting started](getting-started.md): W, A, S, D, Space, and the right mouse button to look.

## Share your address

Other players need your machine's address on your local network, and the port.

- Windows: run `ipconfig` and read the "IPv4 Address" of your active adapter.
- Linux: run `ip addr` and read the `inet` address of your active interface.
- macOS: open System Settings, then Network, and read the IP address of your active connection.

Share it as `address:port`, for example `192.168.1.20:3000`. Players on the same network can reach it.

## Join a game

Each player runs a client with your address and port:

```
ECS3DClient --host 192.168.1.20 --port 3000
```

## Firewall

The server accepts connections over TCP. The first time it runs, your operating system may ask whether to let
ECS3DServer accept network connections. Allow it on private networks. If friends cannot connect, check that your
firewall allows incoming TCP on the port you chose.

## Troubleshooting

- **The port is in use.** Another program, or an earlier server, already holds the port, so the server cannot
  start listening. Pick another with `--port`, and give the same number to every client.
- **A client cannot connect.** Check the address and port, that the server is still running, and that the
  firewall allows it.
- **Use the same build.** Run the server and every client from the same build of ECS3D. The connection does not
  compare versions, so a mismatch can fail in confusing ways.
- **Logs.** Each program writes a log file in `ECS3D/logs` under your user data folder: `server.log`,
  `client.log` and `editor.log`. That folder is `%APPDATA%` on Windows, `~/Library/Application Support` on
  macOS, and `$XDG_CONFIG_HOME` (or `~/.config` when it is unset) on Linux. A server that a client or editor
  starts for you logs to `client-server.log` or `editor-server.log` instead. Add `--console` to a client or
  editor to see its log in a console window as well.

## Test two players on one machine

Start one server, then two clients against it, each from its own terminal:

```
ECS3DServer --project templates/coin-rush
ECS3DClient --host 127.0.0.1
ECS3DClient --host 127.0.0.1
```

Each client is its own player, with its own body.
