using System;
using System.Numerics;
using ScriptBridge;

public class PlayerScript : ScriptBase
{
    [ExposeToEditor("Movement Speed")]
    private float m_speed = 1.0f;

    [ExposeToEditor("Jump Force")]
    private float m_jumpForce = 15.0f;

    private Vector3 m_appliedForce = new Vector3(0, 0, 0);

    private bool m_wasJumping = true;

    public override void start()
    {
        Console.WriteLine("[PlayerScript] Player is ready!");
    }

    public override void fixedUpdate(float dt)
    {
        if (isRespawnNeeded())
        {
            respawn();
            return;
        }

        if (m_appliedForce.Y == 0)
        {
            m_wasJumping = false;
        }

        rigidBody.applyForce(m_appliedForce * dt, transform.getPosition());

        m_appliedForce *= 0;
    }

    public override void variableUpdate()
    {
        handleInput();
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

    private void handleInput()
    {
        if (!InputUtils.windowIsFocused())
        {
            return;
        }

        float xForce = 0;
        if (InputUtils.keyIsPressed(Key.LEFT))
        {
            xForce += m_speed;
        }

        if (InputUtils.keyIsPressed(Key.RIGHT))
        {
            xForce -= m_speed;
        }

        if (xForce != 0)
        {
            m_appliedForce.X = xForce;
        }

        float zForce = 0;
        if (InputUtils.keyIsPressed(Key.UP))
        {
            zForce += m_speed;
        }

        if (InputUtils.keyIsPressed(Key.DOWN))
        {
            zForce -= m_speed;
        }

        if (zForce != 0)
        {
            m_appliedForce.Z = zForce;
        }

        if (!m_wasJumping && !rigidBody.isFalling() && InputUtils.keyIsPressed(Key.X))
        {
            m_appliedForce.Y = m_jumpForce;
            m_wasJumping = true;
        }
    }
}
