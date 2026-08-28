# Compila EasyBackupandDelay-Setup.exe con Inno Setup (instalador + desinstalador de verdad).
# Windows reconoce a los instaladores de Inno como setups legítimos, así que no reciben
# el bloqueo duro de "inseguro" que sí reciben los auto-extraíbles de IExpress.
#   powershell -ExecutionPolicy Bypass -File build-installer.ps1
# Sin rutas locales: por defecto toma el DLL del build de este repo y deja el .exe
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

$iscc = @(
    'C:\Program Files (x86)\Inno Setup 6\ISCC.exe',
    'C:\Program Files\Inno Setup 6\ISCC.exe',
    (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe')
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw 'No se encontro ISCC.exe (instala Inno Setup: winget install JRSoftware.InnoSetup).' }

$ver = (Get-Item $Dll).VersionInfo.FileVersion
if (-not $ver) { $ver = '0.0.0' }

# Bundle de FFmpeg propio (avcodec/avutil/swscale + deps) que copia el build al lado
# del DLL. DEBE viajar en el instalador: sin él el delay queda desactivado (usa su
# propia FFmpeg para no depender de la versión de OBS). Ver ffmpeg-delayload.cpp.
$ffDir = Join-Path (Split-Path -Parent $Dll) 'EasyBackupandDelay-ffmpeg'
$ffLine = ''
$ffUninstall = ''
if (Test-Path $ffDir) {
    $ffCount = (Get-ChildItem $ffDir -Filter *.dll).Count
    Write-Host ("Bundle FFmpeg: " + $ffCount + " DLLs incluidas en el instalador.") -ForegroundColor Green
    $ffLine = 'Source: "' + $ffDir + '\*"; DestDir: "{app}\obs-plugins\64bit\EasyBackupandDelay-ffmpeg"; Flags: ignoreversion recursesubdirs createallsubdirs'
    $ffUninstall = 'Type: filesandordirs; Name: "{app}\obs-plugins\64bit\EasyBackupandDelay-ffmpeg"'
} else {
    Write-Warning ('No se encontro el bundle ' + $ffDir + ' - el instalador saldra SOLO con la DLL. Compila primero.')
}

# GPLv2: el plugin se linkea contra OBS. El instalador tiene que mostrar la licencia
# y dejar junto al binario el texto completo + el puntero al repo de fuentes.
$repoRoot = Split-Path -Parent $root
$licFile  = Join-Path $repoRoot 'LICENSE'
$readme   = Join-Path $root 'README-dist.txt'
if (-not (Test-Path $licFile)) { throw "Falta $licFile - el instalador no puede salir sin la licencia (requisito GPL)." }

$iss = @"
; Generado por build-installer.ps1 — NO editar a mano.
[Setup]
LicenseFile=$licFile
AppId={{8F3E1C2A-4B2D-4E7A-9C10-EA5Y0B5BK000}
AppName=EasyBackupandDelay
AppVersion=$ver
AppPublisher=MAVSoft (miguelangelcsgo)
AppComments=Plugin para OBS Studio: copia en la nube + filtros de delay
DefaultDirName={code:GetOBSDir}
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64
ArchitecturesAllowed=x64
OutputDir=$OutDir
OutputBaseFilename=EasyBackupandDelay-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=EasyBackupandDelay (plugin para OBS Studio)
CloseApplications=yes
RestartApplications=no
DirExistsWarning=no

[Languages]
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"

[Files]
Source: "$Dll"; DestDir: "{app}\obs-plugins\64bit"; Flags: ignoreversion
Source: "$licFile"; DestDir: "{app}\obs-plugins\64bit"; DestName: "EasyBackupandDelay-LICENSE.txt"; Flags: ignoreversion
Source: "$readme"; DestDir: "{app}\obs-plugins\64bit"; DestName: "EasyBackupandDelay-README.txt"; Flags: ignoreversion
$ffLine

[UninstallDelete]
Type: files; Name: "{app}\obs-plugins\64bit\EasyBackupandDelay-LICENSE.txt"
Type: files; Name: "{app}\obs-plugins\64bit\EasyBackupandDelay-README.txt"
$ffUninstall

[Code]
// Auto-detecta la carpeta de OBS desde el registro; si no, usa Program Files.
function GetOBSDir(Param: String): String;
var p: String;
begin
  Result := ExpandConstant('{commonpf}\obs-studio');
  if RegQueryStringValue(HKLM, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio', 'InstallLocation', p) then
    if p <> '' then Result := p;
end;

// Avisa si la carpeta elegida no parece ser OBS (no existe obs-plugins\64bit).
function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = wpSelectDir then
    if not DirExists(ExpandConstant('{app}\obs-plugins\64bit')) then
      if MsgBox('Esa carpeta no parece ser la de OBS Studio (no encontre obs-plugins\64bit).' + #13#10 +
                'Instalar igual aca?', mbConfirmation, MB_YESNO) = IDNO then
        Result := False;
end;
"@

$issPath = Join-Path $env:TEMP 'EasyBackupandDelay.iss'
Set-Content -Path $issPath -Value $iss -Encoding UTF8

$out = Join-Path $OutDir 'EasyBackupandDelay-Setup.exe'
if (Test-Path $out) { Remove-Item $out -Force }

Write-Host "Compilando instalador con Inno Setup (v$ver)..." -ForegroundColor Cyan
& $iscc $issPath | Out-Null
if ($LASTEXITCODE -ne 0) { throw "ISCC fallo (codigo $LASTEXITCODE)." }

if (Test-Path $out) {
    $fi = Get-Item $out
    Write-Host "OK: $out  ($([math]::Round($fi.Length/1KB)) KB)  v$ver" -ForegroundColor Green
} else {
    throw 'No se genero el instalador.'
}
