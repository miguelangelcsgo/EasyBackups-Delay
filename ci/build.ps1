# -----------------------------------------------------------------------------
# ci/build.ps1 - compila el plugin en CI (GitLab) o localmente.
# Prepara las dependencias (_deps/), configura CMake y compila el target Release.
# ASCII puro a proposito (PowerShell 5.1 lee los .ps1 sin BOM como ANSI).
# -----------------------------------------------------------------------------
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$deps = Join-Path $root '_deps'
Write-Host "Repo: $root"

# -- 1) Dependencias (obs-sdk-local, deps-qt6, deps) --------------------------
$prefix = @(
    (Join-Path $deps 'obs-sdk-local'),
    (Join-Path $deps 'deps-qt6'),
    (Join-Path $deps 'deps')
)
function DepsOk { return (($prefix | Where-Object { Test-Path $_ }).Count -eq 3) }

if (-not (DepsOk)) {
    if ([string]::IsNullOrWhiteSpace($env:OBS_DEPS_URL)) {
        throw "Faltan las dependencias en _deps/ (obs-sdk-local, deps-qt6, deps) y no hay OBS_DEPS_URL definida."
    }
    Write-Host "Descargando dependencias de: $env:OBS_DEPS_URL"
    $zip = Join-Path $root '_deps.zip'
    $ProgressPreference = 'SilentlyContinue'
    Invoke-WebRequest -Uri $env:OBS_DEPS_URL -OutFile $zip -UseBasicParsing
    Write-Host "Extrayendo..."
    Expand-Archive -Path $zip -DestinationPath $root -Force
    Remove-Item $zip -Force
}
if (-not (DepsOk)) {
    throw "Las dependencias no quedaron en _deps/. Revisa que el zip de OBS_DEPS_URL contenga _deps/{obs-sdk-local,deps-qt6,deps}."
}
$prefixStr = ($prefix -join ';')
Write-Host "CMAKE_PREFIX_PATH = $prefixStr"

# -- 2) Ubicar cmake ---------------------------------------------------------
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $cmake = Get-ChildItem 'C:\Program Files\Microsoft Visual Studio' -Recurse -Filter cmake.exe -ErrorAction SilentlyContinue |
             Select-Object -First 1 -ExpandProperty FullName
}
if (-not $cmake) { throw 'cmake.exe no encontrado. Instala CMake o el componente "C++ CMake tools" de Visual Studio.' }
Write-Host "cmake: $cmake"

# -- 3) Configurar + compilar (Release) --------------------------------------
$build = Join-Path $root 'build'
if (-not (Test-Path (Join-Path $build 'CMakeCache.txt'))) {
    Write-Host '== Configurando CMake =='
    # -S explicito: sin el, CMake toma el directorio actual como source dir y
    # configura cualquier cosa (o nada) segun desde donde se haya lanzado.
    & $cmake -S $root -B $build -A x64 "-DCMAKE_PREFIX_PATH=$prefixStr"
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure fallo.' }
}

Write-Host '== Compilando Release =='
& $cmake --build $build --config Release --target EasyBackupandDelay
if ($LASTEXITCODE -ne 0) { throw 'La compilacion fallo.' }

$dll = Join-Path $build 'Release\EasyBackupandDelay.dll'
if (-not (Test-Path $dll)) { throw "El build no genero $dll" }
$fi = Get-Item $dll
Write-Host ("OK: {0} ({1} bytes, v{2})" -f $dll, $fi.Length, $fi.VersionInfo.FileVersion) -ForegroundColor Green
