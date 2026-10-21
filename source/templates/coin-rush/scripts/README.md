# Scripts

This sample's own scripts live here. They are compiled together with the engine's built-in scripts, and each
file holds one public class named like the file.

`ScorePillar.cs` is the one sample-only script: it grows a pillar as one player's score rises, since the engine
has no on-screen text. Everything else in the arena (`Collectible`, `Spawner`, `PingPongMover`,
`PlayerSpawnManager`, `ScoreKeeper`, `RoundTimer`, `Spinner`, `Bobber` and `PlayerScript`) is a built-in script, so
this folder copies none of them. A class name that matches a built-in script fails the compile.
