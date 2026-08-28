#include "backup-tab.hpp"
#include "i18n.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QProgressBar>
#include <QTextEdit>
#include <QPushButton>
#include <QFont>
#include <QDesktopServices>
#include <QUrl>
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QByteArray>
#include <QSvgRenderer>
#include <QSize>

// Versión del plugin — viene de project() en CMake; el fallback solo aplica si se
// compila fuera de CMake.
#ifndef EASYOBS_VERSION
#define EASYOBS_VERSION "dev"
#endif

// ── Logos de redes sociales (SVG inline, rasterizados vía Qt Svg) ────────────
// Se mantienen como SVG autocontenido para no distribuir archivos de iconos.
namespace {

const char* const kSvgTwitch =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
    "<path fill='#9146FF' d='M4 2 3 6.5V19h4v3l3-3h4l5-5V2H4z'/>"
    "<path fill='#fff' d='M10.5 7h1.8v5h-1.8zM15 7h1.8v5H15z'/></svg>";

const char* const kSvgYouTube =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
    "<rect x='1' y='5' width='22' height='14' rx='4' fill='#FF0000'/>"
    "<path fill='#fff' d='M10 8.3v7.4L16 12z'/></svg>";

const char* const kSvgKick =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
    "<g fill='#53FC18'><rect x='3' y='4' width='4.2' height='16'/>"
    "<rect x='7.2' y='8.2' width='4.2' height='3.6'/>"
    "<rect x='11.4' y='4' width='4.2' height='4.2'/>"
    "<rect x='11.4' y='11.8' width='4.2' height='4.2'/>"
    "<rect x='7.2' y='12.2' width='4.2' height='3.6'/></g></svg>";

const char* const kSvgTikTok =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
    "<path fill='#25F4EE' d='M15 3h-2.6v13.2a2.7 2.7 0 1 1-2.6-2.7c.3 0 .5 0 .7.1v-2.7a5.4 5.4 0 1 0 4.5 5.3V9.4a6.5 6.5 0 0 0 4 1.3V8a4 4 0 0 1-4-4z'/>"
    "<path fill='#FE2C55' d='M16 4h-1a4 4 0 0 0 3 3.6v-.9A4 4 0 0 1 16 4z'/></svg>";

const char* const kSvgInstagram =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
    "<rect x='2.5' y='2.5' width='19' height='19' rx='5.5' fill='none' stroke='#E4405F' stroke-width='2'/>"
    "<circle cx='12' cy='12' r='4.2' fill='none' stroke='#E4405F' stroke-width='2'/>"
    "<circle cx='17.3' cy='6.7' r='1.3' fill='#E4405F'/></svg>";

// Renderiza un string SVG a un QIcon nítido en el tamaño lógico dado (considera DPR).
QIcon svgIcon(const char* svg, int px = 18)
{
    QByteArray data(svg);
    QSvgRenderer r(data);
    const qreal dpr = 2.0;
    QPixmap pm(int(px * dpr), int(px * dpr));
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    r.render(&p);
    p.end();
    pm.setDevicePixelRatio(dpr);
    return QIcon(pm);
}

} // namespace

BackupTabPage::BackupTabPage(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(8);

    statusLabel = new QLabel(T(S::BackupStatus), this);
    statusLabel->setWordWrap(true);
    layout->addWidget(statusLabel);

    progress = new QProgressBar(this);
    progress->setRange(0, 100);
    progress->setValue(0);
    layout->addWidget(progress);

    log = new QTextEdit(this);
    log->setReadOnly(true);
    log->setFont(QFont("Consolas", 9));
    layout->addWidget(log);

    startBtn = new QPushButton(T(S::StartBackup), this);
    startBtn->setMinimumHeight(36);
    layout->addWidget(startBtn);

    // El panel de apoyo ya no vive acá: lo muestra BackupDialog arriba de las
    // solapas, así se ve desde cualquiera de ellas y no solo desde Copia.

    auto* socialGroup = new QGroupBox(T(S::FollowTitle), this);
    auto* socialLayout = new QVBoxLayout(socialGroup);
    auto* socialIntro = new QLabel(T(S::FollowIntro), this);
    socialIntro->setWordWrap(true);
    socialLayout->addWidget(socialIntro);

    auto* socialBtns = new QHBoxLayout;
    const QString h = QStringLiteral("miguelangelcsgo");
    struct Social { const char* label; const char* svg; QString url; };
    const Social socials[] = {
        { "Twitch",    kSvgTwitch,    "https://twitch.tv/" + h },
        { "YouTube",   kSvgYouTube,   "https://youtube.com/@" + h },
        { "Kick",      kSvgKick,      "https://kick.com/" + h },
        { "TikTok",    kSvgTikTok,    "https://tiktok.com/@" + h },
        { "Instagram", kSvgInstagram, "https://instagram.com/" + h },
    };
    for (const auto& s : socials) {
        auto* b = new QPushButton(svgIcon(s.svg), QString::fromUtf8(s.label), this);
        b->setIconSize(QSize(18, 18));
        const QString url = s.url;
        connect(b, &QPushButton::clicked, this,
                [url]{ QDesktopServices::openUrl(QUrl(url)); });
        socialBtns->addWidget(b);
    }
    socialLayout->addLayout(socialBtns);
    layout->addWidget(socialGroup);

    auto* versionLabel = new QLabel("EasyBackupandDelay v" EASYOBS_VERSION, this);
    versionLabel->setStyleSheet("color: gray;");
    versionLabel->setAlignment(Qt::AlignRight);
    layout->addWidget(versionLabel);
}
