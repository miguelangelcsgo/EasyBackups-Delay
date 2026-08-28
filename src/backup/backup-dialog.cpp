#include "backup-dialog.hpp"
#include "settings-tab.hpp"
#include "delay-tab.hpp"
#include "backup-tab.hpp"
#include "restore-tab.hpp"
#include "i18n.hpp"

#include <obs-module.h>
#include <exception>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QGroupBox>
#include <QTabWidget>
#include <QTreeWidget>
#include <QMessageBox>
#include <QDateTime>
#include <QFileDialog>
#include <QDir>
#include <QProcess>
#include <QCoreApplication>
#include <QFile>


void BackupWorker::run()
{
    try {
        if (m_mode == Mode::Backup) {
            auto result = m_mgr->runBackup(
                [this](const std::string& f, int pct, int cur, int tot) {
                    emit progress(QString::fromStdString(f), pct, cur, tot);
                },
                [this](const std::string& msg) {
                    emit log(QString::fromStdString(msg));
                });

            emit backupFinished(result.success,
                                QString::fromStdString(result.errorMsg));

        } else if (m_mode == Mode::Restore) {
            auto result = m_mgr->runRestore(m_manifest,
                [this](const std::string& f, int pct, int cur, int tot) {
                    emit progress(QString::fromStdString(f), pct, cur, tot);
                },
                [this](const std::string& msg) {
                    emit log(QString::fromStdString(msg));
                });

            emit restoreFinished(result.success,
                                 QString::fromStdString(result.errorMsg));

        } else if (m_mode == Mode::LoadManifest) {
            bool ok = m_mgr->loadRemoteManifest(m_manifest,
                [this](const std::string& msg) {
                    emit log(QString::fromStdString(msg));
                });
            emit manifestLoaded(ok);
        }
    } catch (const std::exception& e) {
        blog(LOG_ERROR, "[EasyBackupandDelay] Worker exception (%d): %s", (int)m_mode, e.what());
        QString err = QString("Error: ") + e.what();
        if (m_mode == Mode::Backup)       emit backupFinished(false, err);
        else if (m_mode == Mode::Restore) emit restoreFinished(false, err);
        else                              emit manifestLoaded(false);
    } catch (...) {
        blog(LOG_ERROR, "[EasyBackupandDelay] Worker unknown exception (mode %d)", (int)m_mode);
        if (m_mode == Mode::Backup)       emit backupFinished(false, "Unknown error");
        else if (m_mode == Mode::Restore) emit restoreFinished(false, "Unknown error");
        else                              emit manifestLoaded(false);
    }
}

BackupDialog::BackupDialog(QWidget* parent)
    : QDialog(parent)
{
    setMinimumSize(620, 560);

    m_manager.loadConfig();
    buildUI();
}

BackupDialog::~BackupDialog()
{
    stopWorker();
}

void BackupDialog::buildUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(8, 8, 8, 8);

    auto* langRow = new QHBoxLayout;
    langRow->addStretch();
    langRow->addWidget(new QLabel("🌐", this));
    m_langCombo = new QComboBox(this);
    for (int i = 0; i < (int)Lang::COUNT; ++i)
        m_langCombo->addItem(langName((Lang)i));
    m_langCombo->setCurrentIndex((int)currentLang());
    m_langCombo->setMaximumWidth(150);
    langRow->addWidget(m_langCombo);
    m_mainLayout->addLayout(langRow);

    connect(m_langCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BackupDialog::onLanguageChanged);

    buildSupportBox();
    buildTabs();
}

// Panel de apoyo. Va en el layout principal, arriba del QTabWidget, para que se
// vea desde cualquier solapa y no solo desde Copia. El cambio de idioma rehace
// las solapas pero no esto, así que el texto se refresca aparte.
void BackupDialog::buildSupportBox()
{
    m_supportBox = new QGroupBox(this);
    auto* l = new QVBoxLayout(m_supportBox);
    l->setContentsMargins(8, 4, 8, 6);

    m_supportLabel = new QLabel(this);
    m_supportLabel->setWordWrap(true);
    m_supportLabel->setTextFormat(Qt::RichText);
    m_supportLabel->setOpenExternalLinks(true);
    l->addWidget(m_supportLabel);

    m_mainLayout->addWidget(m_supportBox);
    retranslateSupportBox();
}

