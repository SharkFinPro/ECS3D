// Helpers shared by the starter scripts: named-object lookup, player lookup, key names, rotation math.
using System;
using System.Numerics;
using ScriptBridge;

// Looks an object up by name without hammering the world: a miss is retried at most once a second, and
// the first miss logs one warning until the object is found and later lost.
public sealed class NamedObject
{
    private const float RetrySeconds = 1.0f;

    private readonly string _owner;
    private string _name = "";
    private string _uuid = "";
    private float _sinceLookup;
    private bool _looked;
    private bool _warned;

    public NamedObject(string owner)
    {
        _owner = owner;
    }

    public string Name
    {
        get => _name;
        set
        {
            value ??= "";
            if (value == _name)
            {
                return;
            }

            _name = value;
            _uuid = "";
            _looked = false;
            _warned = false;
        }
    }

    // Call once per fixed tick. False (with an empty uuid) while the object cannot be found.
    public bool tryResolve(float dt, out string uuid)
    {
        uuid = "";
        if (_name.Length == 0)
        {
            return false;
        }

        if (_uuid.Length > 0)
        {
            if (World.objectExists(_uuid))
            {
                uuid = _uuid;
                return true;
            }

            _uuid = "";
            _looked = false;
        }

        _sinceLookup += dt;
        if (_looked && _sinceLookup < RetrySeconds)
        {
            return false;
        }

        _looked = true;
        _sinceLookup = 0.0f;
        _uuid = World.findObjectByName(_name);
        if (_uuid.Length > 0)
        {
            _warned = false;
            uuid = _uuid;
            return true;
        }

        if (!_warned)
        {
            _warned = true;
            Log.warn($"{_owner}: no object named '{_name}' was found; it will keep looking.");
        }

        return false;
    }
}

// One input action named by a field: re-parsed when the field changes. An unknown name logs one warning
// per value and leaves the action unbound.
public sealed class KeyBinding
{
    private readonly string _label;
    private string? _parsedName;

    public KeyBinding(string label)
    {
        _label = label;
    }

    public Key? Bound { get; private set; }

    public Key? refresh(string name)
    {
        name ??= "";
        if (name == _parsedName)
        {
            return Bound;
        }

        _parsedName = name;
        Bound = StarterUtils.parseKey(name);
        if (Bound == null && name.Trim().Length > 0)
        {
            Log.warn($"{_label}: unknown key '{name}'; the action is unbound.");
        }

        return Bound;
    }
}

// An object named by a field or, when Slot is 0 or more, the body of that player (the slot wins over the name).
// A missing slot is rescanned at most once a second and logs one warning until the body is found.
public sealed class TargetRef
{
    private const float SlotRetrySeconds = 1.0f;

    private readonly NamedObject _named;
    private string _slotUuid = "";
    private int _resolvedSlot = -1;
    private float _sinceSlotLookup;
    private bool _slotLooked;
    private bool _slotWarned;

    public TargetRef(string owner)
    {
        _named = new NamedObject(owner);
        _owner = owner;
    }

    private readonly string _owner;

    public int Slot { get; set; } = -1;

    public string Name
    {
        get => _named.Name;
        set => _named.Name = value;
    }

    // Call once per fixed tick. False (with an empty uuid) while the target cannot be found.
    public bool tryResolve(float dt, out string uuid)
    {
        if (Slot < 0)
        {
            return _named.tryResolve(dt, out uuid);
        }

        uuid = "";
        if (_resolvedSlot != Slot)
        {
            _resolvedSlot = Slot;
            _slotUuid = "";
            _slotLooked = false;
            _slotWarned = false;
        }

        if (_slotUuid.Length > 0)
        {
            if (World.tryGetPlayerController(_slotUuid, out PlayerController controller) &&
                controller.getPlayerSlot() == Slot)
            {
                uuid = _slotUuid;
                return true;
            }

            _slotUuid = "";
            _slotLooked = false;
        }

        _sinceSlotLookup += dt;
        if (_slotLooked && _sinceSlotLookup < SlotRetrySeconds)
        {
            return false;
        }

        _slotLooked = true;
        _sinceSlotLookup = 0.0f;
        _slotUuid = StarterUtils.findPlayerBody(Slot);
        if (_slotUuid.Length > 0)
        {
            _slotWarned = false;
            uuid = _slotUuid;
            return true;
        }

        if (!_slotWarned)
        {
            _slotWarned = true;
            Log.warn($"{_owner}: no player body holds slot {Slot}; it will keep looking.");
        }

        return false;
    }
}

