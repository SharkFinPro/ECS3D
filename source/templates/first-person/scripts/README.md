# Scripts

Put your own `.cs` files in this folder. They are compiled together with the engine's built-in scripts,
and each file holds one public class named like the file.

This template does not copy `PlayerScript`, `PlayerSpawnManager` or any other built-in script: the Player
prefab and the scene already use the built-in ones. A class name that matches a built-in script fails the
compile, so to change one, copy it here under a new class name and point the prefab or scene at the new name.
