using System;
using System.Numerics;
using ScriptBridge;

public class BlockScript : ScriptBase
{
    public override void start()
    {
        Console.WriteLine("[BlockScript] Block is ready!");
    }

    public override void fixedUpdate(float dt)
    {

    }

    public override void variableUpdate()
    {

    }

    public override void stop()
    {
        Console.WriteLine("[BlockScript] Stopping.");
    }

    public override void onCollisionEnter(string otherUuid)
    {
        Console.WriteLine($"[BlockScript] Collision ENTER with {otherUuid}");
    }

    // onCollisionStay fires every tick the contact persists — left unlogged here so it doesn't flood
    // the console. Override it when you need per-tick contact logic.

    public override void onCollisionExit(string otherUuid)
    {
        Console.WriteLine($"[BlockScript] Collision EXIT with {otherUuid}");
    }
}
