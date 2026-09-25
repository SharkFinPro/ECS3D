using System.IO;

namespace ECS3DManagedTests;

// A MemoryStream that reports itself as timeout-capable and stores whatever timeout is set, so tests
// can exercise the path where TcpBackend.ReadBody applies BodyReadTimeoutMs and restores the saved value.
// A plain MemoryStream covers the path where ReadBody skips the timeout.
internal sealed class TimeoutCapableMemoryStream(byte[] buffer) : MemoryStream(buffer)
{
  public override bool CanTimeout => true;

  public override int ReadTimeout { get; set; }

  public override int WriteTimeout { get; set; }
}
