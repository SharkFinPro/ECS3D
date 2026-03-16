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
        if (isRespawnNeeded())
        {
            respawn();
        }
    }

    public override void stop()
    {
        Console.WriteLine("[PlayerScript] Stopping.");
    }

    private bool isRespawnNeeded()
    {
        return transform.getPosition().Y < -250.0f;
    }

    private void respawn()
    {
        transform.stop();
        transform.start();
        rigidBody.setVelocity(0, 0, 0);
    }
}