void BackupDialog::retranslateSupportBox()
{
    if (!m_supportBox) return;
    m_supportBox->setTitle(T(S::DonateTitle));
    m_supportLabel->setText(
        T(S::DonateIntro) + "<br>👉 "
        "<a href=\"https://ceneka.net/miguelangelcsgo\">" + T(S::DonateWord) + "</a>"
        "&nbsp;&nbsp;•&nbsp;&nbsp;"
        "<a href=\"https://streamlabs.com/miguelangelcsgo/tip\">Streamlabs (tip)</a>");
}

void BackupDialog::buildTabs()
{
    setWindowTitle(T(S::WindowTitle));

    m_tabs = new QTabWidget(this);

    m_settingsTab = new SettingsTabPage(&m_manager, this);
    m_backupTab = new BackupTabPage(this);
    m_restoreTab = new RestoreTabPage(this);
    m_delayTab = new DelayTabPage(this);

    m_tabs->addTab(m_settingsTab, T(S::TabSettings));
    m_tabs->addTab(m_backupTab, T(S::TabBackup));
    m_tabs->addTab(m_restoreTab, T(S::TabRestore));
    m_tabs->addTab(m_delayTab, T(S::TabDelay));

    m_mainLayout->addWidget(m_tabs);

    connect(m_settingsTab->browseBtn, &QPushButton::clicked, this, &BackupDialog::onBrowseClicked);
    connect(m_settingsTab->saveBtn, &QPushButton::clicked, this, &BackupDialog::saveSettings);

    connect(m_backupTab->startBtn, &QPushButton::clicked, this, &BackupDialog::onBackupClicked);

    connect(m_restoreTab->refreshBtn, &QPushButton::clicked, this, &BackupDialog::onRefreshManifest);
    connect(m_restoreTab->restoreBtn, &QPushButton::clicked, this, &BackupDialog::onRestoreClicked);

    // El combo de teclas del delay se guarda solo (KeyCaptureButton::onChanged, en
    // DelayTabPage), así que acá no hace falta conectar nada.
}

void BackupDialog::onLanguageChanged(int index)
{
    if (index < 0 || index >= (int)Lang::COUNT) return;
    setCurrentLang((Lang)index);

    if (m_tabs) {
        m_mainLayout->removeWidget(m_tabs);
        delete m_tabs;
        m_tabs = nullptr;
    }

    retranslateSupportBox();

    m_settingsTab = nullptr;
    m_backupTab = nullptr;
    m_restoreTab = nullptr;
    m_delayTab = nullptr;

    buildTabs();
}

void BackupDialog::onBrowseClicked()
{
    QString start = m_settingsTab->localPathEdit->text().trimmed();
    QString dir = QFileDialog::getExistingDirectory(
        this, T(S::SelectBackupFolder), start);
    if (!dir.isEmpty())
        m_settingsTab->localPathEdit->setText(QDir::toNativeSeparators(dir));
}

void BackupDialog::saveSettings()
{
    m_manager.m_localPath = m_settingsTab->localPathEdit->text().trimmed().toStdString();
    m_manager.saveConfig();
    QMessageBox::information(this, T(S::SettingsSavedTitle), T(S::SettingsSavedBody));
}

void BackupDialog::onBackupClicked()
{
    m_manager.m_localPath = m_settingsTab->localPathEdit->text().trimmed().toStdString();
    if (!m_manager.folderReady()) {
        QMessageBox::warning(this, T(S::NotConnectedTitle), T(S::NotConnectedBody));
        m_tabs->setCurrentIndex(0);
        return;
    }

    m_backupTab->log->clear();
    m_backupTab->progress->setValue(0);
    startWorker(BackupWorker::Mode::Backup);
}

