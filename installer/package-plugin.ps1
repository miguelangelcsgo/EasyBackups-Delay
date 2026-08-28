# Empaqueta EasyBackupandDelay como un ZIP de plugin de OBS (se extrae en la carpeta de OBS).
#   powershell -ExecutionPolicy Bypass -File package-plugin.ps1
# Sin rutas locales: por defecto toma el DLL del build de este repo y deja el ZIP
# en <repo>\dist. Se puede apuntar a otro lado con -Dll / -OutDir o con la
# variable de entorno EASYBACKUP_DIST.
param(
    [string]$Dll    = (Join-Path $PSScriptRoot '..\build\Release\EasyBackupandDelay.dll'),
    [string]$OutDir = $(if ($env:EASYBACKUP_DIST) { $env:EASYBACKUP_DIST } else { Join-Path $PSScriptRoot '..\dist' })
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not (Test-Path $Dll)) { throw "No existe el DLL: $Dll" }
# $OutDir puede no existir (p. ej. el dist\ del repo recien clonado).
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }
$OutDir = (Resolve-Path $OutDir).Path

$ver = (Get-Item $Dll).VersionInfo.FileVersion
if (-not $ver) { $ver = '0.0.0' }

# Árbol de staging que replica el layout de la carpeta de instalación de OBS.
$stage = Join-Path $env:TEMP 'eobs-package'
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
$binDir = Join-Path $stage 'obs-plugins\64bit'
New-Item -ItemType Directory -Path $binDir -Force | Out-Null
Copy-Item $Dll (Join-Path $binDir 'EasyBackupandDelay.dll') -Force
Copy-Item (Join-Path $root 'README-dist.txt') (Join-Path $stage 'README.txt') -Force

# GPLv2: el plugin se linkea contra OBS, asi que el binario se distribuye bajo esa
# licencia. El texto viaja dentro del ZIP y el README apunta al repo de fuentes.
$lic = Join-Path (Split-Path -Parent $root) 'LICENSE'
if (Test-Path $lic) {
    Copy-Item $lic (Join-Path $stage 'LICENSE.txt') -Force
} else {
    throw "Falta $lic - el ZIP no puede salir sin la licencia (requisito GPL)."
}

# FFmpeg propio: la subcarpeta EasyBackupandDelay-ffmpeg\ (avcodec/avutil/swscale que
# el build copió al lado del DLL) viaja junto al plugin, para no depender de la version
# de FFmpeg que traiga OBS. Ver ffmpeg-delayload.cpp / CMakeLists.
$ffSrc = Join-Path (Split-Path -Parent $Dll) 'EasyBackupandDelay-ffmpeg'
if (Test-Path $ffSrc) {
    $ffCount = (Get-ChildItem $ffSrc -Filter *.dll -ErrorAction SilentlyContinue).Count
    Copy-Item $ffSrc (Join-Path $binDir 'EasyBackupandDelay-ffmpeg') -Recurse -Force
    Write-Host ("FFmpeg empaquetado: " + $ffCount + " DLLs en la subcarpeta EasyBackupandDelay-ffmpeg") -ForegroundColor Green
} else {
    Write-Warning ("No se encontro " + $ffSrc + " - recompila para que CMake copie las DLLs del bundle.")
}

$zip = Join-Path $OutDir "EasyBackupandDelay-$ver-windows-x64.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }

Write-Host "Empaquetando v$ver..." -ForegroundColor Cyan
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -Force

# Mantener el INSTALAR.txt suelto (notas de instalación manual) sellado con la
# versión actual, para que nunca quede atrás respecto de la DLL. No-op si no está.
$instalar = Join-Path $OutDir 'INSTALAR.txt'
if (Test-Path $instalar) {
    $txt = Get-Content $instalar -Raw -Encoding UTF8
    $new = [regex]::Replace($txt, 'Versi\S+n:\s*v[\d.]+', "Versi" + [char]0xF3 + "n: v$ver")
    if ($new -ne $txt) {
        Set-Content $instalar -Value $new -Encoding UTF8 -NoNewline
        Write-Host "INSTALAR.txt actualizado a v$ver" -ForegroundColor Green
    }
}

if (Test-Path $zip) {
    $fi = Get-Item $zip
    Write-Host "OK: $zip  ($([math]::Round($fi.Length/1KB)) KB)  v$ver" -ForegroundColor Green
} else {
    throw 'No se genero el ZIP.'
}
