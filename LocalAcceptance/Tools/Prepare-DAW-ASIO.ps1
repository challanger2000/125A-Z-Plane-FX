#Requires -Version 5.1
param([switch]$CI)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
# Bridge to existing 125A Plugin Test Lab; NO audio replay or physical ASIO here.
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$reportRoot=Join-Path $root 'Reports'
$out=Join-Path $reportRoot ('DAW-ASIO-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $out -Force | Out-Null

$names=@()
try {
 if(Test-Path 'HKLM:\SOFTWARE\ASIO'){
  $names=@(Get-ChildItem 'HKLM:\SOFTWARE\ASIO' | ForEach-Object {$_.PSChildName})
 }
}catch {}
$names | Set-Content -LiteralPath (Join-Path $out 'REGISTERED-ASIO-DRIVERS.txt') -Encoding UTF8

$integrity='NOT VERIFIED'
$technical='NOT VERIFIED'
$reason='Run 00-START-TEST.bat first; no matching Test Lab evidence.'
$sourceCommit='';$labCommit='';$manifestFingerprint='';$technicalReport=''
$x3Oracle='NOT RUN'
try {
 $manifestPath=Join-Path $root 'SHA256-MANIFEST.json'
 if(-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)){
  throw 'SHA256 manifest missing'
 }
 $manifest=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
 $manifestFingerprint=(Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash
 $sourceCommit=[string]$manifest.plugin_source_commit
 $labCommit=[string]$manifest.test_lab_source_commit
 if($sourceCommit -notmatch '^[0-9a-fA-F]{40}$' -or
    $labCommit -notmatch '^[0-9a-fA-F]{40}$'){
  throw 'Invalid source or Test Lab revision in package manifest'
 }
 if(@($manifest.files).Count -lt 7){throw 'Incomplete package manifest'}
 $prefix=$root+[IO.Path]::DirectorySeparatorChar
 foreach($item in @($manifest.files)){
  $name=[string]$item.path
  if([string]::IsNullOrWhiteSpace($name)){throw 'Empty manifest path'}
  $absolute=[IO.Path]::GetFullPath((Join-Path $root $name))
  if(-not $absolute.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){
   throw ('Manifest path escapes package: '+$name)
  }
  if(-not (Test-Path -LiteralPath $absolute -PathType Leaf)){
   throw ('Missing package file: '+$name)
  }
  if((Get-FileHash -LiteralPath $absolute -Algorithm SHA256).Hash -ine [string]$item.sha256){
   throw ('Modified package file: '+$name)
  }
 }
 $integrity='PASS'
 # We READ the existing native Test Lab report; do not recreate its tests.
 # Stale/mismatching reports cannot make this package look verified.
 $mandatory=@('SHA256 Package Integrity','VST3 Host Inventar',
  'GUI Parameter Inventar','GUI Parameter Sweep','GUI Screenshot',
  'P25 PCM/State Evidence 48000-512',
  'Morpheus Independent 288 Filter Oracle',
  'Filter Display Analytic Accuracy','X3 Embedded 55 Modes (No Banks)')
 $existing=@(Get-ChildItem -LiteralPath $reportRoot -Recurse -File -Filter 'ACCEPTANCE.json' |
   Sort-Object LastWriteTime -Descending)
 foreach($candidate in $existing){
  $a=$null
  try {$a=Get-Content -LiteralPath $candidate.FullName -Raw | ConvertFrom-Json}
  catch {continue}
  if($null -eq $a -or -not $a.PSObject.Properties['package_manifest_sha256'] -or
     -not $a.PSObject.Properties['plugin_source_commit'] -or
     -not $a.PSObject.Properties['test_lab_source_commit']){continue}
  if([string]$a.package_manifest_sha256 -ine $manifestFingerprint -or
     [string]$a.plugin_source_commit -ine $sourceCommit -or
     [string]$a.test_lab_source_commit -ine $labCommit){continue}
  if([string]$a.status -notin @('PARTIAL_X3_REFERENCE_NOT_RUN',
      'PASS_TECHNICAL_NOT_ASIO')){continue}
  if([string]$a.actual_asio -ne 'NOT RUN' -or
     [string]$a.daw_studio_pro -ne 'NOT RUN'){continue}
  $checks=@{}
  foreach($item in @($a.checks)){$checks[[string]$item.name]=[string]$item.status}
  $adequate=$true
  foreach($name in $mandatory){
   if(-not $checks.ContainsKey($name) -or $checks[$name] -ne 'PASS'){
    $adequate=$false;break
   }
  }
  foreach($item in @($a.checks)){
   if($item.required -and $item.status -ne 'PASS'){$adequate=$false;break}
  }
  if(-not $adequate){continue}
  $technical='PASS'
  $reason='Existing 125A Test Lab technical baseline matches this package, NOT DAW/ASIO.'
  $technicalReport=$candidate.FullName.Substring($root.Length+1)
  if($a.status -eq 'PASS_TECHNICAL_NOT_ASIO'){$x3Oracle='PASS'}
  break
 }
}catch{
 $integrity='FAIL'
 $technical='NOT VERIFIED'
 $reason=$_.Exception.Message
}

[pscustomobject]@{
 schema='125A-ZPLANE-DAW-PREFLIGHT-v1'
 generated_utc=(Get-Date).ToUniversalTime().ToString('o')
 plugin_source_commit=$sourceCommit
 test_lab_source_commit=$labCommit
 package_manifest_sha256=$manifestFingerprint
 package_integrity=$integrity
 universal_test_lab_technical=$technical
 technical_reason=$reason
 technical_report=$technicalReport
 x3_embedded=$(if($technical -eq 'PASS'){'PASS'}else{'NOT VERIFIED'})
 x3_optional_external_reference=$x3Oracle
 asio_registry_driver_names=$names
 physical_asio='NOT RUN'
 fender_studio_pro='NOT RUN'
 visual_gui='NOT RUN'
 musical_listening='NOT RUN'
 release_approved=$false
} | ConvertTo-Json -Depth 6 |
 Set-Content -LiteralPath (Join-Path $out 'DAW-ASIO-PREFLIGHT.json') -Encoding UTF8

$headText=@(
 '125A Z-Plane FX - Fender Studio Pro PRAKTISCHE ABNAHME'
 '======================================================='
 'Prototype Windows x64 VST3, keine Releasefreigabe.'
 ('Paketintegritaet: '+$integrity)
 ('125A Test Lab (automatisierte Technik): '+$technical)
 ('Passender Lab-Bericht: '+$technicalReport)
 ('Hinweis: '+$reason)
 ('Registrierte ASIO-Treiber: '+$names.Count+' (Registry, kein Audiotest)')
 ''
)
$manual=@'
Alle folgenden Punkte sind NOT RUN, bis sie am eigenen Rechner
durchgefuehrt und tatsaechlich bewertet wurden. Das Test Lab erledigt
die automatischen VST3-, GUI-, PCM- und State-Pruefungen bereits.
Zuerst 00-START-TEST.bat ausfuehren, danach Fender Studio Pro schliessen
und bei Bedarf 01-INSTALL-PLUGIN.bat ausfuehren.
Abhoerlautstaerke zunaechst niedrig einstellen!

MUSIKALISCHER TEST
------------------
Einen echten Synth mit gehaltenen Akkorden/Pad oder Basslauf verwenden,
125A Z-Plane FX als Audio-Insert NACH dem Instrument einfuegen.
Die aus 00 erzeugte 3-Sekunden-PCM-Test-WAV ist kein Klangreferenzsong.
Ausgangspegel fuer Wet/Bypass-Vergleich angleichen.

[ ] 01 Fender Studio Pro findet das VST3; alle GUI-Elemente auf dem
       900x1050 grossen Editor sind sichtbar und bedienbar (auch bei
       vorhandener Windows-Displayskalierung), kein Abschneiden.
[ ] 02 Physischer ASIO-Interface-Treiber statt WASAPI aktiv. Treiber,
       Firmware, DAW-Version, Samplerate, Buffer und XRUNs notieren.
[ ] 03 Dry/Wet 0, Wet 100 und Bypass: keine unfreiwilligen Klang-,
       Pegel- oder Stereo-Aenderungen im neutralen Zustand.
[ ] 04 Morpheus OFF: X3-Filter 1,17,33,34,50,55 nacheinander,
       dazu Morph und Resonance/Q. KEINE externen Bankdateien noetig.
[ ] 05 Legacy-Filter LP/HP/BP/Notch, Cutoff, Resonance und Mix.
[ ] 06 Morpheus ON: Null Cube 0, Cube 1, VowelSpace (43), Cube 288;
       Morph/Frequency/Transform musikalisch spielen.
[ ] 07 Filter sollen charakteristische, nuetzliche Klangfarben
       liefern. Knackser, zu hohe Pegel oder merkwuerdige Stereo-
       Effekte mit genauem Filter/Cube und Reglerstellung notieren.

ECHTE DSP-LFO-Telemetrie UND ANIMIERTE FILTERANZEIGE
----------------------------------------------------
[ ] 08 LFO 1 ON, Shape=Sine, Target=Morph, DAW SYNC an,
       Depth ca. 40%. DAW-Transport starten und Audiomaterial spielen.
       Die effektive Kurve muss sich bewegen, die BASE-Kurve bleibt
       stationaer; Modulation muss musikalisch hoerbar sein.
[ ] 09 LFO 2 einzeln bei Target=Resonance/Q pruefen; bei LFO OFF
       und ohne neue Audioverarbeitung keine erfundene Bewegung.
[ ] 10 Zwei Plugin-Instanzen mit offenem Display/aktivem LFO:
       unabhaengige Kurven. Zehn Minuten spielen, beide Editoren
       mehrfach oeffnen/schliessen, CPU-Ausreisser/Dropouts notieren.

PROJEKT UND INTERFACE
---------------------
[ ] 11 Automationen aufnehmen, Projekt speichern, DAW schliessen,
       wieder oeffnen: Filter, LFO, Werte und Klang wiederhergestellt?
[ ] 12 Sofern vom echten Interface unterstuetzt: 44,1/48/96 kHz
       mit 64/128/256/512 Samples testen, keine XRUNs/dropouts.
       Beginnen mit 48 kHz / 256 Samples.

DAW/Version: __________________ Windows-Displayskalierung: ______
ASIO-Interface/Treiber/Version: _________________________________
Samplerate/Buffer: _______________ Synth/Preset: ________________
A/B-Pegel: _______________________ Testdauer: ___________________
Repro: Filter/Cube/LFO/Regler, Soll/Ist, Zeitpunkt: ______________
________________________________________________________________
________________________________________________________________
Bei Fehlern optional kurze WAV, Projekt und Screenshot sichern,
keine privaten Pfade/Audio-Dateien automatisch hochladen.

DAW/ASIO: PASS / FAIL / NOT RUN
GUI/Display: PASS / FAIL / NOT RUN
Musikalische Qualitaet: PASS / FAIL / NOT RUN

WICHTIG: 55 X3-Filter sind als Ressourcen im VST3 eingebettet.
Optionale externe X3-Forschungsdaten sind ausschliesslich fuer einen
separaten wissenschaftlichen Vergleich relevant, NICHT zum Spielen.
Ein erfolgreicher Test-Lab-Bericht ist weder ein ASIO-Nachweis noch
eine Aussage ueber historische E-MU-Klangtreue oder Verkaufsfreigabe.
'@
(($headText -join [Environment]::NewLine)+[Environment]::NewLine+$manual) |
 Set-Content -LiteralPath (Join-Path $out 'DAW-ASIO-CHECKLIST.txt') -Encoding UTF8
Write-Host ('Paketintegritaet: '+$integrity)
Write-Host ('125A Test Lab: '+$technical)
Write-Host 'Physisches ASIO, Studio Pro und Hoertest: NOT RUN'
Write-Host ('Checkliste: '+(Join-Path $out 'DAW-ASIO-CHECKLIST.txt'))
Write-Host ('Vorpruefung: '+(Join-Path $out 'DAW-ASIO-PREFLIGHT.json'))
if(-not $CI){Start-Process explorer.exe -ArgumentList ('"'+$out+'"')}
exit 0
