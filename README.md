# EasyBackupandDelay

Source code for **EasyBackupandDelay**, an OBS Studio plugin that backs up and
restores your entire OBS setup, and adds a broadcast-delay toolkit for streamers.

- Plugin page: https://obsproject.com/forum/resources/easy-backup-and-delay.2592/
- Downloads: https://mavsoft.com.ar/easybackupanddelay/

This repository is the *corresponding source* for the released binaries, published
to comply with the GNU General Public License v2 that OBS Studio is distributed
under. See [License](#license).

---

## What it does

**Backup & restore**

| Category | Files |
|---|---|
| Scene collections | `%APPDATA%\obs-studio\basic\scenes\*.json` |
| Profiles | Everything under `%APPDATA%\obs-studio\basic\profiles\*` |
| Media | Images, videos, fonts and local HTML files referenced by your scenes |

Targets Google Drive, OneDrive or a local synced folder. A `manifest.json` is
written next to the files so restore knows where each one belongs.

**Broadcast delay**

Per-source video/audio delay filters, live-voice ducking, Push-to-Delay for mic
and camera, a `Delay Playback` source, a delay-status text source, and a dock
with a global hotkey to toggle every delay at once.

---

## Privacy / Privacidad

The plugin makes **no network connections of any kind**: no update check, no
error reporting, no telemetry. It does not link against WinHTTP. Everything it
does — backups, restores, delay filters — happens on your machine. Updates are
downloaded manually.

El plugin **no hace ninguna conexion de red**: ni chequeo de actualizaciones, ni
reporte de errores, ni telemetria. Ni siquiera se linkea contra WinHTTP. Todo lo
que hace ocurre en tu maquina. Las actualizaciones se bajan a mano.

---

## Layout

```
src/plugin-main.cpp     module entry point, OBS registration
src/backup/             backup & restore: scene parsing, manifest, cloud providers, UI tabs
src/delay/              delay filters, codec, dock, ducking, push-to-delay, mic mute
src/web/                "Web / Stream" dock
src/i18n.hpp            built-in translation table (en, es, fr, zh, ja)
ci/build.ps1            build script
installer/              packaging and installer scripts
```

---

## Building

### Requirements

- Windows 10/11 (64-bit)
- OBS Studio 30+ and its development headers/libs (`libobs`, `obs-frontend-api`)
- Visual Studio 2022 (MSVC, x64)
- CMake 3.20+
- Qt6 (Core, Widgets, Svg) — ships with OBS Studio 28+
- FFmpeg import libs (`avcodec`, `avutil`, `swscale`) from the OBS dependencies package

`nlohmann/json` is fetched automatically by CMake, so there is nothing to install
for it.

### Configure & build

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH="C:/path/to/obs-studio-build;C:/path/to/obs-deps"

cmake --build build --config Release
```

`CMAKE_PREFIX_PATH` must point at a location where CMake can find the `libobs`
and `obs-frontend-api` packages, plus the OBS dependencies package that provides
FFmpeg and Qt6.

There is also `ci/build.ps1`, which is the script used to produce the released
binaries.

### Install

Copy the built module to your OBS installation:

```
build\Release\EasyBackupandDelay.dll
  -> C:\Program Files\obs-studio\obs-plugins\64bit\EasyBackupandDelay.dll
```

Also copy the `EasyBackupandDelay-ffmpeg\` folder produced next to the DLL. The
plugin delay-loads FFmpeg and prefers those bundled DLLs, falling back to the
ones shipped with OBS — this is what keeps the delay working across OBS versions
that were built against a different FFmpeg ABI.

Then restart OBS.

---

## Notes on the cloud backends

The plugin does not ship any OAuth credentials. You register your own
application with Google or Microsoft and enter the client ID (and, for Google,
the client secret) in the plugin's Settings tab. The flow is:

1. The plugin opens your browser at the provider's consent page.
2. The provider redirects to `http://localhost:8765/callback`.
3. The plugin exchanges the authorization code for tokens.
4. Tokens are stored locally in
   `%APPDATA%\obs-studio\plugin_config\EasyBackupandDelay\cloud-backup.json`.

OneDrive uses PKCE, so it needs no client secret.

---

## License

Copyright (C) 2026 MAVSoft (miguelangelcsgo)

This program is free software; you can redistribute it and/or modify it under
the terms of the **GNU General Public License version 2**, or (at your option)
any later version, as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the [LICENSE](LICENSE) file for the full text, or
<https://www.gnu.org/licenses/old-licenses/gpl-2.0.html>.

EasyBackupandDelay links against OBS Studio, which is licensed under GPLv2, and
is therefore distributed under the same terms.
