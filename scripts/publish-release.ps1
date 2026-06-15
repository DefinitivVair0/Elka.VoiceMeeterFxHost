[CmdletBinding()]
param(
    [string]$Configuration = "Release",
    [string]$Runtime = "win-x64",
    [string]$Tag = "",
    [switch]$Upload
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$project = Join-Path $repoRoot "src\app-wpf\Elka.VoiceMeeterFxHost.App.csproj"
$publishDir = Join-Path $repoRoot "artifacts\publish\ElkaVoiceMeeterFxHost\$Runtime"
$singleFileDir = Join-Path $repoRoot "artifacts\publish\ElkaVoiceMeeterFxHost\$Runtime-singlefile"
$releaseDir = Join-Path $repoRoot "artifacts\release"
$publishExe = Join-Path $publishDir "Elka.VoiceMeeterFxHost.App.exe"
$singleFileExe = Join-Path $singleFileDir "Elka.VoiceMeeterFxHost.App.exe"
$releaseExe = Join-Path $releaseDir "ElkaVoiceMeeterFxHost.exe"
$zipPath = Join-Path $releaseDir "ElkaVoiceMeeterFxHost-$Runtime-framework-dependent.zip"

if (Test-Path $publishDir) {
    Remove-Item -LiteralPath $publishDir -Recurse -Force
}

if (Test-Path $singleFileDir) {
    Remove-Item -LiteralPath $singleFileDir -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $publishDir, $singleFileDir, $releaseDir | Out-Null

dotnet publish $project `
    -c $Configuration `
    -r $Runtime `
    --self-contained false `
    -o $publishDir `
    -p:PublishSingleFile=false `
    -p:IncludeNativeLibrariesForSelfExtract=true `
    -p:IncludeAllContentForSelfExtract=true

if ($LASTEXITCODE -ne 0) {
    throw "dotnet publish failed with exit code $LASTEXITCODE"
}

if (!(Test-Path $publishExe)) {
    throw "Publish did not create $publishExe"
}

dotnet publish $project `
    -c $Configuration `
    -r $Runtime `
    --self-contained false `
    -o $singleFileDir `
    -p:PublishSingleFile=true `
    -p:IncludeNativeLibrariesForSelfExtract=true `
    -p:IncludeAllContentForSelfExtract=true `
    -p:ElkaCreateReleaseArtifacts=false `
    -p:ElkaUploadGitHubRelease=false

if ($LASTEXITCODE -ne 0) {
    throw "single-file dotnet publish failed with exit code $LASTEXITCODE"
}

if (!(Test-Path $singleFileExe)) {
    throw "Single-file publish did not create $singleFileExe"
}

$releaseExeLength = (Get-Item -LiteralPath $singleFileExe).Length
if ($releaseExeLength -lt 1000000 -or $releaseExeLength -gt 50000000) {
    throw "Release EXE size $releaseExeLength is outside the expected framework-dependent single-file range."
}
Copy-Item -LiteralPath $singleFileExe -Destination $releaseExe -Force

if (Test-Path $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}

Compress-Archive -Path (Join-Path $publishDir "*") -DestinationPath $zipPath -Force

Write-Host "Published:"
Write-Host "  $publishExe"
Write-Host "  $singleFileExe"
Write-Host "  $releaseExe"
Write-Host "  $zipPath"

if ($Upload) {
    if ([string]::IsNullOrWhiteSpace($Tag)) {
        throw "Pass -Tag vX.Y.Z when using -Upload."
    }

    $gh = Get-Command gh -ErrorAction SilentlyContinue
    if ($null -eq $gh) {
        throw "GitHub CLI was not found. Install gh or upload the files manually from artifacts\release."
    }

    gh release view $Tag *> $null
    if ($LASTEXITCODE -ne 0) {
        gh release create $Tag --title $Tag --notes "Elka VoiceMeeter FX Host $Tag"
    }

    gh release upload $Tag $zipPath $releaseExe --clobber
}
