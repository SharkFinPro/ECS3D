using System.Numerics;
using System.Text.Json;
using ScriptBridge;
using Xunit;

namespace ECS3DManagedTests;

// Bridge's [UnmanagedCallersOnly] entry points marshal IntPtr strings and read/write _instances, but the
// reflection/JSON/conversion logic underneath does not touch native function pointers or the CLR host.
// Bridge.AssemblyInfo's InternalsVisibleTo exposes that logic (Key, MapTypeName, BuildExposedFieldsJson,
// FindExposedField/ReadExposedField, TryConvertFieldValue) here so it is covered directly instead of
// through a real script attach, which would need a compiled user assembly and a running CLR host.
public class BridgeFieldMarshallingTests
{
  // A plain object (not a ScriptBase subclass) is enough: BuildExposedFieldsJson/FindExposedField only
  // reflect over the instance's own fields, so this avoids needing ScriptBase.initComponents (which
  // constructs Transform/RigidBody/Camera/PlayerInput wrappers - safe to construct, but unnecessary here).
  private class SampleFields
  {
    [ExposeToEditor("Speed")]
    public float speed = 3.5f;

    [ExposeToEditor]
    public int count = 7;

    [ExposeToEditor("Enabled")]
    public bool enabled = true;

    [ExposeToEditor("Offset")]
    public Vector3 offset = new(1f, 2f, 3f);

    [ExposeToEditor("Label")]
    public string label = "hello";

    // Not [ExposeToEditor]: must never appear in the exposed-fields JSON or be reachable by name.
    public float notExposed = 99f;

    // [ExposeToEditor] but of an unsupported type (MapTypeName has no case for double): must be skipped
    // by BuildExposedFieldsJson even though the attribute is present.
    [ExposeToEditor("Unsupported")]
    public double unsupported = 1.0;
  }

  [Theory]
  [InlineData(typeof(float), "float")]
  [InlineData(typeof(int), "int")]
  [InlineData(typeof(bool), "bool")]
  [InlineData(typeof(string), "string")]
  [InlineData(typeof(Vector3), "vector3")]
  public void MapTypeName_MapsEverySupportedType(System.Type clrType, string expected)
  {
    Assert.Equal(expected, Bridge.MapTypeName(clrType));
  }

  [Fact]
  public void MapTypeName_ReturnsNullForAnUnsupportedType()
  {
    // Positive control is MapTypeName_MapsEverySupportedType above: the mapped types round-trip, an
    // arbitrary unmapped one (double) does not.
    Assert.Null(Bridge.MapTypeName(typeof(double)));
  }

  [Fact]
  public void Key_JoinsUuidAndClassNameWithUnderscore()
  {
    Assert.Equal("abc-123_PlayerScript", Bridge.Key("abc-123", "PlayerScript"));
  }

  [Fact]
  public void BuildExposedFieldsJson_ListsOnlySupportedExposedFields()
  {
    var json = Bridge.BuildExposedFieldsJson(new SampleFields());

    using var doc = JsonDocument.Parse(json);
    var root = doc.RootElement;

    Assert.Equal(JsonValueKind.Array, root.ValueKind);

    var names = new System.Collections.Generic.List<string>();
    foreach (var element in root.EnumerateArray())
    {
      names.Add(element.GetProperty("name").GetString()!);
    }

    // Positive control: every supported, attributed field is present...
    Assert.Contains("speed", names);
    Assert.Contains("count", names);
    Assert.Contains("enabled", names);
    Assert.Contains("offset", names);
    Assert.Contains("label", names);

    // ...while a field with no [ExposeToEditor] attribute, and one whose type MapTypeName does not cover,
    // are both left out.
    Assert.DoesNotContain("notExposed", names);
    Assert.DoesNotContain("unsupported", names);
    Assert.Equal(5, names.Count);
  }

  [Fact]
  public void BuildExposedFieldsJson_ReportsDisplayNameAndType()
  {
    var json = Bridge.BuildExposedFieldsJson(new SampleFields());
    using var doc = JsonDocument.Parse(json);

    JsonElement? speed = null;
    JsonElement? count = null;
    foreach (var element in doc.RootElement.EnumerateArray())
    {
      var name = element.GetProperty("name").GetString();
      if (name == "speed")
      {
        speed = element;
      }
      if (name == "count")
      {
        count = element;
      }
    }

    Assert.NotNull(speed);
    Assert.Equal("Speed", speed!.Value.GetProperty("displayName").GetString());
    Assert.Equal("float", speed.Value.GetProperty("type").GetString());

    // count uses the attribute's default display name ("Script Variable") since none was given.
    Assert.NotNull(count);
    Assert.Equal("Script Variable", count!.Value.GetProperty("displayName").GetString());
    Assert.Equal("int", count.Value.GetProperty("type").GetString());
  }

  [Fact]
  public void FindExposedField_FindsAnAttributedFieldByName()
  {
    var field = Bridge.FindExposedField(new SampleFields(), "speed");

    Assert.NotNull(field);
    Assert.Equal(typeof(float), field!.FieldType);
  }

  [Fact]
  public void FindExposedField_RefusesAFieldWithNoAttribute()
  {
    // Positive control is FindExposedField_FindsAnAttributedFieldByName above: an attributed field of the
    // same instance is found, an unattributed one by name is not, even though it exists on the type.
    Assert.Null(Bridge.FindExposedField(new SampleFields(), "notExposed"));
  }

  [Fact]
  public void FindExposedField_RefusesAnUnknownFieldName()
  {
    Assert.Null(Bridge.FindExposedField(new SampleFields(), "doesNotExist"));
  }

  [Fact]
  public void ReadExposedField_ReadsTheCurrentValue()
  {
    var instance = new SampleFields { speed = 9.25f };

    Assert.Equal(9.25f, Bridge.ReadExposedField(instance, "speed"));
    Assert.Equal(new Vector3(1f, 2f, 3f), Bridge.ReadExposedField(instance, "offset"));
  }

  [Fact]
  public void ReadExposedField_ReturnsNullForAFieldWithNoAttribute()
  {
    // Positive control is ReadExposedField_ReadsTheCurrentValue above.
    Assert.Null(Bridge.ReadExposedField(new SampleFields(), "notExposed"));
  }

  [Fact]
  public void TryConvertFieldValue_AssignsAMatchingStructTypeDirectly()
  {
    var ok = Bridge.TryConvertFieldValue(typeof(Vector3), new Vector3(4f, 5f, 6f), out var converted);

    Assert.True(ok);
    Assert.Equal(new Vector3(4f, 5f, 6f), converted);
  }

  [Fact]
  public void TryConvertFieldValue_ConvertsBetweenConvertibleTypes()
  {
    // Exercises the widening conversion the native side relies on when a whole number arrives for a
    // float field, the same path setFieldFloat feeds through.
    var ok = Bridge.TryConvertFieldValue(typeof(float), 3, out var converted);

    Assert.True(ok);
    Assert.Equal(3f, converted);
  }

  [Fact]
  public void TryConvertFieldValue_RefusesAMismatchedTypeInsteadOfThrowing()
  {
    // Positive control is TryConvertFieldValue_ConvertsBetweenConvertibleTypes above: a genuinely
    // convertible pair succeeds, a struct that Convert.ChangeType cannot coerce (Vector3 into a float
    // field) is refused cleanly rather than throwing InvalidCastException out of the caller.
    var ok = Bridge.TryConvertFieldValue(typeof(float), new Vector3(1f, 2f, 3f), out var converted);

    Assert.False(ok);
    Assert.Null(converted);
  }
}
