using System.IO;
using System.Windows;
using System.Windows.Threading;

namespace Elka.VoiceMeeterFxHost.App;

public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        StartupCrashLogger.InstallDispatcherHandler(this);
        base.OnStartup(e);
    }
}

internal static class StartupCrashLogger
{
    public static void InstallProcessHandlers()
    {
        AppDomain.CurrentDomain.UnhandledException += (_, args) =>
        {
            if (args.ExceptionObject is Exception ex)
            {
                Write(ex);
            }
        };

        TaskScheduler.UnobservedTaskException += (_, args) =>
        {
            Write(args.Exception);
        };
    }

    public static void InstallDispatcherHandler(Application app)
    {
        app.DispatcherUnhandledException += (_, args) =>
        {
            Write(args.Exception);
            ShowStartupError(args.Exception);
            args.Handled = true;
            app.Shutdown(-1);
        };
    }

    public static void Write(Exception exception)
    {
        try
        {
            Directory.CreateDirectory(CrashLogDirectory);
            File.AppendAllText(
                CrashLogPath,
                $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss}] {exception}\n\n");
        }
        catch
        {
            // Crash logging must never create another startup failure.
        }
    }

    public static void ShowStartupError(Exception exception)
    {
        try
        {
            MessageBox.Show(
                $"Elka VoiceMeeter FX Host could not start cleanly.\n\n{exception.Message}\n\nCrash log:\n{CrashLogPath}",
                "Elka VoiceMeeter FX Host",
                MessageBoxButton.OK,
                MessageBoxImage.Error);
        }
        catch
        {
            // If the UI subsystem is the failing part, the log file is still enough.
        }
    }

    public static string CrashLogDirectory =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "ElkaVoiceMeeterFxHost");

    public static string CrashLogPath => Path.Combine(CrashLogDirectory, "startup-crash.log");
}
