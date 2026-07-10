using System;
using System.Numerics;
using ScriptBridge;

public class PlayerScript : ScriptBase
{
    [ExposeToEditor("Movement Speed")]
    private float m_speed = 1.0f;

    [ExposeToEditor("Jump Force")]
    private float m_jumpForce = 15.0f;

    [ExposeToEditor("Look Sensitivity")]
    private float m_lookSensitivity = 0.15f;

    [ExposeToEditor("Invert Look")]
    private bool m_invertLook = false;

    private Vector3 m_appliedForce = new Vector3(0, 0, 0);

    private bool m_wasJumping = true;

    // Accumulated mouse-look angles (degrees). The camera follows the object's rotation (RenderSystem
    // rotates the Camera's direction by it), so rotating the Transform here is what turns the view.
    private float m_yaw = 0.0f;
    private float m_pitch = 0.0f;

    public override void start()
    {
        // Seed from any editor-set body rotation so mouse-look starts from the placed facing.
        Vector3 rotation = transform.getRotation();
        m_pitch = rotation.X;
        m_yaw = rotation.Y;

        Console.WriteLine("[PlayerScript] Player is ready!");
    }

    public override void fixedUpdate(float dt)
    {
        if (isRespawnNeeded())
        {
            respawn();
        }

        if (m_appliedForce.Y == 0)
        {
            m_wasJumping = false;
        }

        rigidBody.applyForce(m_appliedForce * dt, transform.getPosition());

        m_appliedForce *= 0;

        // Mouse-look owns rotation, so cancel any physics spin (e.g. from a collision) before physics
        // integrates it this tick — otherwise the view would fight/jitter against the look direction.
        rigidBody.setAngularVelocity(0, 0, 0);
    }

    public override void variableUpdate()
    {
        handleInput();
        handleLook();
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

    private void handleLook()
    {
        if (!input.windowIsFocused())
        {
            return;
        }

        // Only look while the right mouse button is held (same convention as the free-fly camera), so plain
        // cursor movement doesn't swing the view.
        if (!input.mouseButton(MouseButton.Right))
        {
            return;
        }

        Vector2 delta = input.mouseDelta();

        // Mouse right -> look right; mouse up -> look up (flip the vertical with Invert Look). Pitch is
        // clamped shy of straight up/down so the view can't roll past vertical.
        m_yaw -= delta.X * m_lookSensitivity;
        float pitchStep = delta.Y * m_lookSensitivity;
        m_pitch += m_invertLook ? pitchStep : -pitchStep;
        m_pitch = Math.Clamp(m_pitch, -89.0f, 89.0f);

        transform.setRotation(m_pitch, m_yaw, 0.0f);
    }

    private void handleInput()
    {
        if (!input.windowIsFocused())
        {
            return;
        }

        float xForce = 0;
        if (input.keyIsPressed(Key.LEFT))
        {
            xForce += m_speed;
        }

        if (input.keyIsPressed(Key.RIGHT))
        {
            xForce -= m_speed;
        }

        if (xForce != 0)
        {
            m_appliedForce.X = xForce;
        }

        float zForce = 0;
        if (input.keyIsPressed(Key.UP))
        {
            zForce += m_speed;
        }

        if (input.keyIsPressed(Key.DOWN))
        {
            zForce -= m_speed;
        }

        if (zForce != 0)
        {
            m_appliedForce.Z = zForce;
        }

        if (!m_wasJumping && !rigidBody.isFalling() && input.keyIsPressed(Key.X))
        {
            m_appliedForce.Y = m_jumpForce;
            m_wasJumping = true;
        }
    }
}
