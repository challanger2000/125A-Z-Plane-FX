125A Z-Plane FX — Windows-Abnahmepaket v0.1.0 (ENTWICKLUNG)
================================================================

1. ZIP in einen ORDNER entpacken. Nicht aus der ZIP heraus ausführen.
2. 00-START-TEST.bat doppelklicken (ohne Python, Visual Studio, VM, Admin).
3. In Reports\YYYYMMDD-HHMMSS die Datei ACCEPTANCE.txt lesen.
   Rohdaten: ACCEPTANCE.json, P25-TSV, Logs, BMP und Test-WAV.
4. Vor Installation Fender Studio Pro schließen. Danach optional
   01-INSTALL-PLUGIN.bat starten und die DAW neu starten.
5. Mit 02-DAW-ASIO-CHECKLIST.bat die echte DAW/ASIO-Pruefung vorbereiten.
   DAW-ASIO-PREFLIGHT.json ueberprueft die Paket-Hashes und liest den
   passenden bereits vorhandenen technischen 125A-Test-Lab-Bericht.
   Keine separaten/duplizierten Testsysteme. Ohne Lab-Beleg NOT VERIFIED.
   Fuer Hoer-/LFO-Tests einen echten Synth in Fender Studio Pro nutzen:
   Die automatische Stereo-WAV ist lediglich ein PCM-Testsignal.

Enthalten:
  Plugin\125A Z-Plane FX.vst3     Windows-x64-VST3, Ressourcen
  Native\*.exe                    Z-Plane-C++-Tests (7 Testarten)
  TestLab\*.exe                   Native universelle Lab-Hosts
  Tools\*.ps1                     Windows PowerShell 5.1 Testskripte
  SHA256-MANIFEST.json            Original-Paket-Hashvergleich

Die drei X3-Filtertabellen sind im VST3 selbst enthalten.
Alle 55 Filter laufen OHNE zusaetzliche Bankdateien oder Installation.
Die BAT prueft alle 55 eingebauten Filter zwingend und automatisiert.
Nur externe wissenschaftliche Referenzvergleiche benoetigen optionale
separate Forschungsdaten im Verzeichnis Banks\, nicht das Plugin.

Sicherheitsregeln:
  Die Installation verwendet den offiziellen VST3-Benutzerpfad
  %LOCALAPPDATA%\Programs\Common\VST3 ohne Administratorrechte.
  Eine vorhandene Benutzer-Version wird zuerst in Backups\ gesichert.
  Keine automatische Veränderung der globalen Program-Files-Installation.
  Keine automatische ASIO-Audioausgabe, keine Änderung am DAW-Projekt.
  Reports nur lokal, ohne Hochladen von Rechner- oder Audiodaten.
  Echte ASIO-Hardware, Studio-Pro-Darstellung und musikalische Qualitaet
  bleiben NOT RUN bis zur separaten realen Praxisabnahme.

Testsemantik:
  PASS = ausgeführte technische Prüfung erfolgreich.
  PARTIAL_X3_REFERENCE_NOT_RUN = eingebaute 55 Filter PASS, externer Referenzvergleich NOT RUN.
  NOT RUN = nicht ausgeführt / keine automatische Abnahme.
  Echte ASIO/ETW-Deadlines, E-MU-Hardware-Klangtreue und die
  kommerziellen Nutzungsrechte sind ausdrücklich NICHT nachgewiesen.
  Kein FULL AUDIT oder Release.
