#include "web-dock.hpp"
#include "i18n.hpp"

#include <obs.h>
#include <obs-frontend-api.h>
#include <graphics/vec2.h>

#include <QWidget>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QFrame>
#include <QMessageBox>
#include <QSettings>
#include <QMainWindow>
#include <QUrl>
#include <QUrlQuery>
#include <QStringList>
#include <QSet>

#include <cstring>
#include <exception>

// El plugin obs-browser expone el tipo de fuente "browser_source". Estas son las
// claves de settings que usamos; el resto (fps, css, etc.) quedan en su default.
static const char* kBrowserId = "browser_source";
static const char* kDockId    = "easyobs_web_stream";

// ── helpers ──────────────────────────────────────────────────────────────────

static QWidget* mainWindow()
{
    return static_cast<QWidget*>(obs_frontend_get_main_window());
}

// Convierte la URL de una plataforma conocida a la URL del PLAYER embebido, para
// que el recuadro muestre solo el directo/video y no la página entera (chat,
// banners, cookies). Si no reconoce la plataforma, devuelve la URL tal cual (con
// esquema). Reconoce: Twitch, YouTube y Kick; además, un texto suelto sin punto
// ni barra se toma como nombre de canal de Twitch.
static QString toEmbedUrl(const QString& rawIn, bool audio)
{
    QString raw = rawIn.trimmed();
    if (raw.isEmpty()) return raw;

    const QString muted = audio ? QStringLiteral("false") : QStringLiteral("true");
    const QString ytMute = audio ? QStringLiteral("0") : QStringLiteral("1");

    // "solo el nombre" → canal de Twitch (ej.: escribir  miguelangelcsgo).
    if (!raw.contains('/') && !raw.contains('.') && !raw.contains(':')) {
        return QStringLiteral("https://player.twitch.tv/?channel=%1&parent=localhost&muted=%2")
            .arg(raw, muted);
    }

    const QString withScheme = raw.contains(QStringLiteral("://")) ? raw
                                                                   : (QStringLiteral("https://") + raw);
    QUrl u(withScheme);
    QString host = u.host().toLower();
    if (host.startsWith(QStringLiteral("www."))) host = host.mid(4);
    if (host.startsWith(QStringLiteral("m.")))   host = host.mid(2);

    const QStringList seg = u.path().split('/', Qt::SkipEmptyParts);

    // Ya es un player embebido → dejar como está.
    if (host == QStringLiteral("player.twitch.tv") ||
        host == QStringLiteral("player.kick.com"))
        return withScheme;

    // Twitch: twitch.tv/CANAL → player.twitch.tv
    if (host == QStringLiteral("twitch.tv")) {
        static const QSet<QString> reserved = {
            QStringLiteral("videos"), QStringLiteral("directory"),
            QStringLiteral("settings"), QStringLiteral("u"), QStringLiteral("p"),
            QStringLiteral("subscriptions"), QStringLiteral("following"),
        };
        if (!seg.isEmpty() && !reserved.contains(seg[0].toLower())) {
            return QStringLiteral("https://player.twitch.tv/?channel=%1&parent=localhost&muted=%2")
                .arg(seg[0], muted);
        }
        return withScheme;
    }

    // YouTube: watch?v=ID / youtu.be/ID / shorts/ID / /embed/ID → embed
    if (host == QStringLiteral("youtube.com")) {
        if (!seg.isEmpty() && seg[0] == QStringLiteral("embed"))
            return withScheme;
        QString id = QUrlQuery(u).queryItemValue(QStringLiteral("v"));
        if (id.isEmpty() && seg.size() >= 2 && seg[0] == QStringLiteral("shorts"))
            id = seg[1];
        if (!id.isEmpty())
            return QStringLiteral("https://www.youtube.com/embed/%1?autoplay=1&mute=%2")
                .arg(id, ytMute);
        return withScheme;
    }
    if (host == QStringLiteral("youtu.be") && !seg.isEmpty()) {
        return QStringLiteral("https://www.youtube.com/embed/%1?autoplay=1&mute=%2")
            .arg(seg[0], ytMute);
    }

    // Kick: kick.com/CANAL → player.kick.com/CANAL
    if (host == QStringLiteral("kick.com") && !seg.isEmpty()) {
        return QStringLiteral("https://player.kick.com/%1").arg(seg[0]);
    }

    return withScheme;   // plataforma desconocida: la página tal cual
}

