# EasyBackupandDelay — verifica e instala los requisitos:
#   1) OBS Studio 30+  (avisa y abre la descarga si falta)
#   2) Visual C++ 2015-2022 (x64)  (descarga e instala solo si falta)
# Al final ofrece instalar el plugin si "instalar.ps1" esta en la misma carpeta.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms | Out-Null

$TITLE = 'EasyBackupandDelay - Requisitos'
function Info($t) { [void][System.Windows.Forms.MessageBox]::Show($t, $TITLE, 'OK',
    [System.Windows.Forms.MessageBoxIcon]::Information) }
function Warn($t) { [void][System.Windows.Forms.MessageBox]::Show($t, $TITLE, 'OK',
    [System.Windows.Forms.MessageBoxIcon]::Warning) }
function AskYN($t) { return [System.Windows.Forms.MessageBox]::Show($t, $TITLE, 'YesNo',
    [System.Windows.Forms.MessageBoxIcon]::Question) -eq 'Yes' }

$here = Split-Path -Parent $MyInvocation.MyCommand.Path

# ── 1) OBS Studio ─────────────────────────────────────────────────────────────
$obs = @(
    'C:\Program Files\obs-studio\bin\64bit\obs64.exe',
    (Join-Path $env:ProgramFiles 'obs-studio\bin\64bit\obs64.exe')
) | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $obs) {
    if (AskYN("No encontre OBS Studio.`n`nEl plugin necesita OBS Studio 30 o superior.`n`n¿Abrir la pagina de descarga de OBS?")) {
        Start-Process 'https://obsproject.com/download'
    }
}

# ── 2) Visual C++ 2015-2022 (x64) ─────────────────────────────────────────────
function Test-VCRedist {
    $key = 'HKLM:\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64'
    try { if ((Get-ItemProperty $key -ErrorAction Stop).Installed -eq 1) { return $true } } catch {}
    return (Test-Path (Join-Path $env:WINDIR 'System32\vcruntime140_1.dll'))
}

if (-not (Test-VCRedist)) {
    if (AskYN("Falta el Microsoft Visual C++ 2015-2022 (x64), necesario para el plugin.`n`n¿Descargar e instalar ahora?`n(Requiere internet y permiso de administrador.)")) {
        $url = 'https://aka.ms/vs/17/release/vc_redist.x64.exe'
        $tmp = Join-Path $env:TEMP 'vc_redist.x64.exe'
        try {
            $ProgressPreference = 'SilentlyContinue'
            Invoke-WebRequest -Uri $url -OutFile $tmp -UseBasicParsing
            $p = Start-Process $tmp -ArgumentList '/install', '/quiet', '/norestart' -Verb RunAs -Wait -PassThru
            if ($p.ExitCode -eq 0 -or $p.ExitCode -eq 3010) {
                Info('Visual C++ instalado correctamente.')
            } else {
                Warn("El instalador de Visual C++ termino con codigo $($p.ExitCode).`nProbá instalarlo a mano desde:`n$url")
            }
        } catch {
            Warn("No se pudo descargar/instalar el Visual C++.`nInstalalo a mano desde:`n$url`n`nDetalle: $($_.Exception.Message)")
        }
    }
}

# ── Resumen ───────────────────────────────────────────────────────────────────
$obsMsg = if ($obs) { 'OK' } else { 'FALTA  ->  instala OBS 30+' }
$vcMsg  = if (Test-VCRedist) { 'OK' } else { 'FALTA' }
Info("Estado de los requisitos:`n`n   OBS Studio:      $obsMsg`n   Visual C++ x64:  $vcMsg")

# ── Ofrecer instalar el plugin (si esta el instalador al lado) ────────────────
$inst = Join-Path $here 'instalar.ps1'
if ($obs -and (Test-VCRedist) -and (Test-Path $inst)) {
    if (AskYN("Los requisitos estan listos.`n`n¿Instalar ahora el plugin EasyBackupandDelay en OBS?`n(Cerra OBS antes.)")) {
        powershell -NoProfile -ExecutionPolicy Bypass -File $inst
    }
}
