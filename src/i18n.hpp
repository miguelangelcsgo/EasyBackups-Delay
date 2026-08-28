#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// i18n.hpp — pequeña tabla de traducción en código para el diálogo de Copia.
//
// Cinco idiomas, elegidos desde un combo box en el diálogo. Sin herramientas
// .ts/.qm: cada string mantiene sus cinco variantes juntas en líneas contiguas
// para que sea fácil mantenerlas sincronizadas. La selección se guarda con QSettings.
//
// Agregar un string: sumá un valor al enum S y una fila correspondiente en kStrings
// (mismo orden, las cinco columnas). Usá marcadores %1 y QString::arg() al invocar.
// ─────────────────────────────────────────────────────────────────────────────
#include <QString>
#include <QSettings>

enum class Lang { EN = 0, ES = 1, FR = 2, ZH = 3, JA = 4, COUNT = 5 };

enum class S {
    WindowTitle,
    TabSettings, TabBackup, TabRestore, TabDelay,

    // Settings tab
    BackupFolder, BackupFolderPh, Browse,
    SaveSettings, HintLocal,

    // Backup tab
    BackupStatus, StartBackup,
    DonateTitle, DonateIntro, DonateWord,
    FollowTitle, FollowIntro,

    // Delay switch dock (main window)
    DockActive, DockBypassed,

    // Delay switch key selector (Settings tab)
    DelaySwitchGroup, DelaySwitchKeyLabel, DelaySwitchNone, DelaySwitchInfo,

    // Fuentes atadas al estado del delay (Delay tab)
    DelayVisGroup, DelayVisColSource, DelayVisColWhen,
    DelayVisIgnore, DelayVisWithDelay, DelayVisWithoutDelay,
    DelayVisInfo, DelayVisEmpty,

    // Botón de micrófono (dock) y su configuración (Delay tab)
    MicLive, MicMuted, MicNotFound,
    MicGroup, MicSourceLabel, MicSourceAuto, MicKeyLabel, MicInfo,

    // Luz de audio demorado (dock) y su aviso sonoro (Delay tab)
    PlayFree, PlayBusy,
    AlertGroup, AlertEnable, AlertDevice, AlertDeviceDefault, AlertTest, AlertInfo,
    AlertLead,


    // Monitor de la voz demorada (Delay tab)
    MonitorEnable, MonitorInfo,

    // Restore tab
    AvailableBackup, Refresh, ColItem, ColType, ColSize,
    SceneCollectionName, SceneNamePh, RestoreSelected,
    SecSceneCollections, SecProfiles, SecMediaFiles, FilesCountFmt,

    // Message boxes / dialogs
    SelectBackupFolder,
    SettingsSavedTitle, SettingsSavedBody,
    NotConnectedTitle, NotConnectedBody,
    NoBackupTitle, NoBackupBody,
    RestoreBackupTitle, RestoreConfirmBody,
    BackupCompleteTitle, BackupCompleteBody,
    BackupFailedTitle, BackupFailedBody,
    RestoreCompleteTitle, RestoreCompleteBody,
    RestoreFailedTitle, RestoreFailedBody,
    NoBackupFoundTitle, NoBackupFoundBodyFmt, NoBackupFoundHint,

    // Web / Stream dock (main window)
    WebDockTitle, WebUrlLabel, WebUrlPh, WebNamePh, WebWidth, WebHeight,
    WebWithAudio, WebAddBtn, WebApplyBtn, WebInfo,
    WebNoUrlTitle, WebNoUrlBody, WebNoBrowserTitle, WebNoBrowserBody,
    WebNoSceneTitle, WebNoSceneBody, WebNoSelTitle, WebNoSelBody,
    WebNotBrowserBody, WebAddedTitle, WebAddedBody, WebUpdatedTitle, WebUpdatedBody,
    WebEmbedOnly,
    WebZoomBtn, WebZoomInfo, WebZoomBadCropTitle, WebZoomBadCropBody,
    WebZoomedTitle, WebZoomedBody,

    // Version instalada (Settings tab)
    UpdatesGroup, UpdateCurrentFmt,

    COUNT
};

struct TrRow { const char* en; const char* es; const char* fr; const char* zh; const char* ja; };

