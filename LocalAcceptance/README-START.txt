125A Z-Plane FX — Windows-Abnahmepaket v0.1.0 (ENTWICKLUNG)
================================================================

1. ZIP in einen ORDNER entpacken. Nicht aus der ZIP heraus ausführen.
2. 00-START-TEST.bat doppelklicken (ohne Python, Visual Studio, VM, Admin).
3. In Reports\YYYYMMDD-HHMMSS die Datei ACCEPTANCE.txt lesen.
   Rohdaten: ACCEPTANCE.json, P25-TSV, Logs, BMP und Test-WAV.
4. Vor Installation Fender Studio Pro schließen. Danach optional
   01-INSTALL-PLUGIN.bat starten und die DAW neu starten.
5. Mit 02-DAW-ASIO-CHECKLIST.bat die echte DAW/ASIO-Prüfung vorbereiten.

Enthalten:
  Plugin\125A Z-Plane FX.vst3     Windows-x64-VST3, Ressourcen
  Native\*.exe                    Z-Plane-C++-Tests (7 Testarten)
  TestLab\*.exe                   Native universelle Lab-Hosts
  Tools\*.ps1                     Windows PowerShell 5.1 Testskripte
  SHA256-MANIFEST.json            Original-Paket-Hashvergleich

Nicht enthalten: die originalen X3-Filterbankdaten (Nutzungsrechte offen).
Eigene berechtigte Dateien factory-bank.bin, standard-bank.bin und
morph-support.bin in Banks\ ablegen, oder schon im Benutzerordner
%LOCALAPPDATA%\125A\ZPlaneFX\Banks lassen.
Ohne diese Dateien: X3-Original-Referenzen werden als NOT RUN markiert.
Die universelle Stereo-Audio/State-Prüfung läuft trotzdem unabhängig.

Sicherheitsregeln:
  Die Installation verwendet den offiziellen VST3-Benutzerpfad
  %LOCALAPPDATA%\Programs\Common\VST3 ohne Administratorrechte.
  Eine vorhandene Benutzer-Version wird zuerst in Backups\ gesichert.
  Keine automatische Veränderung der globalen Program-Files-Installation.
  Keine automatische ASIO-Audioausgabe, keine Änderung am DAW-Projekt.
  Reports nur lokal, ohne Hochladen von Rechner- oder Audiodaten.

Testsemantik:
  PASS = ausgeführte technische Prüfung erfolgreich.
  PARTIAL_X3_NOT_RUN = keine Originalbanken, X3-Klang nicht überprüft.
  NOT RUN = nicht ausgeführt / keine automatische Abnahme.
  Echte ASIO/ETW-Deadlines, E-MU-Hardware-Klangtreue und die
  kommerziellen Nutzungsrechte sind ausdrücklich NICHT nachgewiesen.
  Kein FULL AUDIT oder Release.
