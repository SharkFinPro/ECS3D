using System.Runtime.CompilerServices;

// Exposes this assembly's internal members (Bridge.Key/MapTypeName/BuildExposedFieldsJson/ReadExposedField/
// FindExposedField/TryConvertFieldValue) to ECS3DManagedTests (source/tests/managed), so the field
// marshalling logic that does not depend on native function pointers can be covered directly.
[assembly: InternalsVisibleTo("ECS3DManagedTests")]
