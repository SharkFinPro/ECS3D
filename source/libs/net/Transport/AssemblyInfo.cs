using System.Runtime.CompilerServices;

// Exposes this assembly's internal members (TcpBackend.FrameBytes/ReadFrame,
// WebSocketBackend.ComputeAcceptKey) to ECS3DManagedTests (source/tests/managed), so pure wire-format
// logic can be covered directly instead of through reflection or a real socket.
[assembly: InternalsVisibleTo("ECS3DManagedTests")]
