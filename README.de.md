# macOS Tahoe auf dem ASUS ROG Strix SCAR 18 (G835LXG) – Kurzfassung

Ausführlich auf Englisch: [README.md](README.md).

OpenCore-EFI, Treiber und Befunde für **macOS 26 Tahoe** auf dem ASUS ROG Strix SCAR 18 G835LXG
(Core Ultra 9 290HX Plus, RTX 5090 Laptop). Getestet auf genau einem Gerät mit macOS 26.7.2.

## Was funktioniert

- Start von macOS, Windows und Ubuntu über OpenCore 1.0.9 (Textmenü, nach 10 s macOS).
- RTX 5090 und internes Display mit dem [NullMoth-NVIDIA-Treiber](https://github.com/nullmoth/nvidia-macos-driver) 2.8.1 (open RM 620.70.06).
- **Interne Lautsprecher** links und rechts: VoodooHDA + eigener Treiber TAS2781DirectI2C.
- Kopfhörer (Einstecken schaltet die Lautsprecher ab) und internes Mikrofon.
- WLAN (BE200), Bluetooth, Ethernet, Touchpad, Akku.
- Webcam (FaceTime mit Live-Bild; Photo Booth macht Fotos, zeigt aber kein Live-Bild).
- **Fn+M4** Leistungsprofil Leise/Leistung/Turbo, Start im Leise-Profil wie unter Windows.
- **Fn+F2/F3** Tastaturlicht, **Fn+F4** Aura-Effekte.
- Menüleisten-App **ROG Licht**: Farben, 12 Effekte, Helligkeit, Zonen Tastatur/Lichtleiste/Logo und Leistungsprofil – wie die Beleuchtungsseite von Armoury Crate.
- **Fn+F7/F8** und der Helligkeitsregler steuern die echte Hintergrundbeleuchtung (NullMoth 2.8 mit Boot-Argument `nvrmacpibl=1`).

## Was nicht geht

- World of Warcraft läuft seit NullMoth 2.8, aber nur mit ca. 40 FPS (Windows: 112–154 FPS); der Engpass ist der Metal→Vulkan-Weg.

## Ruhezustand

Funktioniert nicht: Beim Aufwachen startet der Laptop neu (BIOS setzt S3 nicht fort, vermutlich nur
Modern Standby). Lösung hier: Systemschlaf mit `pmset` abschalten, nur der Bildschirm geht aus.

## Kern der Ton-Lösung

1. Die TAS2781-Verstärker liefern nach dem macOS-Start auf jedem Register `0x00`, bis ein
   Software-Reset kommt. Das war die eigentliche Ursache der früheren „I²C-Lesefehler“.
2. Eigener Treiber mit direktem, gepolltem Zugriff auf den I²C-Controller, unabhängig von VoodooI2C.
3. Initialisierung byte-genau wie unter Linux, inklusive Werkskalibrierung aus der UEFI-Variable `CALI_DATA`.
4. Der ALC285 braucht beide Wege (Pin 0x14 und 0x17). VoodooHDA zeigt nur einen Lautsprecher an, der
   Treiber spiegelt den Stream auf den zweiten Wandler.

## Danke

An **NullMoth** für den NVIDIA-Treiber, Acidanthera, VoodooHDA, watchthelight (WTLAudio), VoodooI2C,
OpenIntelWireless, Dortania und die Linux-Entwickler von `tas2781`, `asus-wmi` und asusctl.
Vollständige Liste in der [README.md](README.md#thanks).
