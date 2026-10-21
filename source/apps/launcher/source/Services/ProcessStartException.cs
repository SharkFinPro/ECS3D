using System;

namespace ECS3DLauncher.Services;

// The one failure IProcessRunner.Start reports; callers turn it into an error state.
public sealed class ProcessStartException : Exception
{
    public ProcessStartException(string message) : base(message)
    {
    }

    public ProcessStartException(string message, Exception innerException) : base(message, innerException)
    {
    }
}
