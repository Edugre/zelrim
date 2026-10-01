param(
    [string]$CMake = '',
    [switch]$SkipShipwright,
    [string]$OotArchive = ''
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program exited with code $LASTEXITCODE" }
}
function Ensure-Source([string]$Manifest, [string]$Destination, [bool]$Submodules) {
    $pin = Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json
    if (!(Test-Path -LiteralPath $Destination)) {
        Invoke-Checked 'git' @('clone', $pin.repository, $Destination)
        Invoke-Checked 'git' @('-C', $Destination, 'checkout', '--detach', $pin.commit)
        if ($Submodules) { Invoke-Checked 'git' @('-C', $Destination, 'submodule', 'update', '--init', '--recursive') }
    }
    $actual = & git -C $Destination rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $actual -ne $pin.commit) { throw "Unexpected revision in $Destination" }
}
if (!$CMake) {
    $command = Get-Command cmake -ErrorAction SilentlyContinue
    if ($command) { $CMake = $command.Source }
    else {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        $visualStudio = & $vswhere -latest -products * -property installationPath
        $CMake = Join-Path $visualStudio 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
    }
}
if (!(Test-Path -LiteralPath $CMake)) { throw 'CMake was not found; pass -CMake with its full path.' }
$ctest = Join-Path (Split-Path $CMake) 'ctest.exe'
Push-Location $repo
try {
    Ensure-Source 'integrations/skse/upstream.json' 'external/skse64' $false
    Invoke-Checked $CMake @('-S', '.', '-B', 'build', '-A', 'x64', '-DZELRIM_BUILD_SKSE=ON')
    Invoke-Checked $CMake @('--build', 'build', '--config', 'Release')
    Invoke-Checked $ctest @('--test-dir', 'build', '-C', 'Release', '--output-on-failure')
    if (!$SkipShipwright) {
        Ensure-Source 'integrations/shipwright/upstream.json' 'external/Shipwright' $true
        & (Join-Path $PSScriptRoot 'Prepare-Shipwright.ps1')
        if ($LASTEXITCODE -ne 0) { throw 'Preparing Shipwright failed.' }
        $adapter = Join-Path $repo 'integrations/shipwright/zelrim.cmake'
        $vcpkg = Join-Path $repo 'build/shipwright-vs18/vcpkg'
        Invoke-Checked $CMake @('-S', 'external/Shipwright', '-B', 'build/shipwright-vs18',
            '-G', 'Visual Studio 18 2026', '-A', 'x64', "-DZELRIM_INTEGRATION_FILE=$adapter", "-DVCPKG_ROOT=$vcpkg")
        Invoke-Checked $CMake @('--build', 'build/shipwright-vs18', '--config', 'Release', '--target', 'soh', 'GenerateSohOtr', '--parallel', '6')
        $runtime = Join-Path $repo 'build/runtime-shipwright'
        New-Item -ItemType Directory -Force -Path $runtime | Out-Null
        Copy-Item -LiteralPath 'external/Shipwright/x64/Release/soh.exe' -Destination $runtime
        Copy-Item -LiteralPath 'build/shipwright-vs18/soh/soh.o2r' -Destination $runtime
        # Copy contents into an existing assets directory without creating assets/assets.
        $assets = Join-Path $runtime 'assets'
        New-Item -ItemType Directory -Force -Path $assets | Out-Null
        Get-ChildItem -LiteralPath 'external/Shipwright/x64/Release/assets' | ForEach-Object {
            Copy-Item -LiteralPath $_.FullName -Destination $assets -Recurse -Force
        }
        if ($OotArchive) { Copy-Item -LiteralPath $OotArchive -Destination (Join-Path $runtime 'oot.o2r') }
        Write-Output "Development game staged in $runtime"
    }
} finally { Pop-Location }
