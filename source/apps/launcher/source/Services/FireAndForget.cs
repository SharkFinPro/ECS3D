using System;
using System.Threading.Tasks;

namespace ECS3DLauncher.Services;

// Observes a task nothing awaits, so its exception reaches the error policy instead of dying in the discarded task.
public static class FireAndForget
{
    private static Action<Exception>? s_reporter;

    // The reporter may run on any thread; the caller marshals to the UI thread when it needs to.
    public static void Install(Action<Exception>? reporter) => s_reporter = reporter;

    public static void Forget(this Task task) => task.Forget(Report);

    public static void Forget(this Task task, Action<Exception> report)
    {
        if (task.IsCompletedSuccessfully)
        {
            return;
        }

        _ = task.ContinueWith(
            finished => Observe(finished, report),
            TaskContinuationOptions.NotOnRanToCompletion | TaskContinuationOptions.ExecuteSynchronously,
            TaskScheduler.Default);
    }

    private static void Observe(Task finished, Action<Exception> report)
    {
        if (finished.IsCanceled || finished.Exception is not { } aggregate)
        {
            return;
        }

        var error = aggregate.InnerExceptions.Count == 1 ? aggregate.InnerExceptions[0] : aggregate;
        try
        {
            report(error);
        }
        catch (Exception)
        {
            // A failing reporter must not turn into a new unobserved fault.
        }
    }

    private static void Report(Exception exception) => s_reporter?.Invoke(exception);
}