// Un nombre de fuente único a nivel OBS (obs no permite dos fuentes homónimas).
static QString uniqueSourceName(const QString& wanted)
{
    QString base = wanted.trimmed().isEmpty() ? QStringLiteral("Web") : wanted.trimmed();
    QString name = base;
    for (int i = 2;; ++i) {
        obs_source_t* ex = obs_get_source_by_name(name.toUtf8().constData());
        if (!ex) break;
        obs_source_release(ex);
        name = base + " " + QString::number(i);
    }
    return name;
}

// Vuelca los campos del dock a un obs_data de settings de browser_source.
static obs_data_t* buildSettings(const QString& url, int w, int h, bool audio)
{
    obs_data_t* s = obs_data_create();
    obs_data_set_string(s, "url", url.toUtf8().constData());
    obs_data_set_int(s, "width", w);
    obs_data_set_int(s, "height", h);
    obs_data_set_bool(s, "reroute_audio", audio);   // enruta el audio de la página a OBS
    return s;
}

// Busca el primer sceneitem seleccionado en la escena; devuelve su ref (addref)
// o nullptr. El caller debe hacer obs_sceneitem_release.
static obs_sceneitem_t* firstSelectedItem(obs_scene_t* scene)
{
    obs_sceneitem_t* found = nullptr;
    obs_scene_enum_items(
        scene,
        [](obs_scene_t*, obs_sceneitem_t* item, void* p) -> bool {
            if (obs_sceneitem_selected(item)) {
                auto** out = static_cast<obs_sceneitem_t**>(p);
                obs_sceneitem_addref(item);
                *out = item;
                return false;   // corta la enumeración
            }
            return true;
        },
        &found);
    return found;
}

// ── acciones ─────────────────────────────────────────────────────────────────

// Crea una nueva browser source y la agrega centrada a la escena actual.
static void addBox(const QString& url, const QString& name, int w, int h, bool audio)
{
    obs_source_t* sceneSrc = obs_frontend_get_current_scene();
    if (!sceneSrc) {
        QMessageBox::warning(mainWindow(), T(S::WebNoSceneTitle), T(S::WebNoSceneBody));
        return;
    }
    obs_scene_t* scene = obs_scene_from_source(sceneSrc);
    if (!scene) {
        obs_source_release(sceneSrc);
        QMessageBox::warning(mainWindow(), T(S::WebNoSceneTitle), T(S::WebNoSceneBody));
        return;
    }

    obs_data_t*   settings = buildSettings(url, w, h, audio);
    obs_source_t* src = obs_source_create(
        kBrowserId, uniqueSourceName(name).toUtf8().constData(), settings, nullptr);
    obs_data_release(settings);

    if (!src) {   // obs-browser ausente en esta instalación de OBS
        obs_source_release(sceneSrc);
        QMessageBox::warning(mainWindow(), T(S::WebNoBrowserTitle), T(S::WebNoBrowserBody));
        return;
    }

    obs_sceneitem_t* item = obs_scene_add(scene, src);
    obs_source_release(src);   // la escena mantiene su propia referencia

    // Centra el recuadro en el lienzo.
    struct obs_video_info ovi;
    if (item && obs_get_video_info(&ovi)) {
        struct vec2 pos;
        pos.x = (float)((int)ovi.base_width  - w) / 2.0f;
        pos.y = (float)((int)ovi.base_height - h) / 2.0f;
        if (pos.x < 0) pos.x = 0;
        if (pos.y < 0) pos.y = 0;
        obs_sceneitem_set_pos(item, &pos);
        obs_sceneitem_select(item, true);
    }

    obs_source_release(sceneSrc);
    QMessageBox::information(mainWindow(), T(S::WebAddedTitle), T(S::WebAddedBody));
}

// Aplica URL/tamaño/audio al recuadro web seleccionado en la escena actual.
static void applyToSelected(const QString& url, int w, int h, bool audio)
{
    obs_source_t* sceneSrc = obs_frontend_get_current_scene();
    if (!sceneSrc) {
        QMessageBox::warning(mainWindow(), T(S::WebNoSceneTitle), T(S::WebNoSceneBody));
        return;
    }
    obs_scene_t* scene = obs_scene_from_source(sceneSrc);

    obs_sceneitem_t* item = scene ? firstSelectedItem(scene) : nullptr;
    if (!item) {
        obs_source_release(sceneSrc);
        QMessageBox::warning(mainWindow(), T(S::WebNoSelTitle), T(S::WebNoSelBody));
        return;
    }

    obs_source_t* src = obs_sceneitem_get_source(item);
    const char*   id  = src ? obs_source_get_id(src) : "";
    if (!id || std::strcmp(id, kBrowserId) != 0) {
        obs_sceneitem_release(item);
        obs_source_release(sceneSrc);
        QMessageBox::warning(mainWindow(), T(S::WebNoSelTitle), T(S::WebNotBrowserBody));
        return;
    }

    obs_data_t* settings = buildSettings(url, w, h, audio);
    obs_source_update(src, settings);
    obs_data_release(settings);

    obs_sceneitem_release(item);
    obs_source_release(sceneSrc);
    QMessageBox::information(mainWindow(), T(S::WebUpdatedTitle), T(S::WebUpdatedBody));
}

