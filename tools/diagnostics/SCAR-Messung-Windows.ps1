# SCAR-Messung-Windows.ps1 - laeuft allein durch, kein Klicken im Skriptfenster noetig.
#
#  1. Desktop: schaltet selbst Leise -> Leistung -> Turbo um (ASUS-WMI, wie Fn+M4) und misst je 15 s.
#  2. Wartet, bis World of Warcraft laeuft (Wow.exe / WowClassic.exe), dann 60 s Ladezeit.
#  3. Im Spiel: schaltet wieder alle 3 Profile durch und misst je 15 s, inklusive FPS (PresentMon).
#  4. Stellt das Profil zurueck, schreibt Ergebnisse in "SCAR-Messung-<Datum>" neben diesem Skript.
#
# Gemessen: GPU-Takt, Speichertakt, Leistung, Stromlimit, Temperatur, Last, P-State, Bremsgrund (nvidia-smi),
#           ASUS-Firmware-Werte (TGP, Dynamic Boost, Temperaturziel, CPU-Limits; nur lesen),
#           FPS von WoW (PresentMon; wird beim ersten Mal von github.com/GameTechDev/PresentMon geladen).
#
# Start: Rechtsklick auf die Datei -> "Mit PowerShell ausfuehren" (fragt nach Administratorrechten).
# Waehrend Teil 2: WoW starten, einloggen, an die Vergleichsstelle gehen und einfach stehen bleiben.

param([switch]$Elevated)
$ErrorActionPreference = 'Stop'
$Sekunden = 15
$Warten = 8
$Ladezeit = 60

