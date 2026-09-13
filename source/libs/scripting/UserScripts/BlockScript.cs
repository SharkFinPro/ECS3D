using System;
using System.Numerics;
using ScriptBridge;

public class BlockScript : ScriptBase
{
    public override void start()
    {
        Log.info("Block is ready!");
    }

    public override void fixedUpdate(float dt)
    {

    }

    public override void variableUpdate()
    {

    }

    public override void stop()
    {
        Log.info("Stopping.");
    }

    public override void onCollisionEnter(string otherUuid)
    {
        Log.info($"Collision ENTER with {otherUuid}");
    }

    // onCollisionStay fires every tick the contact persists - left unlogged here so it doesn't flood
    // the log. Override it when you need per-tick contact logic.

    public override void onCollisionExit(string otherUuid)
    {
        Log.info($"Collision EXIT with {otherUuid}");
    }
}
