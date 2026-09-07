param(
    [Parameter(Mandatory=$true)][string]$Tools,
    [Parameter(Mandatory=$true)][string]$JdkHome
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$zip = Join-Path $Tools 'jdk17.zip'
$tmp = Join-Path $Tools 'jdk-unpack'
$url = 'https://api.adoptium.net/v3/binary/latest/17/ga/windows/x64/jdk/hotspot/normal/eclipse?project=jdk'

if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
if (Test-Path $zip) { Remove-Item -Force $zip }
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

Write-Host 'Downloading Eclipse Temurin JDK 17...'
Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $zip
Expand-Archive -Path $zip -DestinationPath $tmp -Force

$dir = Get-ChildItem -Path $tmp -Directory | Select-Object -First 1
if (-not $dir) { throw 'JDK archive layout not recognized.' }
if (Test-Path $JdkHome) { Remove-Item -Recurse -Force $JdkHome }
Move-Item -Path $dir.FullName -Destination $JdkHome

Remove-Item -Recurse -Force $tmp
Remove-Item -Force $zip

if (-not (Test-Path (Join-Path $JdkHome 'bin\java.exe'))) {
    throw 'JDK extraction completed but java.exe was not found.'
}
