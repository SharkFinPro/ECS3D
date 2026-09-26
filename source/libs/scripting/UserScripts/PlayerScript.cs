using System;
using System.Numerics;
using ScriptBridge;

public class PlayerScript : ScriptBase
{
    // Units per second, at a full stride.
    [ExposeToEditor("Top Speed")]
    private float m_topSpeed = 9.0f;

    // Units per second, straight up.
    [ExposeToEditor("Jump Speed")]
    private float m_jumpSpeed = 15.0f;

    [ExposeToEditor("Look Sensitivity")]
    private float m_lookSensitivity = 0.15f;

    [ExposeToEditor("Invert Look")]
    private bool m_invertLook = false;

    // The share of the gap between the player's horizontal velocity and the one it steers for that it closes each
    // tick, on the ground or in the air.
    private const float Grip = 0.1f;

    // The way the player steers this tick, of unit length or zero, read from input.
    private Vector3 m_heading = Vector3.Zero;

    private bool m_jumpRequested = false;

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

        Log.info("Player is ready!");
    }

    public override void fixedUpdate(float dt)
    {
        if (isRespawnNeeded())
        {
            respawn();
        }

        if (!m_jumpRequested)
        {
            m_wasJumping = false;
        }

        // Changes of velocity, so the player moves the same whatever its mass; the mass only decides how hard it
        // pushes what it runs into. Velocity is in units per tick, hence dt.
        Vector3 velocity = rigidBody.getVelocity();
        Vector3 steer = (m_heading * (m_topSpeed * dt) - new Vector3(velocity.X, 0.0f, velocity.Z)) * Grip;
        if (m_jumpRequested)
        {
            steer.Y = m_jumpSpeed * dt;
        }

        rigidBody.applyForce(steer, transform.getPosition(), ForceMode.VelocityChange);

        m_heading = Vector3.Zero;
        m_jumpRequested = false;

        // Mouse-look owns rotation, so cancel any physics spin (e.g. from a collision) before physics
        // integrates it this tick - otherwise the view would fight/jitter against the look direction.
        rigidBody.setAngularVelocity(0, 0, 0);
    }

    public override void variableUpdate()
    {
        // Look first so movement uses this tick's heading (movement is relative to the current yaw).
        handleLook();
        handleInput();
    }

    public override void stop()
    {
        Log.info("Stopping.");
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

        // Movement follows where the player faces: forward is the camera's forward projected onto the
        // ground plane (yaw only, so looking up/down doesn't tilt movement). Assumes the camera's forward
        // is the object's -Z, so the body's yaw is the movement heading.
        float forwardInput = 0;
        if (input.keyIsPressed(Key.UP))
        {
            forwardInput += 1.0f;
        }

        if (input.keyIsPressed(Key.DOWN))
        {
            forwardInput -= 1.0f;
        }

        float strafeInput = 0;
        if (input.keyIsPressed(Key.RIGHT))
        {
            strafeInput += 1.0f;
        }

        if (input.keyIsPressed(Key.LEFT))
        {
            strafeInput -= 1.0f;
        }

        if (forwardInput != 0 || strafeInput != 0)
        {
            // Base heading is the camera's own facing (its direction field) on the ground plane, so
            // "forward" follows wherever the camera actually points - not an assumed -Z.
            Vector3 camDir = camera.getDirection();
            Vector3 baseForward = new Vector3(camDir.X, 0.0f, camDir.Z);
            baseForward = baseForward.LengthSquared() > 0.0001f
                ? Vector3.Normalize(baseForward)
                : new Vector3(0.0f, 0.0f, -1.0f); // camera looks straight up/down: fall back to forward

            // Rotate the base heading by the body's current yaw (mouse-look), then derive right from it.
            float yawRad = transform.getRotation().Y * (MathF.PI / 180.0f);
            float cos = MathF.Cos(yawRad), sin = MathF.Sin(yawRad);
            Vector3 forward = new Vector3(
                baseForward.X * cos + baseForward.Z * sin,
                0.0f,
                -baseForward.X * sin + baseForward.Z * cos
            );
            Vector3 right = new Vector3(-forward.Z, 0.0f, forward.X);

            Vector3 move = forward * forwardInput + right * strafeInput;
            if (move.LengthSquared() > 0.0f)
            {
                m_heading = Vector3.Normalize(move);
            }
        }

        if (!m_wasJumping && !rigidBody.isFalling() && input.keyIsPressed(Key.X))
        {
            m_jumpRequested = true;
            m_wasJumping = true;
        }
    }
}
