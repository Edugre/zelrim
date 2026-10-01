param(
    [Parameter(Mandatory)][string]$SkyrimDirectory,
    [string]$Configuration = 'Release'
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$source = Join-Path $repo "build/integrations/skse/$Configuration/Zelrim.dll"
$destination = Join-Path $SkyrimDirectory 'Data/SKSE/Plugins/Zelrim.dll'
if (!(Test-Path -LiteralPath (Join-Path $SkyrimDirectory 'skse64_loader.exe'))) { throw 'SKSE loader not found.' }
if (!(Test-Path -LiteralPath $source)) { throw 'Build Zelrim.dll first.' }
if ((Get-Item -LiteralPath (Join-Path $SkyrimDirectory 'SkyrimSE.exe')).VersionInfo.FileVersion -ne '1.7.104.0') {
    throw 'This plugin build supports Skyrim 1.7.104 only.'
}
if (Get-Process SkyrimSE -ErrorAction SilentlyContinue) { throw 'Exit Skyrim before installing.' }
if (Test-Path -LiteralPath $destination) {
    if ((Get-FileHash -LiteralPath $source).Hash -eq (Get-FileHash -LiteralPath $destination).Hash) {
        Write-Output 'The matching Zelrim DLL is already installed.'
        exit 0
    }
    throw 'A different Zelrim.dll exists. Preserve or remove it explicitly before installing.'
}
New-Item -ItemType Directory -Force -Path (Split-Path $destination) | Out-Null
Copy-Item -LiteralPath $source -Destination $destination
Write-Output "Installed $destination"
