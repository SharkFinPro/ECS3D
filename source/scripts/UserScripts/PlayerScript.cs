using System;
using ScriptBridge;

public class PlayerScript : ScriptBase
{
  private float _elapsed;
  private int   _ticks;

  public override void start()
  {
    Console.WriteLine("[PlayerScript] Player is ready!");
    _elapsed = 0f;
    _ticks   = 0;
  }

  public override void fixedUpdate(float dt)
  {
    _elapsed += dt;
    _ticks++;
    Console.WriteLine($"[PlayerScript] Tick {_ticks:D3} | dt={dt:F3}s | elapsed={_elapsed:F2}s");
    Console.WriteLine("[PlayerScript] Hello, World!");
  }

  public override void stop()
  {
    Console.WriteLine($"[PlayerScript] Stopping after {_ticks} ticks ({_elapsed:F2}s total).");
  }
}
