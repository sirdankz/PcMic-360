param(
    [Parameter(Mandatory=$true)][string]$Tools,
    [Parameter(Mandatory=$true)][string]$SdkRoot,
    [Parameter(Mandatory=$true)][string]$ZipPath,
    [Parameter(Mandatory=$true)][string]$ExpectedSha
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$url = 'https://dl.google.com/android/repository/commandlinetools-win-15859902_latest.zip'
$tmp = Join-Path $Tools 'android-cli-unpack'
$dst = Join-Path $SdkRoot 'cmdline-tools\latest'

if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
if (Test-Path $ZipPath) { Remove-Item -Force $ZipPath }

Write-Host 'Downloading official Android command-line tools...'
Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $ZipPath
$actual = (Get-FileHash -Path $ZipPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actual -ne $ExpectedSha.ToLowerInvariant()) {
    throw "Android tools SHA-256 mismatch. Expected $ExpectedSha but got $actual"
}

New-Item -ItemType Directory -Force -Path $tmp | Out-Null
Expand-Archive -Path $ZipPath -DestinationPath $tmp -Force
New-Item -ItemType Directory -Force -Path $dst | Out-Null

$source = Join-Path $tmp 'cmdline-tools\*'
Copy-Item -Path $source -Destination $dst -Recurse -Force

Remove-Item -Recurse -Force $tmp
Remove-Item -Force $ZipPath

if (-not (Test-Path (Join-Path $dst 'bin\sdkmanager.bat'))) {
    throw 'Android command-line tools extracted, but sdkmanager.bat was not found.'
}
