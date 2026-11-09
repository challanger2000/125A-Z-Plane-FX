#Requires -Version 5.1
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$out=Join-Path $root ('Reports\DAW-ASIO-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $out -Force | Out-Null
$names=@()
try{if(Test-Path 'HKLM:\SOFTWARE\ASIO'){$names=@(Get-ChildItem 'HKLM:\SOFTWARE\ASIO' | ForEach-Object {$_.PSChildName})}}catch{}
$names | Set-Content -LiteralPath (Join-Path $out 'REGISTERED-ASIO-DRIVERS.txt') -Encoding UTF8
$doc=@'
125A Z-Plane FX - Fender Studio Pro/ASIO-ABNAHME
================================================
NOT RUN bis zur echten Durchfuehrung. Keine automatische ASIO-Ausgabe.
Zuerst Abhoerlautstaerke niedrig einstellen!

DAW Build: ____________  Windows: ____________
ASIO-Interface / Treiber: __________  Version: _________
Rate: __________ Hz  Buffer: __________ Samples
Gesamtlaufzeit: __________ Minuten

Importiere aus dem letzten technischen Reports-Ordner
die Datei 125A-DAW-REFERENCE-44100-24bit-stereo.wav.

[ ] 01. DAW scannt VST3 und oeffnet Editor bei 100%/Zoom
[ ] 02. Physischer ASIO-Treiber ist aktiv (NICHT WASAPI)
[ ] 03. Original-WAV sauber mit Stereo-Kanaltrennung abspielbar
[ ] 04. Legacy: Lowpass, Highpass, Bandpass, Notch
[ ] 05. X3: drei echte Bankdateien vorhanden, alle 55 Modi
[ ] 06. X3: Modi 1, 17, 33, 34, 50 und 55 bei Wet=100
[ ] 07. Wet=0 ist trocken, keine Phasen-/Stereo-Artefakte
[ ] 08. Bypass / Wet 100% / Morph / Q ohne unkontrollierten Pegelsprung
[ ] 09. Schnelle Filterwechsel und 5-ms-Fades ohne Haenger
[ ] 10. VST3-Automation an Blockgrenzen (Klang/Werte)
[ ] 11. Projekt speichern, DAW schliessen, wieder oeffnen:
         Werte, Filterbank, Automation korrekt zurueck
[ ] 12. Hardware-ASIO mit 64, 128, 256, 512 (soweit unterstuetzt)
[ ] 13. 44.1 / 48 / 96 kHz (soweit unterstuetzt)
[ ] 14. Mindestens 10 Minuten Loop, GUI offen/geschlossen
[ ] 15. Export der echten gefilterten 24-bit-WAV, Bildschirmbild
         der ASIO-Einstellungen und Problemzeitpunkte sichern

Abweichungen mit Filter, Rate, Buffer, Steps und Zeitpunkt:
__________________________________________________________
__________________________________________________________
__________________________________________________________

Manuell bewertet: PASS / FAIL / NOT RUN

Hinweis: Diese BAT erzeugt eine pruefbare Checkliste, misst aber
KEINE Hardware-Callback-Fristen/ETW-Werte. Eine ausgefuellte
Liste ist kein E-MU-Hardwarevergleich und keine Vertriebsfreigabe.
'@
$doc | Set-Content -LiteralPath (Join-Path $out 'DAW-ASIO-CHECKLIST.txt') -Encoding UTF8
Write-Host ("ASIO-Treibernamen in Registry: "+$names.Count)
Write-Host ('Pruefliste: '+(Join-Path $out 'DAW-ASIO-CHECKLIST.txt'))
Start-Process explorer.exe -ArgumentList ('"'+$out+'"')
exit 0