// Amplía la fuente seleccionada: toma su recorte actual (el que hiciste con
// Alt+arrastrar) y escala esa región para llenar un recuadro de boxW×boxH,
// usando bounds de OBS para respetar la proporción. Zoom fijo a una región.
static void zoomSelected(int boxW, int boxH)
{
    obs_source_t* sceneSrc = obs_frontend_get_current_scene();
    if (!sceneSrc) {
        QMessageBox::warning(mainWindow(), T(S::WebNoSceneTitle), T(S::WebNoSceneBody));
        return;
    }
    obs_scene_t* scene = obs_scene_from_source(sceneSrc);

    obs_sceneitem_t* item = scene ? firstSelectedItem(scene) : nullptr;
    if (!item) {
        obs_source_release(sceneSrc);
        QMessageBox::warning(mainWindow(), T(S::WebZoomBadCropTitle), T(S::WebZoomBadCropBody));
        return;
    }

    obs_source_t* src = obs_sceneitem_get_source(item);
    const int sw = src ? (int)obs_source_get_width(src)  : 0;
    const int sh = src ? (int)obs_source_get_height(src) : 0;

    struct obs_sceneitem_crop crop;
    obs_sceneitem_get_crop(item, &crop);
    const int rw = sw - crop.left - crop.right;    // ancho de la región recortada
    const int rh = sh - crop.top  - crop.bottom;   // alto  de la región recortada

    if (sw <= 0 || sh <= 0 || rw <= 0 || rh <= 0) {
        obs_sceneitem_release(item);
        obs_source_release(sceneSrc);
        QMessageBox::warning(mainWindow(), T(S::WebZoomBadCropTitle), T(S::WebZoomBadCropBody));
        return;
    }

    // La región recortada se escala para caber dentro del recuadro, centrada y
    // conservando la proporción (SCALE_INNER).
    obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_INNER);
    obs_sceneitem_set_bounds_alignment(item, OBS_ALIGN_CENTER);
    struct vec2 b;
    b.x = (float)boxW;
    b.y = (float)boxH;
    obs_sceneitem_set_bounds(item, &b);
    obs_sceneitem_select(item, true);

    obs_sceneitem_release(item);
    obs_source_release(sceneSrc);
    QMessageBox::information(mainWindow(), T(S::WebZoomedTitle), T(S::WebZoomedBody));
}

// ── UI ───────────────────────────────────────────────────────────────────────