# --- Administratorrechte -----------------------------------------------------------------------------------
$id = [Security.Principal.WindowsIdentity]::GetCurrent()
if (-not (New-Object Security.Principal.WindowsPrincipal($id)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    if ($Elevated) { Write-Host "Keine Administratorrechte erhalten."; Read-Host "Enter"; exit 1 }
    Start-Process powershell.exe -Verb RunAs -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Elevated"
    exit
}

$here = Split-Path -Parent $PSCommandPath
$out = Join-Path $here ("SCAR-Messung-" + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $out | Out-Null
$csv = Join-Path $out 'gpu.csv'
$asusCsv = Join-Path $out 'asus.csv'
$summary = Join-Path $out 'zusammenfassung.txt'
$log = Join-Path $out 'ablauf.txt'
function Say([string]$t, [string]$c = 'Gray') { Write-Host $t -ForegroundColor $c; "$(Get-Date -Format 'HH:mm:ss') $t" | Add-Content $log }

# --- nvidia-smi ---------------------------------------------------------------------------------------------
$smi = @("$env:SystemRoot\System32\nvidia-smi.exe", "$env:ProgramFiles\NVIDIA Corporation\NVSMI\nvidia-smi.exe") |
       Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $smi) { Say "nvidia-smi nicht gefunden." 'Red'; Read-Host "Enter"; exit 1 }
$fields = 'pstate,clocks.gr,clocks.sm,clocks.mem,power.draw,power.limit,enforced.power.limit,power.max_limit,temperature.gpu,utilization.gpu,utilization.memory,clocks_event_reasons.active'
& $smi --query-gpu=$fields --format=csv,noheader 2>$null | Out-Null
if ($LASTEXITCODE -ne 0) { $fields = $fields -replace 'clocks_event_reasons', 'clocks_throttle_reasons' }
"phase,profil,sekunde,$fields" | Set-Content -Path $csv -Encoding UTF8

# --- ASUS-WMI -----------------------------------------------------------------------------------------------
$atk = $null
try { $atk = Get-CimInstance -Namespace root/wmi -ClassName AsusAtkWmi_WMNB -ErrorAction Stop | Select-Object -First 1 } catch { }
function Dsts([uint32]$devid) {
    $r = Invoke-CimMethod -InputObject $atk -MethodName DSTS -Arguments @{ Device_ID = $devid }
    if ($null -ne $r.device_status) { return [uint32]$r.device_status }
    return $null
}
function Devs([uint32]$devid, [uint32]$val) {
    $r = Invoke-CimMethod -InputObject $atk -MethodName DEVS -Arguments @{ Device_ID = $devid; Control_status = $val }
    return $r
}
$asusIds = [ordered]@{
    0x00120075 = 'Thermal policy (Profil)'; 0x00120098 = 'dGPU TGP'; 0x00120099 = 'dGPU Basis-TGP'
    0x001200C0 = 'NV Dynamic Boost'; 0x001200C2 = 'NV Temperaturziel'; 0x001200C6 = 'MGFP'
    0x0012009F = '0x12009F'; 0x0012009C = '0x12009C'; 0x001200A0 = 'CPU PL2 SPPT'; 0x001200A3 = 'CPU PL1 SPL'
    0x00090016 = 'GPU MUX'
}
"phase,profil,id,name,wert_hex,wert_dez" | Set-Content -Path $asusCsv -Encoding UTF8
function Read-Asus([string]$phase, [string]$profil) {
    if (-not $atk) { return }
    foreach ($k in $asusIds.Keys) {
        try {
            $v = Dsts $k
            if ($null -ne $v) { "{0},{1},0x{2:X8},{3},0x{4:X8},{5}" -f $phase, $profil, $k, $asusIds[$k], $v, ($v -band 0xFFFF) | Add-Content $asusCsv }
            else { "{0},{1},0x{2:X8},{3},leer," -f $phase, $profil, $k, $asusIds[$k] | Add-Content $asusCsv }
        } catch { "{0},{1},0x{2:X8},{3},fehler,{4}" -f $phase, $profil, $k, $asusIds[$k], ($_.Exception.Message -replace ',', ';') | Add-Content $asusCsv }
    }
}

# Profile wie Linux asus-wmi: 2 = Leise, 0 = Leistung, 1 = Turbo
$profiles = [ordered]@{ 'Leise' = 2; 'Leistung' = 0; 'Turbo' = 1 }
$canSwitch = $false
$start = $null
if ($atk) {
    try { $start = Dsts 0x00120075 } catch { }
    try { Devs 0x00120075 2 | Out-Null; $canSwitch = $true } catch { Say "Profil per WMI umschalten geht nicht: $($_.Exception.Message)" 'Yellow' }
}
function Set-Profile([string]$name) {
    if ($canSwitch) {
        try { Devs 0x00120075 $profiles[$name] | Out-Null; Say "  Profil -> $name (WMI)" ; return } catch { Say "  WMI-Umschalten fehlgeschlagen: $($_.Exception.Message)" 'Yellow' }
    }
    # Ersatz: der Nutzer drueckt Fn+M4; erkannt an einer Aenderung des GPU-Stromlimits
    $before = (& $smi --query-gpu=enforced.power.limit --format=csv,noheader,nounits) | Select-Object -First 1
    Say "  Bitte Fn+M4 druecken, bis '$name' eingestellt ist (warte bis 30 s) ..." 'Yellow'
    [console]::beep(880, 300)
    for ($i = 0; $i -lt 30; $i++) {
        Start-Sleep -Seconds 1
        $now = (& $smi --query-gpu=enforced.power.limit --format=csv,noheader,nounits) | Select-Object -First 1
        if ($now -ne $before) { Say "  Stromlimit $before -> $now W erkannt"; Start-Sleep -Seconds 3; return }
    }
    Say "  keine Aenderung erkannt, messe trotzdem" 'Yellow'
}

# --- PresentMon (FPS) ---------------------------------------------------------------------------------------
$pm = Get-ChildItem -Path $here -Filter 'PresentMon*.exe' -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
if (-not $pm) {
    try {
        Say "Lade PresentMon von GitHub (einmalig) ..."
        $rel = Invoke-RestMethod -Uri 'https://api.github.com/repos/GameTechDev/PresentMon/releases/latest' -Headers @{ 'User-Agent' = 'SCAR-Messung' }
        $asset = $rel.assets | Where-Object { $_.name -match '^PresentMon-.*x64\.exe$' } | Select-Object -First 1
        if ($asset) { $pm = Join-Path $here $asset.name; Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $pm -UseBasicParsing }
    } catch { Say "PresentMon nicht geladen ($($_.Exception.Message)) - FPS werden nicht gemessen" 'Yellow'; $pm = $null }
}

function Measure-Phase([string]$phase, [string]$profil, [string]$exe) {
    Start-Sleep -Seconds $Warten
    Read-Asus $phase $profil
    $pmJob = $null
    if ($exe -and $pm) {
        $pmCsv = Join-Path $out "fps-$profil.csv"
        $pmJob = Start-Process -FilePath $pm -PassThru -WindowStyle Hidden -ArgumentList @(
            '--process_name', $exe, '--output_file', $pmCsv, '--timed', $Sekunden, '--terminate_after_timed', '--no_console_stats', '--stop_existing_session')
    }
    for ($s = 1; $s -le $Sekunden; $s++) {
        $t0 = Get-Date
        $line = (& $smi --query-gpu=$fields --format=csv,noheader,nounits 2>$null | Select-Object -First 1)
        "$phase,$profil,$s,$line" | Add-Content -Path $csv
        $rest = 1000 - ((Get-Date) - $t0).TotalMilliseconds
        if ($rest -gt 0) { Start-Sleep -Milliseconds $rest }
    }
    if ($pmJob) { $pmJob | Wait-Process -Timeout 20 -ErrorAction SilentlyContinue }
}

function Get-Fps([string]$profil) {
    $f = Join-Path $out "fps-$profil.csv"
    if (-not (Test-Path $f)) { return '' }
    $rows = Import-Csv $f
    $col = @('MsBetweenPresents', 'MsBetweenDisplayChange', 'FrameTime') | Where-Object { $rows[0].PSObject.Properties.Name -contains $_ } | Select-Object -First 1
    if (-not $col -or $rows.Count -lt 2) { return '' }
    $ms = ($rows | ForEach-Object { [double]$_.$col } | Where-Object { $_ -gt 0 } | Measure-Object -Average).Average
    if ($ms -gt 0) { return [math]::Round(1000 / $ms, 1) } else { return '' }
}

# --- Ablauf -------------------------------------------------------------------------------------------------
Say "=== SCAR-Messung ===" 'Cyan'
Say "nvidia-smi: $smi"
Say ("ASUS-WMI: " + $(if ($atk) { "gefunden, Profil umschalten: $canSwitch" } else { 'nicht gefunden' }))
Say ("PresentMon: " + $(if ($pm) { $pm } else { 'nicht vorhanden' }))

Say "Teil 1: Desktop (bitte Maus nicht bewegen)" 'Yellow'
foreach ($p in $profiles.Keys) { Set-Profile $p; Measure-Phase 'desktop' $p $null }

Say "Teil 2: Starte jetzt WoW, logge dich ein, geh an die Vergleichsstelle und bleib stehen." 'Yellow'
[console]::beep(660, 300); [console]::beep(880, 300)
$wow = $null
while (-not $wow) {
    $wow = Get-Process -Name 'Wow', 'WowClassic', 'WowB', 'WowT' -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $wow) { Start-Sleep -Seconds 2 }
}
Say "WoW erkannt ($($wow.ProcessName).exe). Warte $Ladezeit s Ladezeit ..." 'Green'
Start-Sleep -Seconds $Ladezeit
$fps = @{}
foreach ($p in $profiles.Keys) {
    Set-Profile $p
    Measure-Phase 'spiel' $p "$($wow.ProcessName).exe"
    $fps[$p] = Get-Fps $p
    Say "  $p : FPS $($fps[$p])"
}

