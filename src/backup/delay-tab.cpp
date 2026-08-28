#include "delay-tab.hpp"
#include "delay-visibility.hpp"
#include "mic-mute.hpp"
#include "delay-playing.hpp"
#include "i18n.hpp"

#include <obs.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QStringList>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QHeaderView>
#include <QTableWidget>
#include <QTableWidgetItem>

#include <vector>
#include <algorithm>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>   // VK_*, códigos que sondea el dock

// Combo global de "activar/desactivar todos los delays" — definido en delay-dock.cpp.
std::vector<int> easyobs_get_toggle_keys(void);
void             easyobs_set_toggle_keys(const std::vector<int>& vks);

// ── Helpers de teclas ─────────────────────────────────────────────────────────

// Normaliza las variantes izquierda/derecha de los modificadores a la tecla genérica,
// así el sondeo con GetAsyncKeyState(VK_SHIFT/CONTROL/MENU) coincide sin importar cuál
// se apretó.
static int normalizeVk(int vk)
{
    switch (vk) {
    case VK_LSHIFT:   case VK_RSHIFT:   return VK_SHIFT;
    case VK_LCONTROL: case VK_RCONTROL: return VK_CONTROL;
    case VK_LMENU:    case VK_RMENU:    return VK_MENU;
    default: return vk;
    }
}

// Nombre legible de un VK para mostrar en el botón.
static QString vkName(int vk)
{
    if (vk >= 'A' && vk <= 'Z') return QString(QChar(vk));
    if (vk >= '0' && vk <= '9') return QString(QChar(vk));
    if (vk >= VK_F1 && vk <= VK_F12) return QString("F%1").arg(vk - VK_F1 + 1);
    switch (vk) {
    case VK_CONTROL:  return "Ctrl";
    case VK_MENU:     return "Alt";
    case VK_SHIFT:    return "Shift";
    case VK_SPACE:    return "Espacio";
    case VK_TAB:      return "Tab";
    case VK_CAPITAL:  return "Bloq Mayús";
    case VK_RETURN:   return "Enter";
    case VK_BACK:     return "Backspace";
    case VK_XBUTTON1: return "Mouse X1";
    case VK_XBUTTON2: return "Mouse X2";
    case VK_LEFT:     return QString::fromUtf8("\xE2\x86\x90");   // ←
    case VK_RIGHT:    return QString::fromUtf8("\xE2\x86\x92");   // →
    case VK_UP:       return QString::fromUtf8("\xE2\x86\x91");   // ↑
    case VK_DOWN:     return QString::fromUtf8("\xE2\x86\x93");   // ↓
    default:          return QString("VK 0x%1").arg(vk, 2, 16, QChar('0')).toUpper();
    }
}

static QString comboText(const std::vector<int>& vks)
{
    QStringList parts;
    for (int vk : vks) parts << vkName(vk);
    return parts.join(" + ");
}

// ── Nombres de las fuentes puestas en alguna escena ───────────────────────────
// La visibilidad se aplica por nombre de fuente, así que la tabla ofrece lo que
// esté en alguna escena (incluyendo grupos y lo que hay adentro de ellos).

static bool collect_item(obs_scene_t*, obs_sceneitem_t* item, void* param)
{
    auto* out = static_cast<QStringList*>(param);
    if (obs_source_t* src = obs_sceneitem_get_source(item)) {
        const char* n = obs_source_get_name(src);
        if (n && *n) {
            const QString q = QString::fromUtf8(n);
            if (!out->contains(q)) out->append(q);
        }
    }
    if (obs_sceneitem_is_group(item))
        obs_sceneitem_group_enum_items(item, collect_item, param);
    return true;   // seguir enumerando
}

static bool collect_scene(void* param, obs_source_t* sceneSrc)
{
    if (obs_scene_t* scene = obs_scene_from_source(sceneSrc))
        obs_scene_enum_items(scene, collect_item, param);
    return true;
}

static QStringList sceneSourceNames()
{
    QStringList names;
    obs_enum_scenes(collect_scene, &names);
    names.sort(Qt::CaseInsensitive);
    return names;
}

// ── Fuentes con audio (para elegir cuál silencia el botón del dock) ──────────