// Una fila por cada valor de S, en el MISMO orden que el enum. El orden importa.
static const TrRow kStrings[] = {
/* WindowTitle          */ {"Cloud Backup and Delay","Copia y delay","Sauvegarde et delay","云备份与延迟","バックアップとディレイ"},

/* TabSettings          */ {"⚙ Settings","⚙ Ajustes","⚙ Réglages","⚙ 设置","⚙ 設定"},
/* TabBackup            */ {"☁ Backup","☁ Copia","☁ Sauvegarde","☁ 备份","☁ バックアップ"},
/* TabRestore           */ {"⬇ Restore","⬇ Restaurar","⬇ Restaurer","⬇ 恢复","⬇ 復元"},
/* TabDelay             */ {"⏱ Delay","⏱ Delay","⏱ Delay","⏱ 延迟","⏱ ディレイ"},

/* BackupFolder         */ {"Backup folder:","Carpeta de copia:","Dossier de sauvegarde :","备份文件夹：","バックアップフォルダー："},
/* BackupFolderPh       */ {"e.g. C:\\Users\\you\\OneDrive\\My-Backup","ej. C:\\Users\\vos\\OneDrive\\Mi-Copia","ex. C:\\Users\\vous\\OneDrive\\Ma-Sauvegarde","例如 C:\\Users\\you\\OneDrive\\My-Backup","例：C:\\Users\\you\\OneDrive\\My-Backup"},
/* Browse               */ {"Browse…","Examinar…","Parcourir…","浏览…","参照…"},
/* SaveSettings         */ {"Save settings","Guardar ajustes","Enregistrer","保存设置","設定を保存"},
/* HintLocal            */ {"<small>Pick a folder inside your <b>OneDrive</b> or <b>Google Drive</b> sync directory. The plugin copies your backup there and the desktop client uploads it automatically – no login or credentials needed.</small>","<small>Elegí una carpeta dentro de tu carpeta de sincronización de <b>OneDrive</b> o <b>Google Drive</b>. El plugin copia ahí tu backup y el cliente de escritorio lo sube automáticamente: sin login ni credenciales.</small>","<small>Choisissez un dossier dans votre répertoire de synchronisation <b>OneDrive</b> ou <b>Google Drive</b>. Le plugin y copie votre sauvegarde et le client de bureau l’envoie automatiquement — aucun identifiant requis.</small>","<small>选择 <b>OneDrive</b> 或 <b>Google Drive</b> 同步目录内的文件夹。插件会把备份复制到那里，桌面客户端会自动上传——无需登录或凭据。</small>","<small><b>OneDrive</b> または <b>Google Drive</b> の同期フォルダー内のフォルダーを選択してください。プラグインがそこにバックアップをコピーし、デスクトップアプリが自動でアップロードします（ログインや認証情報は不要）。</small>"},

/* BackupStatus         */ {"Click 'Start backup' to upload scenes, profiles and media files.","Hacé clic en 'Iniciar copia' para subir escenas, perfiles y archivos multimedia.","Cliquez sur « Démarrer la sauvegarde » pour envoyer scènes, profils et médias.","点击“开始备份”上传场景、配置文件和媒体文件。","「バックアップ開始」をクリックして、シーン・プロファイル・メディアをアップロードします。"},
/* StartBackup          */ {"Start backup","Iniciar copia","Démarrer la sauvegarde","开始备份","バックアップ開始"},
/* DonateTitle          */ {"\U0001f49b Support the project","\U0001f49b Apoyá el proyecto","\U0001f49b Soutenez le projet","\U0001f49b 支持这个项目","\U0001f49b プロジェクトを応援"},
/* DonateIntro          */ {"Is <b>EasyBackupandDelay</b> useful to you? I build it in my free time and <b>I'm currently out of work</b>, so every donation helps a lot to keep it alive and improving. Thank you from the heart! ❤️","¿Te sirve <b>EasyBackupandDelay</b>? Lo desarrollo en mi tiempo libre y <b>ahora mismo estoy sin trabajo</b>, así que cada donación ayuda muchísimo a mantenerlo y seguir mejorándolo. ¡Gracias de corazón! ❤️","<b>EasyBackupandDelay</b> vous est utile ? Je le développe sur mon temps libre et <b>je suis actuellement sans emploi</b>, chaque don aide énormément à le maintenir et à l’améliorer. Merci du fond du cœur ! ❤️","<b>EasyBackupandDelay</b> 对你有帮助吗？我利用业余时间开发它，而且<b>目前没有工作</b>，每一笔捐赠都能帮助它持续维护和改进。衷心感谢！❤️","<b>EasyBackupandDelay</b> は役に立っていますか？余暇に開発しており、<b>今は仕事がない状態</b>なので、ご寄付は継続と改善の大きな支えになります。心から感謝します！❤️"},
/* DonateWord           */ {"Donate","Donar","Faire un don","捐赠","寄付する"},
/* FollowTitle          */ {"\U0001f4e3 Follow me on social media","\U0001f4e3 Seguime en mis redes","\U0001f4e3 Suivez-moi sur les réseaux","\U0001f4e3 关注我的社交媒体","\U0001f4e3 SNSでフォローしてね"},
/* FollowIntro          */ {"Can't donate? No worries — a follow on my socials helps a lot too. Thank you so much! \U0001f64c","¿No podés donar? ¡No hay problema! Seguirme en mis redes también ayuda un montón. ¡Muchísimas gracias! \U0001f64c","Vous ne pouvez pas faire de don ? Pas de souci — un abonnement sur mes réseaux aide beaucoup aussi. Merci infiniment ! \U0001f64c","不能捐赠也没关系——在我的社交媒体上关注一下同样帮助很大。非常感谢！\U0001f64c","寄付が難しくても大丈夫！SNSでのフォローもとても助かります。本当にありがとう！\U0001f64c"},

/* DockActive           */ {"🟢 Delays ON — click to bypass","🟢 Delays ACTIVOS — clic para apagar","🟢 Delays ACTIFS — cliquez pour couper","🟢 延迟已开启 — 点击关闭","🟢 ディレイ ON — クリックで無効化"},
/* DockBypassed         */ {"🔴 Delays OFF (live) — click to enable","🔴 Delays APAGADOS (en vivo) — clic para activar","🔴 Delays COUPÉS (direct) — cliquez pour activer","🔴 延迟已关闭（直播）— 点击开启","🔴 ディレイ OFF（ライブ）— クリックで有効化"},

/* DelaySwitchGroup     */ {"Delay switch (turn all delays on/off)","Interruptor de delays (apagar/encender todo)","Interrupteur de delays (tout couper/activer)","延迟开关（一键开关所有延迟）","ディレイスイッチ（全ディレイの ON/OFF）"},
/* DelaySwitchKeyLabel  */ {"Toggle key:","Tecla:","Touche :","切换按键：","切り替えキー："},
/* DelaySwitchNone      */ {"None","Ninguna","Aucune","无","なし"},
/* DelaySwitchInfo      */ {"Pick a key to turn ALL delays on/off with one press (works globally, even with the game focused).","Elegi una tecla para apagar/encender TODOS los delays con una pulsacion (funciona global, con el juego en foco).","Choisissez une touche pour couper/activer TOUS les delays d'une pression (fonctionne globalement, meme en jeu).","选择一个按键即可一键开关所有延迟（全局有效，游戏聚焦时也可）。","1回の押下で全ディレイをON/OFFするキーを選べます（ゲーム中でもグローバルに動作）。"},

/* DelayVisGroup        */ {"Sources that follow the delay switch","Fuentes que siguen al interruptor de delay","Sources liees a l'interrupteur de delay","跟随延迟开关的来源","ディレイスイッチに連動するソース"},
/* DelayVisColSource    */ {"Source","Fuente","Source","来源","ソース"},
/* DelayVisColWhen      */ {"When to show it","Cuando mostrarla","Quand l'afficher","何时显示","表示するタイミング"},
/* DelayVisIgnore       */ {"Don't touch","No tocar","Ne pas toucher","不处理","変更しない"},
/* DelayVisWithDelay    */ {"Show with delay ON","Mostrar con delay activo","Afficher avec delay actif","延迟开启时显示","ディレイONのとき表示"},
/* DelayVisWithoutDelay */ {"Show with delay OFF","Mostrar sin delay","Afficher sans delay","延迟关闭时显示","ディレイOFFのとき表示"},
/* DelayVisInfo         */ {"Each source set here is shown or hidden automatically when the delay switch flips, in every scene it appears in. Example: the full-screen capture as 'Show with delay OFF' and the game-only capture as 'Show with delay ON' - one keypress swaps them.","Cada fuente que marques aca se muestra u oculta sola cuando cambia el interruptor de delay, en todas las escenas donde este. Ejemplo: la pantalla que muestra todo como 'Mostrar sin delay' y la que muestra solo el juego como 'Mostrar con delay activo': una tecla las intercambia.","Chaque source reglee ici s'affiche ou se masque automatiquement quand l'interrupteur de delay change, dans toutes les scenes ou elle apparait. Exemple : la capture plein ecran en 'Afficher sans delay' et la capture du jeu seul en 'Afficher avec delay actif' - une touche les echange.","在这里设置的每个来源，会在延迟开关切换时自动显示或隐藏（在它出现的所有场景中）。例如：全屏画面设为“延迟关闭时显示”，仅游戏画面设为“延迟开启时显示”，一个按键即可互换。","ここで設定したソースは、ディレイスイッチが切り替わると、含まれるすべてのシーンで自動的に表示/非表示になります。例：全画面キャプチャを「ディレイOFFのとき表示」、ゲームのみを「ディレイONのとき表示」にすると、キー1つで入れ替わります。"},
/* DelayVisEmpty        */ {"No sources in this scene collection yet.","Todavia no hay fuentes en esta coleccion de escenas.","Aucune source dans cette collection de scenes.","此场景集合中还没有来源。","このシーンコレクションにはまだソースがありません。"},

/* MicLive              */ {"Mic live","Microfono al aire","Micro ouvert","麦克风已开启","マイク オン"},
/* MicMuted             */ {"Mic muted","Microfono silenciado","Micro coupe","麦克风已静音","マイク ミュート"},
/* MicNotFound          */ {"No microphone","Sin microfono","Aucun micro","没有麦克风","マイクなし"},
/* MicGroup             */ {"Microphone button (dock)","Boton de microfono (dock)","Bouton micro (dock)","麦克风按钮（面板）","マイクボタン（ドック）"},
/* MicSourceLabel       */ {"Microphone:","Microfono:","Micro :","麦克风：","マイク："},
/* MicSourceAuto        */ {"Automatic (OBS Mic/Aux)","Automatico (Mic/Aux de OBS)","Automatique (Micro/Aux d'OBS)","自动（OBS 麦克风/辅助）","自動（OBS のマイク/AUX）"},
/* MicKeyLabel          */ {"Mute key:","Tecla:","Touche :","静音按键：","ミュートキー："},
/* MicInfo              */ {"The dock button turns green when the mic is live and red when it is muted; click it to switch. Pick a key to do it globally (works with the game focused) - there is also an OBS hotkey under Settings / Hotkeys.","El boton del dock se pone verde con el microfono al aire y rojo cuando esta silenciado; clic para cambiarlo. Elegi una tecla para hacerlo global (funciona con el juego en foco): tambien hay un atajo de OBS en Ajustes / Atajos.","Le bouton du dock passe au vert quand le micro est ouvert et au rouge quand il est coupe ; cliquez pour changer. Choisissez une touche pour le faire globalement (meme en jeu) - il y a aussi un raccourci OBS dans Parametres / Raccourcis.","当麦克风开启时面板按钮为绿色，静音时为红色；点击即可切换。可选择一个按键进行全局切换（游戏聚焦时也有效），在 OBS 的“设置 / 快捷键”中也有对应快捷键。","マイクがオンのときドックのボタンは緑、ミュート時は赤になります（クリックで切り替え）。グローバルに切り替えるキーも選べます（ゲーム中でも動作）。OBS の「設定／ホットキー」にも項目があります。"},

/* PlayFree             */ {"Clear — you can talk","Libre — podes hablar","Libre — vous pouvez parler","空闲 — 可以说话","フリー — 話してOK"},
/* PlayBusy             */ {"PLAYING — don't talk","SONANDO — no hables","EN LECTURE — ne parlez pas","播放中 — 请勿说话","再生中 — 話さないで"},
/* AlertGroup           */ {"Delayed audio: light and alert","Audio demorado: luz y aviso","Audio differe : voyant et alerte","延迟音频：指示灯与提示音","ディレイ音声：ランプと通知音"},
/* AlertEnable          */ {"Play a sound when the delayed audio starts","Sonar un aviso cuando arranca el audio demorado","Jouer un son quand l'audio differe demarre","延迟音频开始时播放提示音","ディレイ音声の開始時に音を鳴らす"},
/* AlertDevice          */ {"Alert output device:","Dispositivo del aviso:","Sortie de l'alerte :","提示音输出设备：","通知音の出力先："},
/* AlertDeviceDefault   */ {"Default device","Dispositivo predeterminado","Peripherique par defaut","默认设备","既定のデバイス"},
/* AlertTest            */ {"Test","Probar","Tester","测试","テスト"},
/* AlertInfo            */ {"The dock light turns RED while your delayed voice is on air, so you don't talk over yourself, and GREEN when nothing delayed is playing. The alert is played straight to the chosen output device, never through the OBS mixer, so it is on no track. IMPORTANT: pick an output OBS is NOT capturing — if you send it to the same device your Desktop Audio captures, it will end up on stream anyway.","La luz del dock se pone ROJA mientras tu voz demorada esta al aire, para que no te pises, y VERDE cuando no suena nada demorado. El aviso se toca directo al dispositivo elegido, nunca por el mezclador de OBS, asi que no va en ninguna pista. IMPORTANTE: elegi una salida que OBS NO este capturando — si la mandas al mismo dispositivo que captura tu Audio del escritorio, igual va a salir al aire.","Le voyant passe au ROUGE pendant que votre voix differee est a l'antenne, et au VERT quand rien ne joue. L'alerte est envoyee directement au peripherique choisi, jamais via le mixeur d'OBS. IMPORTANT : choisissez une sortie qu'OBS ne capture PAS.","当你的延迟语音正在播出时，面板指示灯变为红色，避免与自己抢话；没有延迟音频播放时为绿色。提示音直接送到所选设备，不经过 OBS 混音器，因此不在任何音轨上。重要：请选择 OBS 未捕获的输出设备，否则仍会进入直播。","ディレイ音声が放送中はドックのランプが赤になり（自分の声にかぶせないため）、何も再生していないときは緑になります。通知音は選んだデバイスへ直接出力され、OBS のミキサーを通らないのでどのトラックにも乗りません。重要：OBS がキャプチャしていない出力先を選んでください。"},
/* AlertLead            */ {"Warn this many seconds before:","Avisar con esta anticipacion:","Prevenir avec ce delai :","提前多少秒提醒：","何秒前に知らせるか："},


/* MonitorEnable        */ {"Hear my delayed voice in my headphones","Escuchar mi voz demorada en los auriculares","Ecouter ma voix differee au casque","在耳机里听我的延迟语音","ディレイ音声をヘッドホンで聞く"},
/* MonitorInfo          */ {"Plays back your own delayed voice so you can hear exactly what is going out and stop before talking over it. It uses the OBS monitoring device (Settings > Audio), and it does NOT add a second copy to any track. IMPORTANT: the monitoring device must be one your Desktop Audio is not capturing, otherwise the loopback picks it up and it ends up on stream anyway.","Te reproduce tu propia voz demorada para que escuches exactamente lo que esta saliendo y pares antes de pisarte. Usa el dispositivo de monitoreo de OBS (Ajustes > Audio) y NO agrega una segunda copia a ninguna pista. IMPORTANTE: ese dispositivo de monitoreo tiene que ser uno que tu Audio del escritorio NO este capturando; si no, el loopback lo vuelve a tomar y termina al aire igual.","Rejoue votre voix differee pour entendre exactement ce qui part a l'antenne. Utilise le peripherique de monitoring d'OBS et n'ajoute aucune copie sur les pistes. IMPORTANT : ce peripherique ne doit pas etre capture par votre Audio du bureau.","回放你自己的延迟语音，让你清楚听到正在播出的内容并及时停下。使用 OBS 的监听设备（设置 > 音频），不会向任何音轨添加第二份副本。重要：该监听设备不能是桌面音频正在捕获的设备，否则回环会再次采集并最终进入直播。","自分のディレイ音声を再生し、実際に配信されている内容を確認して話しかぶせを防げます。OBS のモニタリングデバイス（設定 > 音声）を使い、どのトラックにも二重に乗りません。重要：デスクトップ音声がキャプチャしていないデバイスを選んでください。"},

/* AvailableBackup      */ {"Available backup:","Copia disponible:","Sauvegarde disponible :","可用备份：","利用可能なバックアップ："},
/* Refresh              */ {"Refresh","Actualizar","Actualiser","刷新","更新"},
/* ColItem              */ {"Item","Elemento","Élément","项目","項目"},
/* ColType              */ {"Type","Tipo","Type","类型","種類"},
/* ColSize              */ {"Size","Tamaño","Taille","大小","サイズ"},
/* SceneCollectionName  */ {"Scene collection name:","Nombre de la colección de escenas:","Nom de la collection de scènes :","场景集合名称：","シーンコレクション名："},
/* SceneNamePh          */ {"Name for the restored scene collection","Nombre para la colección restaurada","Nom de la collection restaurée","恢复后的场景集合名称","復元するシーンコレクションの名前"},
/* RestoreSelected      */ {"Restore selected backup","Restaurar copia seleccionada","Restaurer la sauvegarde","恢复所选备份","選択したバックアップを復元"},
/* SecSceneCollections  */ {"Scene collections","Colecciones de escenas","Collections de scènes","场景集合","シーンコレクション"},
/* SecProfiles          */ {"Profiles","Perfiles","Profils","配置文件","プロファイル"},
/* SecMediaFiles        */ {"Media files","Archivos multimedia","Fichiers médias","媒体文件","メディアファイル"},
/* FilesCountFmt        */ {"%1 files","%1 archivos","%1 fichiers","%1 个文件","%1 ファイル"},

/* SelectBackupFolder   */ {"Select backup folder","Elegí la carpeta de copia","Choisir le dossier de sauvegarde","选择备份文件夹","バックアップフォルダーを選択"},
/* SettingsSavedTitle   */ {"Settings saved","Ajustes guardados","Réglages enregistrés","设置已保存","設定を保存しました"},
/* SettingsSavedBody    */ {"Settings saved successfully.","Ajustes guardados correctamente.","Réglages enregistrés.","设置保存成功。","設定を保存しました。"},
/* NotConnectedTitle    */ {"Not connected","Sin conectar","Non connecté","未连接","未接続"},
/* NotConnectedBody     */ {"Set a valid backup folder first (Settings tab).","Configura primero una carpeta de copia valida (solapa Ajustes).","Definissez d'abord un dossier de sauvegarde valide (onglet Reglages).","请先设置有效的备份文件夹（设置选项卡）。","先に有効なバックアップフォルダーを設定してください（設定タブ）。"},
/* NoBackupTitle        */ {"No backup","Sin copia","Aucune sauvegarde","无备份","バックアップなし"},
/* NoBackupBody         */ {"No backup loaded. Click Refresh first.","No hay copia cargada. Hacé clic en Actualizar primero.","Aucune sauvegarde chargée. Cliquez d’abord sur Actualiser.","未加载备份。请先点击刷新。","バックアップが読み込まれていません。先に更新をクリックしてください。"},
/* RestoreBackupTitle   */ {"Restore backup","Restaurar copia","Restaurer la sauvegarde","恢复备份","バックアップを復元"},
/* RestoreConfirmBody   */ {"This will overwrite your current scenes, profiles and media files.\nContinue?","Esto sobrescribira tus escenas, perfiles y archivos multimedia actuales.\nContinuar?","Cela remplacera vos scenes, profils et medias actuels.\nContinuer ?","这将覆盖你当前的场景、配置文件和媒体文件。\n继续吗？","現在のシーン・プロファイル・メディアを上書きします。\n続けますか？"},
/* BackupCompleteTitle  */ {"Backup complete","Copia completada","Sauvegarde terminée","备份完成","バックアップ完了"},
/* BackupCompleteBody   */ {"Backup uploaded successfully.","Copia subida correctamente.","Sauvegarde envoyée avec succès.","备份上传成功。","バックアップをアップロードしました。"},
/* BackupFailedTitle    */ {"Backup failed","Falló la copia","Échec de la sauvegarde","备份失败","バックアップに失敗しました"},
/* BackupFailedBody     */ {"An error occurred during backup.","Ocurrió un error durante la copia.","Une erreur s’est produite pendant la sauvegarde.","备份过程中发生错误。","バックアップ中にエラーが発生しました。"},
/* RestoreCompleteTitle */ {"Restore complete","Restauración completada","Restauration terminée","恢复完成","復元完了"},
/* RestoreCompleteBody  */ {"Restore complete.\n\nThe app needs to restart to load the restored scenes, profiles and media. Keeping this session open may overwrite the restored files.\n\nRestart now?","Restauracion completada.\n\nLa aplicacion necesita reiniciarse para cargar las escenas, perfiles y multimedia restaurados. Si seguis con esta sesion abierta podes sobrescribir los archivos restaurados.\n\nReiniciar ahora?","Restauration terminee.\n\nL'application doit redemarrer pour charger les scenes, profils et medias restaures. Garder cette session ouverte pourrait ecraser les fichiers restaures.\n\nRedemarrer maintenant ?","恢复完成。\n\n应用需要重启才能加载恢复的场景、配置文件和媒体。保持此会话可能会覆盖恢复的文件。\n\n现在重启吗？","復元が完了しました。\n\n復元したシーン・プロファイル・メディアを読み込むにはアプリの再起動が必要です。このセッションを開いたままにすると、復元したファイルを上書きする可能性があります。\n\n今すぐ再起動しますか？"},
/* RestoreFailedTitle   */ {"Restore failed","Falló la restauración","Échec de la restauration","恢复失败","復元に失敗しました"},
/* RestoreFailedBody    */ {"An error occurred during restore.","Ocurrió un error durante la restauración.","Une erreur s’est produite pendant la restauration.","恢复过程中发生错误。","復元中にエラーが発生しました。"},
/* NoBackupFoundTitle   */ {"No backup found","No se encontró copia","Aucune sauvegarde trouvée","未找到备份","バックアップが見つかりません"},
/* NoBackupFoundBodyFmt */ {"Could not load a backup from %1.","No se pudo cargar una copia desde %1.","Impossible de charger une sauvegarde depuis %1.","无法从 %1 加载备份。","%1 からバックアップを読み込めませんでした。"},
/* NoBackupFoundHint    */ {"Make sure the folder points to your backup location.","Asegurate de que la carpeta apunte a tu ubicacion de copia.","Assurez-vous que le dossier pointe vers votre emplacement de sauvegarde.","请确保该文件夹指向你的备份位置。","フォルダーがバックアップの場所を指していることを確認してください。"},

/* WebDockTitle         */ {"🌐 Web / Stream","🌐 Web / Stream","🌐 Web / Stream","🌐 网页 / 直播","🌐 ウェブ / 配信"},
/* WebUrlLabel          */ {"URL:","URL:","URL :","URL：","URL："},
/* WebUrlPh             */ {"https://…  (Twitch, YouTube, Kick, any web page)","https://…  (Twitch, YouTube, Kick, cualquier web)","https://…  (Twitch, YouTube, Kick, toute page web)","https://…（Twitch、YouTube、Kick、任意网页）","https://…（Twitch、YouTube、Kick、任意のウェブページ）"},
/* WebNamePh            */ {"Name (optional)","Nombre (opcional)","Nom (optionnel)","名称（可选）","名前（任意）"},
/* WebWidth             */ {"Width:","Ancho:","Largeur :","宽度：","幅："},
/* WebHeight            */ {"Height:","Alto:","Hauteur :","高度：","高さ："},
/* WebWithAudio         */ {"Route audio into OBS","Traer el audio a OBS","Router l'audio vers OBS","将音频接入 OBS","音声を OBS に取り込む"},
/* WebAddBtn            */ {"➕ Add box to scene","➕ Agregar recuadro a la escena","➕ Ajouter le cadre à la scène","➕ 向场景添加窗口","➕ 枠をシーンに追加"},
/* WebApplyBtn          */ {"✏ Apply to selected box","✏ Aplicar a la seleccionada","✏ Appliquer à la sélection","✏ 应用到所选","✏ 選択中に適用"},
/* WebInfo              */ {"Adds a web page / another streamer's stream as a resizable box. Drag its corners in the preview to resize, or set the width/height here. Paste any URL.","Agrega una página web / el stream de otro como un recuadro redimensionable. Arrastrá las esquinas en el preview para cambiar el tamaño, o poné el ancho/alto acá. Pegá cualquier URL.","Ajoute une page web / le stream d'un autre en cadre redimensionnable. Faites glisser les coins dans l'aperçu pour redimensionner, ou définissez la taille ici. Collez n'importe quelle URL.","将网页/他人的直播添加为可调整大小的窗口。在预览中拖动边角调整大小，或在此设置宽高。粘贴任意 URL。","ウェブページや他配信者の映像をサイズ変更可能な枠として追加します。プレビューで角をドラッグするか、ここで幅と高さを設定します。任意の URL を貼り付けてください。"},
/* WebNoUrlTitle        */ {"Missing URL","Falta la URL","URL manquante","缺少 URL","URL がありません"},
/* WebNoUrlBody         */ {"Enter a URL first.","Primero ingresá una URL.","Saisissez d'abord une URL.","请先输入 URL。","先に URL を入力してください。"},
/* WebNoBrowserTitle    */ {"Browser source unavailable","Fuente de navegador no disponible","Source navigateur indisponible","浏览器源不可用","ブラウザソースを利用できません"},
/* WebNoBrowserBody     */ {"Could not create a browser source. Make sure OBS was installed with the Browser Source (obs-browser) feature.","No se pudo crear la fuente de navegador. Verificá que OBS esté instalado con la función Fuente de navegador (obs-browser).","Impossible de créer une source navigateur. Vérifiez qu'OBS est installé avec la fonction Source navigateur (obs-browser).","无法创建浏览器源。请确认 OBS 安装时包含浏览器源（obs-browser）功能。","ブラウザソースを作成できませんでした。OBS がブラウザソース（obs-browser）機能付きでインストールされているか確認してください。"},
/* WebNoSceneTitle      */ {"No scene","Sin escena","Aucune scène","无场景","シーンなし"},
/* WebNoSceneBody       */ {"There is no active scene to add the box to.","No hay una escena activa donde agregar el recuadro.","Aucune scène active où ajouter le cadre.","没有可添加窗口的活动场景。","枠を追加できるアクティブなシーンがありません。"},
/* WebNoSelTitle        */ {"No selection","Nada seleccionado","Rien de sélectionné","未选择","未選択"},
/* WebNoSelBody         */ {"Select a web box in the current scene first.","Primero seleccioná un recuadro web en la escena actual.","Sélectionnez d'abord un cadre web dans la scène actuelle.","请先在当前场景中选择一个网页窗口。","先に現在のシーンでウェブ枠を選択してください。"},
/* WebNotBrowserBody    */ {"The selected source is not a web box.","La fuente seleccionada no es un recuadro web.","La source sélectionnée n'est pas un cadre web.","所选源不是网页窗口。","選択したソースはウェブ枠ではありません。"},
/* WebAddedTitle        */ {"Box added","Recuadro agregado","Cadre ajouté","已添加窗口","枠を追加しました"},
/* WebAddedBody         */ {"Web box added to the current scene. Drag it in the preview to move or resize.","Recuadro web agregado a la escena actual. Arrastralo en el preview para moverlo o redimensionarlo.","Cadre web ajouté à la scène actuelle. Faites-le glisser dans l'aperçu pour le déplacer ou le redimensionner.","网页窗口已添加到当前场景。在预览中拖动即可移动或调整大小。","現在のシーンにウェブ枠を追加しました。プレビューでドラッグして移動やサイズ変更ができます。"},
/* WebUpdatedTitle      */ {"Updated","Actualizado","Mis à jour","已更新","更新しました"},
/* WebUpdatedBody       */ {"Web box updated.","Recuadro web actualizado.","Cadre web mis à jour.","网页窗口已更新。","ウェブ枠を更新しました。"},
/* WebEmbedOnly         */ {"Show only the stream (embed player)","Mostrar solo el directo (player embebido)","Afficher seulement le direct (lecteur intégré)","仅显示直播画面（嵌入播放器）","配信画面だけを表示（埋め込みプレーヤー）"},
/* WebZoomBtn           */ {"🔍 Enlarge crop (selected)","🔍 Ampliar recorte (selección)","🔍 Agrandir le recadrage (sélection)","🔍 放大裁剪（所选）","🔍 切り抜きを拡大（選択中）"},
/* WebZoomInfo          */ {"Zoom into a fixed region (e.g. the camera): select the source, crop it to the region with Alt + drag on its edges, then click here to enlarge it to the width/height above.","Zoom a una región fija (ej. la cámara): seleccioná la fuente, recortala a la región con Alt + arrastrar los bordes, y tocá acá para ampliarla al ancho/alto de arriba.","Zoom sur une zone fixe (ex. la caméra) : sélectionnez la source, recadrez-la avec Alt + glisser sur les bords, puis cliquez ici pour l'agrandir à la largeur/hauteur ci-dessus.","放大固定区域（例如摄像头）：选择源，按住 Alt 拖动边缘裁剪到该区域，然后点击此处将其放大到上面的宽高。","固定領域（例：カメラ）を拡大：ソースを選択し、Alt+ドラッグで領域まで切り抜いてから、ここをクリックして上の幅・高さに拡大します。"},
/* WebZoomBadCropTitle  */ {"Nothing to enlarge","Nada para ampliar","Rien à agrandir","无可放大内容","拡大対象がありません"},
/* WebZoomBadCropBody   */ {"Select a source in the scene first (and crop it with Alt + drag to pick the region).","Primero seleccioná una fuente en la escena (y recortala con Alt + arrastrar para elegir la región).","Sélectionnez d'abord une source dans la scène (et recadrez-la avec Alt + glisser pour choisir la zone).","请先在场景中选择一个源（并用 Alt + 拖动裁剪出区域）。","先にシーンでソースを選択してください（Alt+ドラッグで領域を切り抜いてください）。"},
/* WebZoomedTitle       */ {"Enlarged","Ampliado","Agrandi","已放大","拡大しました"},
/* WebZoomedBody        */ {"The cropped region was enlarged. Drag it in the preview to place it. Tip: duplicate the source first (Ctrl+C / Ctrl+V) if you want to keep the full view too.","Se amplió la región recortada. Arrastralo en el preview para ubicarlo. Tip: duplicá la fuente antes (Ctrl+C / Ctrl+V) si querés conservar también la vista completa.","La zone recadrée a été agrandie. Faites-la glisser dans l'aperçu pour la placer. Astuce : dupliquez la source d'abord (Ctrl+C / Ctrl+V) pour garder aussi la vue complète.","已放大裁剪区域。在预览中拖动以放置。提示：如果想同时保留完整画面，请先复制源（Ctrl+C / Ctrl+V）。","切り抜いた領域を拡大しました。プレビューでドラッグして配置してください。ヒント：全体表示も残したい場合は先にソースを複製（Ctrl+C / Ctrl+V）してください。"},

/* UpdatesGroup        */ {"Updates","Actualizaciones","Mises à jour","更新","アップデート"},
/* UpdateCurrentFmt    */ {"Installed version: %1","Versión instalada: %1","Version installée : %1","已安装版本：%1","インストール済みバージョン：%1"},
};

