param(
    [Parameter(Mandatory=$true)][string]$Tools,
    [Parameter(Mandatory=$true)][string]$Version,
    [Parameter(Mandatory=$true)][string]$GradleHome
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$zip = Join-Path $Tools ("gradle-{0}-bin.zip" -f $Version)
$url = "https://services.gradle.org/distributions/gradle-$Version-bin.zip"

if (Test-Path $zip) { Remove-Item -Force $zip }
Write-Host "Downloading Gradle $Version..."
Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $zip
Expand-Archive -Path $zip -DestinationPath $Tools -Force
Remove-Item -Force $zip

if (-not (Test-Path (Join-Path $GradleHome 'bin\gradle.bat'))) {
    throw 'Gradle archive extracted, but gradle.bat was not found.'
}
