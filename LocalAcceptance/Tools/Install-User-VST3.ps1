#Requires -Version 5.1
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
function HashSha256([string]$path) {
  $stream=[IO.File]::OpenRead($path)
  $hash=[Security.Cryptography.SHA256]::Create()
  try {
    return ([BitConverter]::ToString($hash.ComputeHash($stream))).Replace('-','')
  } finally {
    $hash.Dispose()
    $stream.Dispose()
  }
}
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$source=Join-Path $root 'Plugin\125A Z-Plane FX.vst3'
$binary=Join-Path $source 'Contents\x86_64-win\125A Z-Plane FX.vst3'
if(-not (Test-Path -LiteralPath $binary -PathType Leaf)){
  Write-Host '[FAIL] Plugin-Bundle fehlt. ZIP zuerst komplett entpacken.'
  exit 2
}
Write-Host 'Bitte Fender Studio Pro / andere DAWs vorher komplett schliessen!'
$answer=Read-Host 'Installation fuer aktuellen Benutzer? Nur JA bestaetigt'
if($answer -cne 'JA'){Write-Host 'Keine Aenderung';exit 0}
$destRoot=Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3'
$dest=Join-Path $destRoot '125A Z-Plane FX.vst3'
$backup=Join-Path $root ('Backups\' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $destRoot -Force | Out-Null
if(Test-Path -LiteralPath $dest){
  New-Item -ItemType Directory -Path $backup -Force | Out-Null
  Move-Item -LiteralPath $dest -Destination (Join-Path $backup '125A Z-Plane FX.vst3')
  Write-Host ('Alte Benutzer-Version gesichert unter: '+$backup)
}
Copy-Item -LiteralPath $source -Destination $destRoot -Recurse
$installed=Join-Path $dest 'Contents\x86_64-win\125A Z-Plane FX.vst3'
if(-not (Test-Path -LiteralPath $installed -PathType Leaf)){throw 'Kopiertes VST3 fehlt'}
$hash=(HashSha256 $installed)
$original=(HashSha256 $binary)
if($hash -ine $original){throw 'Installationshash stimmt nicht'}
# Keine Bankdateien zu installieren: alle drei X3-Tabellen sind in der DLL.
Write-Host ('[PASS] 125A Z-Plane FX im VST3-Benutzerpfad: '+$dest)
Write-Host 'DAW jetzt neu starten. Kein Admin, keine globale Plugin-Installation.'
Write-Host 'Echte ASIO-Pruefung und kommerzielle Lizenz weiterhin NICHT freigegeben.'
exit 0