void BackupDialog::onRefreshManifest()
{
    m_manager.m_localPath = m_settingsTab->localPathEdit->text().trimmed().toStdString();
    m_restoreTab->manifestTree->clear();
    m_restoreTab->log->clear();
    m_restoreTab->restoreBtn->setEnabled(false);
    startWorker(BackupWorker::Mode::LoadManifest);
}

void BackupDialog::onRestoreClicked()
{
    if (m_loadedManifest.sceneCollections.empty() &&
        m_loadedManifest.profiles.empty() &&
        m_loadedManifest.mediaFiles.empty()) {
        QMessageBox::information(this, T(S::NoBackupTitle), T(S::NoBackupBody));
        return;
    }

    auto ret = QMessageBox::question(this, T(S::RestoreBackupTitle),
        T(S::RestoreConfirmBody));
    if (ret != QMessageBox::Yes) return;

    m_manager.m_restoreSceneName = m_restoreTab->sceneNameEdit->text().trimmed().toStdString();

    m_restoreTab->progress->setValue(0);
    m_restoreTab->log->clear();
    startWorker(BackupWorker::Mode::Restore, m_loadedManifest);
}

void BackupDialog::onProgress(const QString& file, int pct, int current, int total)
{
    (void)file;
    int overall = total > 0
        ? ((current - 1) * 100 + pct) / total
        : pct;

    if (m_activeMode == BackupWorker::Mode::Backup)
        m_backupTab->progress->setValue(overall);
    else
        m_restoreTab->progress->setValue(overall);
}

void BackupDialog::onLog(const QString& msg)
{
    const QString line =
        "[" + QDateTime::currentDateTime().toString("hh:mm:ss") + "] " + msg;

    if (m_activeMode == BackupWorker::Mode::Backup)
        m_backupTab->log->append(line);
    else
        m_restoreTab->log->append(line);
}

void BackupDialog::onBackupFinished(bool success, const QString& msg)
{
    stopWorker();
    setControlsEnabled(true);
    m_backupTab->progress->setValue(success ? 100 : 0);

    if (success)
        QMessageBox::information(this, T(S::BackupCompleteTitle), T(S::BackupCompleteBody));
    else
        QMessageBox::warning(this, T(S::BackupFailedTitle),
            msg.isEmpty() ? T(S::BackupFailedBody) : msg);
}

static void restartApp()
{
    const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const QString dir = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());

    QString bat = QDir::tempPath() + "/eobs_restart.bat";
    QFile f(bat);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    QString content =
        "@echo off\r\n"
        "timeout /t 1 >nul\r\n"
        "taskkill /f /im obs64.exe >nul 2>&1\r\n"
        "start \"\" /d \"" + dir + "\" \"" + exe + "\"\r\n"
        "del \"%~f0\"\r\n";
    f.write(content.toUtf8());
    f.close();

    QProcess::startDetached("cmd.exe",
        QStringList() << "/c" << QDir::toNativeSeparators(bat));
}

