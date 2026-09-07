param(
    [Parameter(Mandatory=$true)][string]$SdkManager,
    [Parameter(Mandatory=$true)][string]$SdkRoot
)
$ErrorActionPreference = 'Stop'

if (-not (Test-Path $SdkManager)) {
    throw "sdkmanager.bat not found: $SdkManager"
}

# sdkmanager may ask multiple yes/no questions. Feed a generous number of y lines.
$answers = 1..100 | ForEach-Object { 'y' }
$answers | & $SdkManager "--sdk_root=$SdkRoot" --licenses
if ($LASTEXITCODE -ne 0) {
    throw "sdkmanager --licenses failed with exit code $LASTEXITCODE"
}