void register_web_dock(void)
{
    QSettings st("MAVSoft", "EasyOBSBackups");

    // OBS toma ownership del widget (se remueve vía obs_frontend_remove_dock).
    auto* w = new QWidget();
    w->setObjectName("EasyOBSWebStream");

    auto* root = new QVBoxLayout(w);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    auto* urlEdit = new QLineEdit(w);
    urlEdit->setPlaceholderText(T(S::WebUrlPh));
    urlEdit->setText(st.value("webLastUrl", "").toString());

    auto* nameEdit = new QLineEdit(w);
    nameEdit->setPlaceholderText(T(S::WebNamePh));

    auto* widthSpin = new QSpinBox(w);
    widthSpin->setRange(16, 7680);
    widthSpin->setValue(st.value("webLastW", 1280).toInt());

    auto* heightSpin = new QSpinBox(w);
    heightSpin->setRange(16, 4320);
    heightSpin->setValue(st.value("webLastH", 720).toInt());

    auto* audioChk = new QCheckBox(T(S::WebWithAudio), w);
    audioChk->setChecked(st.value("webLastAudio", false).toBool());

    // Activado por defecto: convierte la URL de Twitch/YouTube/Kick al player
    // embebido para ver solo el directo y no la página entera.
    auto* embedChk = new QCheckBox(T(S::WebEmbedOnly), w);
    embedChk->setChecked(st.value("webLastEmbed", true).toBool());

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    form->addRow(T(S::WebUrlLabel), urlEdit);
    form->addRow(QString(), nameEdit);

    auto* sizeRow = new QHBoxLayout;
    sizeRow->addWidget(new QLabel(T(S::WebWidth), w));
    sizeRow->addWidget(widthSpin);
    sizeRow->addSpacing(8);
    sizeRow->addWidget(new QLabel(T(S::WebHeight), w));
    sizeRow->addWidget(heightSpin);
    sizeRow->addStretch();

    root->addLayout(form);
    root->addLayout(sizeRow);
    root->addWidget(embedChk);
    root->addWidget(audioChk);

    auto* addBtn = new QPushButton(T(S::WebAddBtn), w);
    addBtn->setMinimumHeight(40);
    addBtn->setCursor(Qt::PointingHandCursor);
    addBtn->setStyleSheet(
        "QPushButton{background:#1f7a34;color:white;font-weight:bold;"
        "border:none;border-radius:6px;padding:6px;}"
        "QPushButton:hover{background:#25923f;}");

    auto* applyBtn = new QPushButton(T(S::WebApplyBtn), w);
    applyBtn->setCursor(Qt::PointingHandCursor);

    root->addWidget(addBtn);
    root->addWidget(applyBtn);

    auto* info = new QLabel(T(S::WebInfo), w);
    info->setWordWrap(true);
    info->setStyleSheet("color: gray;");
    root->addWidget(info);

    // ── Zoom / recorte ────────────────────────────────────────────────────────
    auto* sep = new QFrame(w);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    root->addWidget(sep);

    auto* zoomBtn = new QPushButton(T(S::WebZoomBtn), w);
    zoomBtn->setCursor(Qt::PointingHandCursor);
    root->addWidget(zoomBtn);

    auto* zoomInfo = new QLabel(T(S::WebZoomInfo), w);
    zoomInfo->setWordWrap(true);
    zoomInfo->setStyleSheet("color: gray;");
    root->addWidget(zoomInfo);

    root->addStretch();

    // Persiste los últimos valores para no re-tipearlos en la próxima sesión.
    auto persist = [urlEdit, widthSpin, heightSpin, audioChk, embedChk]() {
        QSettings s("MAVSoft", "EasyOBSBackups");
        s.setValue("webLastUrl",   urlEdit->text());
        s.setValue("webLastW",     widthSpin->value());
        s.setValue("webLastH",     heightSpin->value());
        s.setValue("webLastAudio", audioChk->isChecked());
        s.setValue("webLastEmbed", embedChk->isChecked());
    };

    // Toda llamada a OBS va envuelta: los slots de Qt no deben dejar escapar una
    // excepción hacia el bucle de eventos.
    auto guard = [](auto fn) {
        try {
            fn();
        } catch (const std::exception& e) {
            blog(LOG_ERROR, "[EasyBackupandDelay] web dock: %s", e.what());
        } catch (...) {
            blog(LOG_ERROR, "[EasyBackupandDelay] web dock: unknown exception");
        }
    };

    QObject::connect(addBtn, &QPushButton::clicked, w,
        [=]() {
            guard([&]() {
                const QString raw = urlEdit->text().trimmed();
                if (raw.isEmpty()) {
                    QMessageBox::warning(mainWindow(), T(S::WebNoUrlTitle), T(S::WebNoUrlBody));
                    return;
                }
                const bool audio = audioChk->isChecked();
                const QString url = embedChk->isChecked() ? toEmbedUrl(raw, audio) : raw;
                addBox(url, nameEdit->text(), widthSpin->value(), heightSpin->value(), audio);
                persist();
            });
        });

    QObject::connect(applyBtn, &QPushButton::clicked, w,
        [=]() {
            guard([&]() {
                const QString raw = urlEdit->text().trimmed();
                if (raw.isEmpty()) {
                    QMessageBox::warning(mainWindow(), T(S::WebNoUrlTitle), T(S::WebNoUrlBody));
                    return;
                }
                const bool audio = audioChk->isChecked();
                const QString url = embedChk->isChecked() ? toEmbedUrl(raw, audio) : raw;
                applyToSelected(url, widthSpin->value(), heightSpin->value(), audio);
                persist();
            });
        });

    QObject::connect(zoomBtn, &QPushButton::clicked, w,
        [=]() {
            guard([&]() {
                zoomSelected(widthSpin->value(), heightSpin->value());
            });
        });

    obs_frontend_add_dock_by_id(kDockId, T(S::WebDockTitle).toUtf8().constData(), w);
}

void unregister_web_dock(void)
{
    obs_frontend_remove_dock(kDockId);
}