static bool collect_audio(void* param, obs_source_t* src)
{
    auto* out = static_cast<QStringList*>(param);
    if (obs_source_get_output_flags(src) & OBS_SOURCE_AUDIO) {
        const char* n = obs_source_get_name(src);
        if (n && *n) {
            const QString q = QString::fromUtf8(n);
            if (!out->contains(q)) out->append(q);
        }
    }
    return true;
}

static QStringList audioSourceNames()
{
    QStringList names;
    obs_enum_sources(collect_audio, &names);
    names.sort(Qt::CaseInsensitive);
    return names;
}

// ── KeyCaptureButton ──────────────────────────────────────────────────────────

KeyCaptureButton::KeyCaptureButton(QWidget* parent)
    : QPushButton(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
    setMinimumWidth(180);
    refreshText();
}

void KeyCaptureButton::setKeys(const std::vector<int>& vks)
{
    m_vks.clear();
    for (int vk : vks) {
        int n = normalizeVk(vk);
        if (n > 0 && n <= 255 &&
            std::find(m_vks.begin(), m_vks.end(), n) == m_vks.end())
            m_vks.push_back(n);
    }
    refreshText();
}

void KeyCaptureButton::refreshText()
{
    if (m_listening) {
        setText(m_capture.empty()
                ? QStringLiteral("Presioná la(s) tecla(s)…  (Esc = ninguna)")
                : comboText(m_capture));
        setStyleSheet("font-weight:bold; color:#e0a020;");
    } else {
        setText(m_vks.empty() ? QStringLiteral("Sin asignar (click para elegir)")
                              : comboText(m_vks));
        setStyleSheet("");
    }
}

void KeyCaptureButton::startListening()
{
    m_listening = true;
    m_capture.clear();
    m_down.clear();
    grabKeyboard();
    refreshText();
}

void KeyCaptureButton::finalize(bool cancelled)
{
    m_listening = false;
    releaseKeyboard();
    if (!cancelled) {
        m_vks = m_capture;                 // vacío = "ninguna" (p. ej. tras Esc)
        if (onChanged) onChanged(m_vks);
    }
    refreshText();
}

void KeyCaptureButton::mousePressEvent(QMouseEvent* e)
{
    if (!m_listening) {
        if (e->button() == Qt::LeftButton) { startListening(); e->accept(); return; }
        QPushButton::mousePressEvent(e);
        return;
    }
    // Escuchando: botón derecho cancela; los laterales del mouse se agregan al combo.
    if (e->button() == Qt::RightButton) { finalize(true); e->accept(); return; }
    int vk = 0;
    if (e->button() == Qt::XButton1) vk = VK_XBUTTON1;
    else if (e->button() == Qt::XButton2) vk = VK_XBUTTON2;
    if (vk) {
        if (std::find(m_capture.begin(), m_capture.end(), vk) == m_capture.end())
            m_capture.push_back(vk);
        m_down.insert(vk);
        refreshText();
    }
    e->accept();
}

void KeyCaptureButton::mouseReleaseEvent(QMouseEvent* e)
{
    if (!m_listening) { QPushButton::mouseReleaseEvent(e); return; }
    int vk = 0;
    if (e->button() == Qt::XButton1) vk = VK_XBUTTON1;
    else if (e->button() == Qt::XButton2) vk = VK_XBUTTON2;
    if (vk) {
        m_down.erase(vk);
        if (m_down.empty() && !m_capture.empty()) finalize(false);
    }
    e->accept();
}

void KeyCaptureButton::keyPressEvent(QKeyEvent* e)
{
    if (!m_listening) { QPushButton::keyPressEvent(e); return; }
    if (e->isAutoRepeat()) { e->accept(); return; }

    const int raw = (int)e->nativeVirtualKey();
    if (raw == VK_ESCAPE) { m_capture.clear(); finalize(false); return; }   // ninguna

    const int vk = normalizeVk(raw);
    if (vk > 0 && vk <= 255) {
        if (std::find(m_capture.begin(), m_capture.end(), vk) == m_capture.end())
            m_capture.push_back(vk);
        m_down.insert(vk);
        refreshText();
    }
    e->accept();
}

void KeyCaptureButton::keyReleaseEvent(QKeyEvent* e)
{
    if (!m_listening) { QPushButton::keyReleaseEvent(e); return; }
    if (e->isAutoRepeat()) { e->accept(); return; }

    m_down.erase(normalizeVk((int)e->nativeVirtualKey()));
    if (m_down.empty() && !m_capture.empty()) finalize(false);   // soltaste todo → confirmar
    e->accept();
}