public static class StarterUtils
{
    // One-shot lookup of an object by name: its uuid, or "" when the name is empty or nothing has it.
    public static string resolveObject(string name)
    {
        return string.IsNullOrEmpty(name) ? "" : World.findObjectByName(name);
    }

    // The uuid of the first object whose PlayerController holds this slot, or "" when none does.
    public static string findPlayerBody(int slot)
    {
        if (slot < 0)
        {
            return "";
        }

        foreach (string uuid in World.getAllObjects())
        {
            if (World.tryGetPlayerController(uuid, out PlayerController controller) &&
                controller.getPlayerSlot() == slot)
            {
                return uuid;
            }
        }

        return "";
    }

    // Case-insensitive; accepts A-Z, Space, Left, Right, Up and Down. Anything else is null.
    public static Key? parseKey(string name)
    {
        string trimmed = name?.Trim() ?? "";
        if (trimmed.Length == 0)
        {
            return null;
        }

        foreach (char c in trimmed)
        {
            if (!char.IsAsciiLetter(c))
            {
                return null;
            }
        }

        return Enum.TryParse(trimmed, true, out Key key) && Enum.IsDefined(key) ? key : null;
    }

    // The engine's Euler mapping (RotationConvention.h): degrees, composed as Rz * Ry * Rx, like glm.
    public static Quaternion eulerToQuaternion(Vector3 degrees)
    {
        const float toRadians = MathF.PI / 180.0f;
        Vector3 half = degrees * (0.5f * toRadians);
        float cx = MathF.Cos(half.X), sx = MathF.Sin(half.X);
        float cy = MathF.Cos(half.Y), sy = MathF.Sin(half.Y);
        float cz = MathF.Cos(half.Z), sz = MathF.Sin(half.Z);

        return new Quaternion(
            sx * cy * cz - cx * sy * sz,
            cx * sy * cz + sx * cy * sz,
            cx * cy * sz - sx * sy * cz,
            cx * cy * cz + sx * sy * sz);
    }

    // The inverse of eulerToQuaternion, as glm::eulerAngles gives it: degrees, pitch in x, yaw in y, roll in z.
    public static Vector3 quaternionToEuler(Quaternion q)
    {
        const float toDegrees = 180.0f / MathF.PI;

        float pitchY = 2.0f * (q.Y * q.Z + q.W * q.X);
        float pitchX = q.W * q.W - q.X * q.X - q.Y * q.Y + q.Z * q.Z;
        float pitch = MathF.Abs(pitchX) < 1.1920929e-7f && MathF.Abs(pitchY) < 1.1920929e-7f
            ? 2.0f * MathF.Atan2(q.X, q.W)
            : MathF.Atan2(pitchY, pitchX);
        float yaw = MathF.Asin(Math.Clamp(-2.0f * (q.X * q.Z - q.W * q.Y), -1.0f, 1.0f));
        float roll = MathF.Atan2(2.0f * (q.X * q.Y + q.W * q.Z), q.W * q.W + q.X * q.X - q.Y * q.Y - q.Z * q.Z);

        return new Vector3(pitch, yaw, roll) * toDegrees;
    }

    // Turns a vector by the orientation an object with these Euler degrees has.
    public static Vector3 rotate(Vector3 eulerDegrees, Vector3 v)
    {
        return Vector3.Transform(v, eulerToQuaternion(eulerDegrees));
    }

    // Fields are per second and the engine is per tick; this converts a rate for one fixed step.
    public static float perTick(float perSecond, float dt)
    {
        return perSecond * dt;
    }
}
