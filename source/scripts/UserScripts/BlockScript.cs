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
}
