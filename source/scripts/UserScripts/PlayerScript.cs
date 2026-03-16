using System;
using ScriptBridge;

public class PlayerScript : ScriptBase
{
  public override void start()
  {
    Console.WriteLine("[PlayerScript] Player is ready!");
  }

  public override void fixedUpdate(float dt)
  {
    Console.WriteLine("[PlayerScript] Fixed Update!");
  }

  public override void stop()
  {
    Console.WriteLine("[PlayerScript] Stopping.");
  }
}