# Profil zuruecksetzen
if ($canSwitch) {
    $back = if ($null -ne $start -and (($start -band 0xFF) -in 0, 1, 2)) { $start -band 0xFF } else { 2 }
    try { Devs 0x00120075 $back | Out-Null; Say "Profil zurueckgesetzt ($back)" } catch { }
}
[console]::beep(880, 200); [console]::beep(660, 400)

# --- Zusammenfassung ----------------------------------------------------------------------------------------
$hdr = @('phase', 'profil', 'sekunde') + ($fields -split ',')
$rows = Import-Csv -Path $csv -Header $hdr | Select-Object -Skip 1
$lines = @("SCAR-Messung $(Get-Date)", "")
foreach ($ph in 'desktop', 'spiel') {
    foreach ($p in $profiles.Keys) {
        $r = @($rows | Where-Object { $_.phase -eq $ph -and $_.profil -eq $p })
        if (-not $r) { continue }
        $avg = { param($n) [math]::Round((($r | ForEach-Object { [double](($_.$n) -replace '[^0-9.]', '') }) | Measure-Object -Average).Average, 1) }
        $lines += ("{0,-8} {1,-9} Takt {2,6} MHz  Speicher {3,6} MHz  Leistung {4,6} W  Limit {5,6} W  Temp {6,4} C  Last {7,4} %  {8}" -f
            $ph, $p, (& $avg 'clocks.gr'), (& $avg 'clocks.mem'), (& $avg 'power.draw'), (& $avg 'enforced.power.limit'),
            (& $avg 'temperature.gpu'), (& $avg 'utilization.gpu'), $(if ($ph -eq 'spiel') { "FPS $($fps[$p])" } else { '' }))
    }
}
$lines += ""; $lines += "ASUS-Werte je Profil: asus.csv, Rohdaten: gpu.csv, fps-*.csv"
$lines | Set-Content -Path $summary -Encoding UTF8
$lines | ForEach-Object { Write-Host $_ }
Say "Fertig. Ergebnisse in: $out" 'Green'
Start-Sleep -Seconds 30
