========================================================================
 EasyBackupandDelay  —  OBS Studio plugin
 by MAVSoft (miguelangelcsgo)
========================================================================

WHAT IT DOES / QUÉ HACE
------------------------------------------------------------------------
- Cloud backup & restore of your OBS scenes, profiles and media
  (Google Drive, OneDrive, or a local synced folder).
- Broadcast-delay filters for any source: Screen Delay (video/audio),
  delayed-key ducking, Push-to-Delay (mic) and Push-to-Delay (camera).
- "Delay Playback" source: clone any camera with a delay and place it
  anywhere on screen — always-on or only while you hold a key.
- A dock + global hotkey to turn ALL delays on/off at once.
- Multi-language UI: English, Español, Français, 中文, 日本語.

REQUIREMENTS / REQUISITOS
------------------------------------------------------------------------
- Windows 64-bit.
- OBS Studio 30 or newer (Qt6).
- Microsoft Visual C++ 2015-2022 Redistributable (x64):
  https://aka.ms/vs/17/release/vc_redist.x64.exe

INSTALL / INSTALACIÓN
------------------------------------------------------------------------
1) Close OBS completely (also check the system tray, next to the clock).
2) Right-click "EasyBackupandDelay.dll" -> Properties -> tick "Unblock" -> OK.
   (Removes the "downloaded from the internet" mark so OBS can load it.)
3) Copy the "obs-plugins" folder from this zip INTO your OBS install folder,
   usually:  C:\Program Files\obs-studio\
   The DLL must end up at:
   C:\Program Files\obs-studio\obs-plugins\64bit\EasyBackupandDelay.dll
   (Click "Continue" if Windows asks for administrator permission.)
4) Open OBS.

USAGE / USO
------------------------------------------------------------------------
- Tools menu -> "Cloud Backup / Restore…"  (language selector inside).
- Add filters to any source (right-click source -> Filters):
  Screen Delay (video), Screen Delay (audio), Push-to-Delay (mic/camera).
- Add source -> "Delay Playback (cámara demorada)" to clone a camera delayed.
- Dock "EasyBackupandDelay — Delays" toggles all delays; assign a hotkey in
  Settings -> Hotkeys if you like.

SOURCE CODE & LICENSE / CÓDIGO FUENTE Y LICENCIA
------------------------------------------------------------------------
EasyBackupandDelay links against OBS Studio and is therefore distributed
under the GNU General Public License, version 2 or (at your option) any
later version.

The complete corresponding source code for this build is available at:

  https://github.com/miguelangelcsgo/EasyBackups-Delay/

A copy of the GPLv2 is included in this package as LICENSE, and is also
available at https://www.gnu.org/licenses/old-licenses/gpl-2.0.html

Copyright (C) 2026 MAVSoft (miguelangelcsgo)

This program is distributed WITHOUT ANY WARRANTY; without even the implied
warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

SUPPORT THE PROJECT / APOYÁ EL PROYECTO
------------------------------------------------------------------------
Donate:   https://ceneka.net/miguelangelcsgo
          https://streamlabs.com/miguelangelcsgo/tip
Follow:   Twitch / YouTube / Kick / TikTok / Instagram  ->  miguelangelcsgo

========================================================================