// Guarda en tiempo de compilación: una fila de kStrings por cada valor de S. Agregar
// un valor al enum sin su fila (o viceversa) ahora rompe la compilación en vez de leer fuera de rango.
static_assert(sizeof(kStrings) / sizeof(kStrings[0]) == (size_t)S::COUNT,
              "i18n: kStrings must have exactly one row per S enum value");

// ── Idioma actual (guardado con QSettings) ───────────────────────────────────
inline Lang& g_lang_ref()
{
    static Lang lang = []{
        QSettings st("MAVSoft", "EasyOBSBackups");
        int v = st.value("language", (int)Lang::ES).toInt();
        if (v < 0 || v >= (int)Lang::COUNT) v = (int)Lang::ES;
        return (Lang)v;
    }();
    return lang;
}

inline Lang currentLang() { return g_lang_ref(); }

inline void setCurrentLang(Lang l)
{
    g_lang_ref() = l;
    QSettings st("MAVSoft", "EasyOBSBackups");
    st.setValue("language", (int)l);
}

// Traducir: busca la fila de `id` y devuelve la columna del idioma actual
// como un QString en UTF-8.
inline QString T(S id)
{
    const TrRow& r = kStrings[(int)id];
    const char* col = nullptr;
    switch (currentLang()) {
        case Lang::EN: col = r.en; break;
        case Lang::ES: col = r.es; break;
        case Lang::FR: col = r.fr; break;
        case Lang::ZH: col = r.zh; break;
        case Lang::JA: col = r.ja; break;
        default:       col = r.en; break;
    }
    return QString::fromUtf8(col);
}

// Nombres nativos de los idiomas para el selector (nunca se traducen).
inline QString langName(Lang l)
{
    switch (l) {
        case Lang::EN: return QStringLiteral("English");
        case Lang::ES: return QStringLiteral("Español");
        case Lang::FR: return QStringLiteral("Français");
        case Lang::ZH: return QString::fromUtf8("中文");
        case Lang::JA: return QString::fromUtf8("日本語");
        default:       return QStringLiteral("English");
    }
}
