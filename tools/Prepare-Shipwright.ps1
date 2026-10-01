param([string]$SourcePath = (Join-Path $PSScriptRoot '../external/Shipwright'))
$ErrorActionPreference = 'Stop'
$integrationPath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../integrations/shipwright'))
$SourcePath = (Resolve-Path -LiteralPath $SourcePath).Path
$pin = Get-Content -LiteralPath (Join-Path $integrationPath 'upstream.json') -Raw | ConvertFrom-Json
$revision = & git -C $SourcePath rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $revision -ne $pin.commit) {
    throw "Expected Shipwright revision $($pin.commit). No files changed."
}
foreach ($module in $pin.submodules.PSObject.Properties) {
    $revision = & git -C (Join-Path $SourcePath $module.Name) rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $revision -ne $module.Value) {
        throw "Submodule $($module.Name) must be at $($module.Value). No files changed."
    }
}
$patch = Join-Path $integrationPath 'heartbeat.patch'
& git -C $SourcePath apply --reverse --check $patch 2>$null
if ($LASTEXITCODE -eq 0) {
    Write-Output 'Zelrim heartbeat patch is already applied.'
    exit 0
}
& git -C $SourcePath apply --check $patch
if ($LASTEXITCODE -ne 0) { throw 'Patch does not apply cleanly. Existing files were preserved.' }
& git -C $SourcePath apply $patch
if ($LASTEXITCODE -ne 0) { throw 'Applying the heartbeat patch failed.' }
Write-Output 'Applied the Zelrim heartbeat patch.'
