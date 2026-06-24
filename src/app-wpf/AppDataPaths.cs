using System.IO;

namespace Elka.VoiceMeeterFxHost.App;

internal static class AppDataPaths
{
    private const string CompanyFolder = "ElkaSoft";
    private const string AppFolder = "VoiceMeeterFxHost";

    public static string RoamingRoot => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
        CompanyFolder,
        AppFolder);

    public static string LocalRoot => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        CompanyFolder,
        AppFolder);

    public static string LegacyLocalRoot => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "ElkaVoiceMeeterFxHost");
}