void KeyCaptureButton::focusOutEvent(QFocusEvent* e)
{
    if (m_listening) finalize(true);   // perdiste el foco a mitad → cancelar
    QPushButton::focusOutEvent(e);
}

// ── DelayTabPage ──────────────────────────────────────────────────────────────

DelayTabPage::DelayTabPage(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(12);

    auto* delayGroup = new QGroupBox(T(S::DelaySwitchGroup), this);
    auto* delayLayout = new QVBoxLayout(delayGroup);

    auto* keyRow = new QHBoxLayout;
    keyRow->addWidget(new QLabel(T(S::DelaySwitchKeyLabel), this));

    keyCapture = new KeyCaptureButton(this);
    keyCapture->setKeys(easyobs_get_toggle_keys());
    keyCapture->onChanged = [](const std::vector<int>& vks) {
        easyobs_set_toggle_keys(vks);
    };
    keyRow->addWidget(keyCapture);
    keyRow->addStretch();
    delayLayout->addLayout(keyRow);

    auto* delayInfo = new QLabel(T(S::DelaySwitchInfo), this);
    delayInfo->setWordWrap(true);
    delayInfo->setStyleSheet("color: gray;");
    delayLayout->addWidget(delayInfo);

    layout->addWidget(delayGroup);

    // ── Micrófono: qué fuente silencia el botón del dock, y con qué tecla ────
    auto* micGroup  = new QGroupBox(T(S::MicGroup), this);
    auto* micLayout = new QVBoxLayout(micGroup);

    auto* micRow = new QHBoxLayout;
    micRow->addWidget(new QLabel(T(S::MicSourceLabel), this));

    micCombo = new QComboBox(this);
    micCombo->addItem(T(S::MicSourceAuto), QString());   // userData vacío = automático
    for (const QString& n : audioSourceNames())
        micCombo->addItem(n, n);
    {
        const QString cur = easyobs_get_mic_source_name();
        int idx = cur.isEmpty() ? 0 : micCombo->findData(cur);
        if (idx < 0) {   // guardada de antes pero ya no existe: mostrarla igual
            micCombo->addItem(cur, cur);
            idx = micCombo->count() - 1;
        }
        micCombo->setCurrentIndex(idx);
    }
    QObject::connect(micCombo, &QComboBox::currentIndexChanged, micCombo, [this](int) {
        easyobs_set_mic_source_name(micCombo->currentData().toString());
    });
    micRow->addWidget(micCombo, 1);
    micLayout->addLayout(micRow);

    auto* micKeyRow = new QHBoxLayout;
    micKeyRow->addWidget(new QLabel(T(S::MicKeyLabel), this));
    micKeyCapture = new KeyCaptureButton(this);
    micKeyCapture->setKeys(easyobs_get_mic_keys());
    micKeyCapture->onChanged = [](const std::vector<int>& vks) {
        easyobs_set_mic_keys(vks);
    };
    micKeyRow->addWidget(micKeyCapture);
    micKeyRow->addStretch();
    micLayout->addLayout(micKeyRow);

    auto* micInfo = new QLabel(T(S::MicInfo), this);
    micInfo->setWordWrap(true);
    micInfo->setStyleSheet("color: gray;");
    micLayout->addWidget(micInfo);

    layout->addWidget(micGroup);

    // ── Luz y aviso del audio demorado ───────────────────────────────────────
    auto* alertGroup  = new QGroupBox(T(S::AlertGroup), this);
    auto* alertLayout = new QVBoxLayout(alertGroup);

    alertCheck = new QCheckBox(T(S::AlertEnable), this);
    alertCheck->setChecked(easyobs_alert_enabled());
    QObject::connect(alertCheck, &QCheckBox::toggled, alertCheck, [](bool on) {
        easyobs_set_alert_enabled(on);
    });
    alertLayout->addWidget(alertCheck);

    auto* leadRow = new QHBoxLayout;
    leadRow->addWidget(new QLabel(T(S::AlertLead), this));
    alertLead = new QComboBox(this);
    for (int i = 1; i <= 10; ++i)
        alertLead->addItem(QStringLiteral("%1 s").arg(i), i);
    {
        const int idx = alertLead->findData(easyobs_alert_lead_sec());
        alertLead->setCurrentIndex(idx >= 0 ? idx : 1);   // 2 s por defecto
    }
    QObject::connect(alertLead, &QComboBox::currentIndexChanged, alertLead, [this](int) {
        easyobs_set_alert_lead_sec(alertLead->currentData().toInt());
    });
    leadRow->addWidget(alertLead);
    leadRow->addStretch();
    alertLayout->addLayout(leadRow);

    monitorCheck = new QCheckBox(T(S::MonitorEnable), this);
    monitorCheck->setChecked(easyobs_delay_monitor_enabled());
    QObject::connect(monitorCheck, &QCheckBox::toggled, monitorCheck, [](bool on) {
        easyobs_set_delay_monitor(on);
    });
    alertLayout->addWidget(monitorCheck);

    auto* monInfo = new QLabel(T(S::MonitorInfo), this);
    monInfo->setWordWrap(true);
    monInfo->setStyleSheet("color: gray;");
    alertLayout->addWidget(monInfo);

    auto* devRow = new QHBoxLayout;
    devRow->addWidget(new QLabel(T(S::AlertDevice), this));

    alertDevice = new QComboBox(this);
    alertDevice->addItem(T(S::AlertDeviceDefault), -1);   // WAVE_MAPPER
    {
        const QStringList devs = easyobs_alert_device_names();
        for (int i = 0; i < devs.size(); ++i) alertDevice->addItem(devs.at(i), i);

        const int cur = easyobs_alert_device();
        const int idx = alertDevice->findData(cur);
        alertDevice->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    QObject::connect(alertDevice, &QComboBox::currentIndexChanged, alertDevice, [this](int) {
        easyobs_set_alert_device(alertDevice->currentData().toInt());
    });
    devRow->addWidget(alertDevice, 1);

    auto* testBtn = new QPushButton(T(S::AlertTest), this);
    testBtn->setCursor(Qt::PointingHandCursor);
    QObject::connect(testBtn, &QPushButton::clicked, testBtn, []() { easyobs_play_alert(); });
    devRow->addWidget(testBtn);

    alertLayout->addLayout(devRow);

    auto* alertInfo = new QLabel(T(S::AlertInfo), this);
    alertInfo->setWordWrap(true);
    alertInfo->setStyleSheet("color: gray;");
    alertLayout->addWidget(alertInfo);

    layout->addWidget(alertGroup);

    // ── Fuentes atadas al estado del delay ───────────────────────────────────
    auto* visGroup  = new QGroupBox(T(S::DelayVisGroup), this);
    auto* visLayout = new QVBoxLayout(visGroup);

    const QStringList names = sceneSourceNames();
    if (names.isEmpty()) {
        auto* empty = new QLabel(T(S::DelayVisEmpty), this);
        empty->setStyleSheet("color: gray;");
        visLayout->addWidget(empty);
    } else {
        visTable = new QTableWidget(0, 2, this);
        visTable->setHorizontalHeaderLabels({ T(S::DelayVisColSource), T(S::DelayVisColWhen) });
        visTable->verticalHeader()->setVisible(false);
        visTable->setSelectionMode(QAbstractItemView::NoSelection);
        visTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        visTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        visTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        visTable->setMinimumHeight(220);

        for (const QString& name : names) {
            const int row = visTable->rowCount();
            visTable->insertRow(row);
            visTable->setItem(row, 0, new QTableWidgetItem(name));

            // El orden de los ítems coincide con DelayVisMode (0/1/2).
            auto* combo = new QComboBox(visTable);
            combo->addItem(T(S::DelayVisIgnore));
            combo->addItem(T(S::DelayVisWithDelay));
            combo->addItem(T(S::DelayVisWithoutDelay));
            combo->setCurrentIndex(static_cast<int>(easyobs_get_delay_visibility(name)));

            // Se guarda y se aplica al instante: no hay botón de "Guardar".
            QObject::connect(combo, &QComboBox::currentIndexChanged, combo, [name](int idx) {
                easyobs_set_delay_visibility(name, static_cast<DelayVisMode>(idx));
            });
            visTable->setCellWidget(row, 1, combo);
        }
        visLayout->addWidget(visTable);
    }

    auto* visInfo = new QLabel(T(S::DelayVisInfo), this);
    visInfo->setWordWrap(true);
    visInfo->setStyleSheet("color: gray;");
    visLayout->addWidget(visInfo);

    layout->addWidget(visGroup);
    layout->addStretch();
}
