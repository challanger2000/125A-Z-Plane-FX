#Requires -Version 5.1
param([switch]$CI)
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
$report=Join-Path $root ('Reports\' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $report -Force | Out-Null
$plugin=Join-Path $root 'Plugin\125A Z-Plane FX.vst3'
$dll=Join-Path $plugin 'Contents\x86_64-win\125A Z-Plane FX.vst3'
$lab=Join-Path $root 'TestLab'
$native=Join-Path $root 'Native'
$script:items=New-Object 'System.Collections.Generic.List[object]'
$script:warnings=New-Object 'System.Collections.Generic.List[string]'
$script:fails=0
function Verdict([string]$name,[string]$status,[string]$detail,[bool]$must) {
  $script:items.Add([pscustomobject]@{name=$name;status=$status;detail=$detail;required=$must})
  if($must -and $status -eq 'FAIL'){$script:fails++}
  Write-Host ('[{0}] {1}: {2}' -f $status,$name,$detail)
}
function RunExe([string]$label,[string]$file,[string[]]$argv,[string]$logname,[bool]$required) {
  if(-not (Test-Path -LiteralPath $file -PathType Leaf)){
    Verdict $label 'FAIL' 'EXE fehlt' $required; return $false
  }
  $log=Join-Path $report $logname
  try {
    & $file @argv 2>&1 | Out-File -LiteralPath $log -Encoding UTF8
    $code=$LASTEXITCODE
    if($code -ne 0){Verdict $label 'FAIL' ("Exit=$code, Log=$logname") $required;return $false}
    Verdict $label 'PASS' ("Exit=0, Log=$logname") $required;return $true
  } catch {
    $_ | Out-String | Out-File -LiteralPath $log -Encoding UTF8
    Verdict $label 'FAIL' ('Exception, Log='+$logname) $required
    return $false
  }
}
function ReadTsv([string]$file) {
  $d=@{}
  if(-not (Test-Path -LiteralPath $file)){return $d}
  foreach($line in (Get-Content -LiteralPath $file)){
    $pair=$line -split [char]9,2
    if($pair.Count -eq 2){$d[$pair[0]]=$pair[1]}
  }
  return $d
}
function GenerateWave([string]$destination) {
  $rate=44100;$frames=3*$rate
  $writer=New-Object IO.BinaryWriter([IO.File]::Create($destination))
  try {
    $writer.Write([Text.Encoding]::ASCII.GetBytes('RIFF'))
    $writer.Write([int](36+6*$frames))
    $writer.Write([Text.Encoding]::ASCII.GetBytes('WAVEfmt '))
    $writer.Write([int]16);$writer.Write([int16]1);$writer.Write([int16]2)
    $writer.Write([int]$rate);$writer.Write([int]($rate*6))
    $writer.Write([int16]6);$writer.Write([int16]24)
    $writer.Write([Text.Encoding]::ASCII.GetBytes('data'))
    $writer.Write([int]($frames*6))
    for($i=0;$i -lt $frames;$i++){
      $time=[double]$i/$rate
      $l=.12*[Math]::Sin(2*[Math]::PI*110*$time)+.06*[Math]::Sin(2*[Math]::PI*880*$time)
      $r=.10*[Math]::Sin(2*[Math]::PI*220*$time)-.045*[Math]::Sin(2*[Math]::PI*1320*$time)
      if(($i % 11025) -eq 0){$l+=.25;$r-=.20}
      foreach($value in @($l,$r)){
        $q=[int][Math]::Round([Math]::Max(-1,[Math]::Min(1,$value))*8388607)
        $writer.Write([byte]($q -band 255))
        $writer.Write([byte](($q -shr 8) -band 255))
        $writer.Write([byte](($q -shr 16) -band 255))
      }
    }
  } finally {$writer.Close()}
}
Write-Host '125A Z-Plane FX: Windows x64 LOCAL QA, KEIN Hardware-ASIO-Test'
Write-Host ('Ergebnisse: '+$report)
$manifest=Join-Path $root 'SHA256-MANIFEST.json'
try {
  if(-not (Test-Path -LiteralPath $manifest)){throw 'Manifest fehlt'}
  $list=Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
  $misses=0
  foreach($x in @($list.files)){
    $file=Join-Path $root ([string]$x.path)
    if(-not (Test-Path -LiteralPath $file -PathType Leaf)){$misses++;continue}
    if((HashSha256 $file) -ine [string]$x.sha256){$misses++}
  }
  if($misses -ne 0 -or @($list.files).Count -lt 7){throw "$misses falsche/fehlende Dateien"}
  Verdict 'SHA256 Package Integrity' 'PASS' ("$(@($list.files).Count) Dateihashes korrekt") $true
} catch {Verdict 'SHA256 Package Integrity' 'FAIL' $_.Exception.Message $true}
if(Test-Path -LiteralPath $dll -PathType Leaf){
  if((Get-Item -LiteralPath $dll).Length -gt 100000){
    Verdict 'VST3 x64 Bundle' 'PASS' 'Binaerdatei vorhanden' $true
  }else{Verdict 'VST3 x64 Bundle' 'FAIL' 'Datei unplausibel klein' $true}
}else{Verdict 'VST3 x64 Bundle' 'FAIL' 'VST3 fehlt' $true}
try {
  $sound=@(Get-CimInstance Win32_SoundDevice | Select-Object -ExpandProperty Name)
} catch {$sound=@();$script:warnings.Add('Windows SoundDevice-Inventar nicht verfuegbar')}
try {
  if(Test-Path 'HKLM:\SOFTWARE\ASIO'){
    $asio=@(Get-ChildItem 'HKLM:\SOFTWARE\ASIO' | ForEach-Object {$_.PSChildName})
  }else{$asio=@()}
} catch {$asio=@();$script:warnings.Add('ASIO-Registrierung nicht lesbar')}
[pscustomobject]@{
  os=[Environment]::OSVersion.VersionString
  x64=[Environment]::Is64BitOperatingSystem
  local_test_time_utc=(Get-Date).ToUniversalTime().ToString('o')
  sound_devices=$sound
  registered_asio_drivers=$asio
  note='Registry inventory only. NO physical ASIO streaming was executed.'
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $report 'WINDOWS-ASIO-INVENTORY.json') -Encoding UTF8

if(Test-Path -LiteralPath $dll){
  [void](RunExe 'VST3 Host Inventar' (Join-Path $lab '125A_Test_Lab_Host.exe') @('--inspect',$plugin) 'host-inventory.log' $true)
  [void](RunExe 'GUI Parameter Inventar' (Join-Path $lab '125A_Test_Lab_GUI.exe') @('--inventory-parameters',$plugin,(Join-Path $report 'PARAMETERS.tsv')) 'gui-params.log' $true)
  [void](RunExe 'GUI Parameter Sweep' (Join-Path $lab '125A_Test_Lab_GUI.exe') @('--sweep-parameters',$plugin,(Join-Path $report 'PARAMETER-SWEEP.tsv')) 'gui-sweep.log' $true)
  [void](RunExe 'GUI Screenshot' (Join-Path $lab '125A_Test_Lab_GUI.exe') @('--snapshot-editor',$plugin,(Join-Path $report 'VSTGUI-SCREENSHOT.bmp')) 'gui-screenshot.log' $true)
  $stress=Join-Path $lab '125A_Test_Lab_State_Stress.exe'
  foreach($rate in @(44100,48000,96000)){
    foreach($frames in @(64,512)){
      $id="$rate-$frames";$tsv=Join-Path $report ("P25-$id.tsv")
      $ok=RunExe "P25 Audio/State $id" $stress @('--stress',$plugin,$tsv,[string]$rate,[string]$frames,'2500','none','--save-only') ("P25-$id.log") $true
      if($ok){
        try {
          $d=ReadTsv $tsv
          $pass=$d['result'] -eq 'PASS' -and $d['process_failures'] -eq '0' -and $d['save_success'] -eq '500' -and $d['input_buses'] -eq '1' -and [int]$d['saves_during_playback'] -gt 0
          if($pass){Verdict "P25 PCM/State Evidence $id" 'PASS' 'Stereoeingang, 500 Saves, keine DSP-Fehler' $true}
          else{Verdict "P25 PCM/State Evidence $id" 'FAIL' 'P25-Bericht ungueltig' $true}
          if($d.ContainsKey('callback_overruns') -and [int]$d['callback_overruns'] -gt 0){
            $script:warnings.Add("P25 $id : $($d['callback_overruns']) gemessene Callback-Deadlinefehler. Das ist KEINE ASIO-Zertifizierung.")
          }
        }catch{Verdict "P25 PCM/State Evidence $id" 'FAIL' $_.Exception.Message $true}
      }
    }
  }
}
[void](RunExe 'Core DSP Unit' (Join-Path $native 'zplane_core_tests.exe') @() 'core.log' $true)
[void](RunExe 'Automation Unit' (Join-Path $native 'zplane_automation_tests.exe') @() 'automation.log' $true)
[void](RunExe 'Morpheus 289 Cubes Unit' (Join-Path $native 'zplane_morpheus_tests.exe') @() 'morpheus-cubes.log' $true)
[void](RunExe 'Morpheus Independent 288 Filter Oracle' (Join-Path $native 'zplane_morpheus_oracle_tests.exe') @() 'morpheus-reference-288.log' $true)
[void](RunExe 'X3 Synthetic Fault Recovery' (Join-Path $native 'zplane_x3_adapter_tests.exe') @() 'x3-synthetic.log' $true)
# Always test the self-contained VST3, independently of optional reference files.
if(Test-Path -LiteralPath $dll -PathType Leaf){
  [void](RunExe 'X3 Embedded 55 Modes (No Banks)' (Join-Path $native 'zplane_native_host_tests.exe') @($dll,'--embedded-smoke') 'x3-embedded-55-modes.log' $true)
}
$bankdir=''
$locations=@((Join-Path $root 'Banks'),(Join-Path $env:LOCALAPPDATA '125A\ZPlaneFX\Banks'))
if($env:ZPLANE_X3_BANK_DIR){$locations=@($env:ZPLANE_X3_BANK_DIR)+$locations}
foreach($candidate in $locations){
  $valid=$true
  foreach($file in @('factory-bank.bin','standard-bank.bin','morph-support.bin')){
    $p=Join-Path $candidate $file
    if(-not (Test-Path -LiteralPath $p -PathType Leaf) -or (Get-Item -LiteralPath $p).Length -le 0){$valid=$false;break}
  }
  if($valid){$bankdir=$candidate;break}
}
if(-not $bankdir){
  Verdict 'X3 Separate External Reference Oracle' 'NOT RUN' 'Optionale separate Referenzdateien fehlen; eingebauter X3-Modus separat geprueft' $false
  $script:warnings.Add('Der eingebaute X3-Filtersatz wurde geprueft. Nur unabhaengige Forschungsreferenzen sind NOT RUN.')
}else{
  $env:ZPLANE_X3_BANK_DIR=$bankdir
  Verdict 'X3 Bank Lookup' 'PASS' 'Dateien vorhanden, werden von nativen Tests validiert' $false
  [void](RunExe 'X3 Originalbank PCM' (Join-Path $native 'zplane_x3_real_adapter_tests.exe') @() 'x3-original.log' $true)
  [void](RunExe 'X3 RT/Overflow' (Join-Path $native 'zplane_x3_realtime_audit_tests.exe') @() 'x3-realtime.log' $true)
  [void](RunExe 'Actual VST3 2640 PCM + GUI + Automation' (Join-Path $native 'zplane_native_host_tests.exe') @($dll) 'x3-vst3-host.log' $true)
}
try {
  $wave=Join-Path $report '125A-DAW-REFERENCE-44100-24bit-stereo.wav'
  GenerateWave $wave
  if((Get-Item -LiteralPath $wave).Length -lt 100000){throw 'WAV leer'}
  Verdict 'Original DAW Stereowave 24-bit' 'PASS' '3s PCM erzeugt, kein E-MU-Referenzklang' $true
}catch{Verdict 'Original DAW Stereowave 24-bit' 'FAIL' $_.Exception.Message $true}
$status=if($script:fails -gt 0){'FAIL'}elseif(-not $bankdir){'PARTIAL_X3_REFERENCE_NOT_RUN'}else{'PASS_TECHNICAL_NOT_ASIO'}
$obj=[pscustomobject]@{
  schema='125A-ZPLANE-LOCAL-ACCEPTANCE-v1'
  time_utc=(Get-Date).ToUniversalTime().ToString('o')
  status=$status
  python_required=$false
  actual_asio='NOT RUN'
  daw_studio_pro='NOT RUN'
  hardware_emulation='NOT VERIFIED'
  commercial_license='NOT VERIFIED'
  x3_optional_reference_banks=$(if($bankdir){'PRESENT'}else{'NOT_RUN'})
  checks=@($script:items.ToArray())
  warnings=@($script:warnings.ToArray())
}
$obj | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $report 'ACCEPTANCE.json') -Encoding UTF8
$txt=@("125A Z-Plane FX | $status","Kein physisches ASIO/ETW, keine DAW/Hardware- oder Lizenz-Freigabe.","")
$txt+=@($script:items | ForEach-Object {"$($_.status) : $($_.name) : $($_.detail)"})
$txt+=@('','WARNUNGEN:')+@($script:warnings)
$txt | Set-Content -LiteralPath (Join-Path $report 'ACCEPTANCE.txt') -Encoding UTF8
Write-Host ("ERGEBNIS: "+$status)
Write-Host ("BERICHT: "+(Join-Path $report 'ACCEPTANCE.txt'))
if(-not $CI){Start-Process explorer.exe -ArgumentList ('"'+$report+'"')}
if($script:fails -gt 0){exit 1}
exit 0