void BackupDialog::onRestoreFinished(bool success, const QString& msg)
{
    stopWorker();
    setControlsEnabled(true);
    m_restoreTab->progress->setValue(success ? 100 : 0);

    if (success) {
        auto ans = QMessageBox::question(this, T(S::RestoreCompleteTitle),
            T(S::RestoreCompleteBody),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (ans == QMessageBox::Yes)
            restartApp();
    } else {
        QMessageBox::warning(this, T(S::RestoreFailedTitle),
            msg.isEmpty() ? T(S::RestoreFailedBody) : msg);
    }
}

void BackupDialog::onManifestLoaded(bool ok)
{
    stopWorker();
    setControlsEnabled(true);

    if (!ok) {
        QMessageBox::warning(this, T(S::NoBackupFoundTitle),
            T(S::NoBackupFoundBodyFmt).arg(QStringLiteral("carpeta local")) +
            "\n\n" +
            QString::fromStdString(m_manager.lastError()) +
            "\n\n" + T(S::NoBackupFoundHint));
        return;
    }

    m_restoreTab->manifestTree->clear();

    auto addSection = [&](const QString& title,
                          const std::vector<RestoreItem>& items) {
        auto* root = new QTreeWidgetItem(m_restoreTab->manifestTree);
        root->setText(0, title + " (" + T(S::FilesCountFmt).arg(items.size()) + ")");
        root->setExpanded(true);
        for (auto& item : items) {
            auto* child = new QTreeWidgetItem(root);
            child->setText(0, QString::fromStdString(item.name));
            child->setText(1, title);
            child->setText(2, formatSize(item.size));
        }
    };

    addSection(T(S::SecSceneCollections), m_loadedManifest.sceneCollections);
    addSection(T(S::SecProfiles), m_loadedManifest.profiles);
    addSection(T(S::SecMediaFiles), m_loadedManifest.mediaFiles);

    if (!m_loadedManifest.sceneCollections.empty()) {
        QString base = QString::fromStdString(m_loadedManifest.sceneCollections.front().name);
        if (base.endsWith(".json", Qt::CaseInsensitive))
            base.chop(5);
        base.replace('_', ' ');
        m_restoreTab->sceneNameEdit->setText(base);
    }

    m_restoreTab->restoreBtn->setEnabled(true);
}

void BackupDialog::setControlsEnabled(bool enabled)
{
    m_backupTab->startBtn->setEnabled(enabled);
    m_restoreTab->refreshBtn->setEnabled(enabled);
    m_restoreTab->restoreBtn->setEnabled(enabled);

    m_settingsTab->saveBtn->setEnabled(enabled);
    m_settingsTab->browseBtn->setEnabled(enabled);
    m_settingsTab->localPathEdit->setEnabled(enabled);
    m_langCombo->setEnabled(enabled);
    m_delayTab->keyCapture->setEnabled(enabled);
    m_restoreTab->sceneNameEdit->setEnabled(enabled);
}

QString BackupDialog::formatSize(int64_t bytes)
{
    if (bytes < 1024)           return QString("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)    return QString("%1 KB").arg(bytes / 1024);
    return QString("%1 MB").arg(bytes / (1024 * 1024));
}

void BackupDialog::startWorker(BackupWorker::Mode mode, BackupManifest manifest)
{
    stopWorker();
    setControlsEnabled(false);

    m_thread = new QThread(this);
    m_worker = new BackupWorker(&m_manager);
    m_activeMode = mode;
    m_worker->setMode(mode);
    m_worker->setManifest(std::move(manifest));
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::started, m_worker, &BackupWorker::run);
    connect(m_worker, &BackupWorker::progress, this, &BackupDialog::onProgress);
    connect(m_worker, &BackupWorker::log, this, &BackupDialog::onLog);
    connect(m_worker, &BackupWorker::backupFinished, this, &BackupDialog::onBackupFinished);
    connect(m_worker, &BackupWorker::restoreFinished, this, &BackupDialog::onRestoreFinished);
    connect(m_worker, &BackupWorker::manifestLoaded, this,
            [this](bool ok) {
                if (ok) m_loadedManifest = m_worker->manifest();
                onManifestLoaded(ok);
            });

    connect(m_worker, &BackupWorker::manifestLoaded, m_thread, &QThread::quit);
    connect(m_worker, &BackupWorker::backupFinished, m_thread, &QThread::quit);
    connect(m_worker, &BackupWorker::restoreFinished, m_thread, &QThread::quit);

    m_thread->start();
}

void BackupDialog::stopWorker()
{
    if (m_thread) {
        m_thread->quit();
        m_thread->wait(5000);
        m_thread->deleteLater();
        m_thread = nullptr;
    }
    if (m_worker) {
        delete m_worker;
        m_worker = nullptr;
    }
}
