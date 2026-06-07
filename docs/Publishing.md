# Publishing and GitHub Releases

The app publishes as a framework-dependent Windows x64 single-file EXE. It is
not self-contained, so the target PC must have the .NET 8 Desktop Runtime
installed.

The native VoiceMeeter/VST bridge is bundled into the single-file publish and
extracted by `.NET` at runtime. The release zip is still useful because it can
carry future extra files without changing the release flow.

## Local Publish

From the repo root:

```powershell
.\scripts\publish-release.ps1
```

In Visual Studio, use the publish profile:

```text
src\app-wpf\Properties\PublishProfiles\win-x64-framework-dependent.pubxml
```

This creates:

```text
artifacts\publish\ElkaVoiceMeeterFxHost\win-x64\Elka.VoiceMeeterFxHost.App.exe
artifacts\release\ElkaVoiceMeeterFxHost.exe
artifacts\release\ElkaVoiceMeeterFxHost-win-x64-framework-dependent.zip
```

## Upload to GitHub Release

After the GitHub repo exists and `gh auth login` has been completed:

```powershell
.\scripts\publish-release.ps1 -Tag v0.2.0 -Upload
```

The script publishes locally first, then creates the release if it does not
exist, and uploads both the zip and the standalone EXE with `--clobber`.

## Manual Publish Command

```powershell
dotnet publish .\src\app-wpf\Elka.VoiceMeeterFxHost.App.csproj `
  -c Release `
  -r win-x64 `
  --self-contained false `
  -p:PublishSingleFile=true `
  -p:IncludeNativeLibrariesForSelfExtract=true `
  -p:IncludeAllContentForSelfExtract=true
```
