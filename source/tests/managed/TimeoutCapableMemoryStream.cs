using System.IO;

namespace ECS3DManagedTests;

// TcpBackend.ReadBody (called from ReadFrame for any body longer than zero bytes) unconditionally reads
// stream.ReadTimeout before the read loop and writes it twice - once to apply BodyReadTimeoutMs, once in
// a finally block to restore the saved value - without checking stream.CanTimeout first. A plain
// MemoryStream reports CanTimeout = false and throws InvalidOperationException from both the getter and
// the setter, so it cannot stand in for ReadFrame's Stream parameter as-is. This subclass only adds
// timeout bookkeeping (reporting itself as timeout-capable and storing whatever value is set) on top of
// MemoryStream's own Read/Write behavior, which needs no changes, so these tests can drive ReadFrame off
// an in-memory buffer without touching TcpBackend. Every real caller passes a NetworkStream (a
// TcpClient's GetStream()), which does support timeouts, so this gap in ReadBody has not been reachable
// in production - but it would throw the same way for any future non-socket Stream, since
// ReadBody never checks CanTimeout before touching ReadTimeout.
internal sealed class TimeoutCapableMemoryStream(byte[] buffer) : MemoryStream(buffer)
{
  public override bool CanTimeout => true;

  public override int ReadTimeout { get; set; }

  public override int WriteTimeout { get; set; }
}
