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

From the launcher, open the Samples page and click Host on the Coin Rush card. The launcher starts
`ECS3DServer` on the sample's folder and opens the hosting panel. (Host appears only on samples for more than one
player. Open a copy makes your own editable project from a sample, and a project's card menu has Host a game...
for any project you have created.) If a game is already being hosted, the launcher asks whether to stop it first.

The hosting panel shows how long the server has been running and the addresses to share, each with a Copy button.
Click Open another game window to join your own game with a client on this machine. Hide closes the panel, and the
green "Hosting Coin Rush" pill in the sidebar reopens it. Stop server ends the game for everyone in it. Quitting the
launcher while a game is hosted asks whether to stop the server or keep hosting, in which case the launcher
minimizes instead of closing.

Each player who joins gets a body at a spawn point. The controls are the same as in
[Getting started](getting-started.md): W, A, S, D, Space, and the right mouse button to look.

The server has no window of its own, and the launcher hides it. Its log is `server.log` (see Troubleshooting).

### The command line

The launcher runs the same programs you can start yourself. From `bin/`, start the server on the sample folder:

```
ECS3DServer --project templates/coin-rush --port 3000
```

Port 3000 is the default, so `--port` can be left out. The server prints its log in the terminal that started it.
Then open your own window as a client of that server:

```
ECS3DClient --host 127.0.0.1 --port 3000
```

## Share your address

Other players need your machine's address on your local network, and the port. The hosting panel lists the
addresses it found under "Addresses to share" as `address : port`; Copy puts `address:port` on the clipboard.
Players on the same network can reach it. If the panel finds no network address, it says so, and only players on
your own computer can join at 127.0.0.1.

The port starts at 3000. Change it with "Port for hosting" on the Settings page; it applies the next time you host.

Without the launcher, find your address yourself:

- Windows: run `ipconfig` and read the "IPv4 Address" of your active adapter.
- Linux: run `ip addr` and read the `inet` address of your active interface.
- macOS: open System Settings, then Network, and read the IP address of your active connection.

Share it as `address:port`, for example `192.168.1.20:3000`.

## Join a game

In the launcher, click Join a game in the sidebar. Enter the "Host address" and the Port, or paste the host's
`address:port` into the address field and both fields fill in. Click Join. The launcher remembers your last join
and fills it in next time.

The command-line equivalent runs a client with the host's address and port:

```
ECS3DClient --host 192.168.1.20 --port 3000
```

## Firewall

The server accepts connections over TCP. The first time you host, your operating system may ask whether to let
ECS3DServer accept network connections. Allow it on private networks. If friends cannot connect, check that your
firewall allows incoming TCP on the port you chose.

## Troubleshooting

- **The port is in use.** Another program, or an earlier server, already holds the port. The launcher says
  "Port 3000 is already in use; choose another port in Settings." Pick another port there. Started by hand, the
  server stops right after it starts, with an error about the address being in use or with no clear message; pick
  another with `--port`. Give the same number to every client.
- **A client cannot connect.** Check the address and port, that the server is still running (the launcher toasts
  the exit code and `server.log` if it stopped), and that the firewall allows it.
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
