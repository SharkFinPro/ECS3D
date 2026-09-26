using System.Runtime.CompilerServices;

// Exposes this assembly's internal members (Bridge.Key/MapTypeName/BuildExposedFieldsJson/ReadExposedField/
// FindExposedField/TryConvertFieldValue) to ECS3DManagedTests (source/tests/managed), so the field
// marshalling logic that does not depend on native function pointers can be covered directly.
[assembly: InternalsVisibleTo("ECS3DManagedTests")]

// Native bindings return a 1-byte C++ bool. With runtime marshalling on, a bool in a function-pointer
// signature is read as a 4-byte Win32 BOOL, so garbage in the upper bytes of the return register turns
// false into true (PlayerInput.mouseButton read as always held). Every binding signature is blittable.
[assembly: DisableRuntimeMarshalling]
