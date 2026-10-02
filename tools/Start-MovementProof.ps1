param(
    [Parameter(Mandatory)][string]$SkyrimDirectory,
    [Parameter(Mandatory)][string]$ProxyBaseFormId,
    [Parameter(Mandatory)][string]$ExpectedCellFormId,
    [float]$Scale = 1.0,
    [ValidateSet(-1, 1)][int]$YawSign = 1,
    [float]$MaxDelta = 1000.0,
    [float]$VisualOffsetX = 120.0
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$loader = Join-Path $SkyrimDirectory 'skse64_loader.exe'
$installed = Join-Path $SkyrimDirectory 'Data/SKSE/Plugins/Zelrim.dll'
$built = Join-Path $repo 'build/integrations/skse/Release/Zelrim.dll'
if (!(Test-Path -LiteralPath $loader)) { throw 'SKSE loader not found.' }
if (!(Test-Path -LiteralPath $built)) { throw 'Build the Release Zelrim DLL first.' }
if (!(Test-Path -LiteralPath $installed) -or
    (Get-FileHash -LiteralPath $built).Hash -ne (Get-FileHash -LiteralPath $installed).Hash) {
    throw 'The installed Zelrim DLL does not match this Release build. Install it while Skyrim is closed.'
}
if ($Scale -le 0 -or $MaxDelta -le 0) { throw 'Scale and MaxDelta must be positive.' }
if (Get-Process SkyrimSE -ErrorAction SilentlyContinue) { throw 'Skyrim is already running.' }
$logDirectory = Join-Path $repo 'build/movement-proof'
New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null
$env:ZELRIM_MOVEMENT_PROOF = '1'
$env:ZELRIM_PROXY_BASE_FORM_ID = $ProxyBaseFormId
$env:ZELRIM_EXPECTED_CELL_FORM_ID = $ExpectedCellFormId
$env:ZELRIM_MOVEMENT_SCALE = $Scale.ToString([Globalization.CultureInfo]::InvariantCulture)
$env:ZELRIM_MOVEMENT_YAW_SIGN = $YawSign.ToString([Globalization.CultureInfo]::InvariantCulture)
$env:ZELRIM_MOVEMENT_MAX_DELTA = $MaxDelta.ToString([Globalization.CultureInfo]::InvariantCulture)
$env:ZELRIM_PROXY_OFFSET_X = $VisualOffsetX.ToString([Globalization.CultureInfo]::InvariantCulture)
$env:ZELRIM_LOG_PATH = Join-Path $logDirectory 'ZelrimSKSE.log'
Write-Output 'Use only a disposable Skyrim save/test cell and exit without saving.'
Write-Output "Movement proof log: $env:ZELRIM_LOG_PATH"
Start-Process -FilePath $loader -WorkingDirectory $SkyrimDirectory -Wait
