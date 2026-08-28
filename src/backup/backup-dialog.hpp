#pragma once
#include "backup-manager.hpp"

#include <QDialog>
#include <QTabWidget>
#include <QTextEdit>
#include <QProgressBar>
#include <QLabel>

class QGroupBox;
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QTreeWidget>
#include <QThread>
#include <QObject>
#include <QVBoxLayout>

// Cada solapa vive en su propio archivo (settings-tab.hpp, delay-tab.hpp,
// backup-tab.hpp, restore-tab.hpp); acá solo se declaran para los punteros
// miembro de BackupDialog.
class SettingsTabPage;
class DelayTabPage;
class BackupTabPage;
class RestoreTabPage;

// ─────────────────────────────────────────────────────────────────────────────
// Objeto worker thread – ejecuta backup / restore fuera del thread de la UI
// ─────────────────────────────────────────────────────────────────────────────

class BackupWorker : public QObject {
    Q_OBJECT
public:
    explicit BackupWorker(BackupManager* mgr) : m_mgr(mgr) {}

    enum class Mode { Backup, Restore, LoadManifest };
    void setMode(Mode m)                   { m_mode = m; }
    void setManifest(BackupManifest mf)    { m_manifest = std::move(mf); }
    const BackupManifest& manifest() const { return m_manifest; }

public slots:
    void run();

signals:
    void progress(const QString& file, int pct, int current, int total);
    void log(const QString& msg);
    void backupFinished(bool success, const QString& msg);
    void restoreFinished(bool success, const QString& msg);
    void manifestLoaded(bool ok);

private:
    BackupManager* m_mgr  = nullptr;
    Mode           m_mode = Mode::Backup;
    BackupManifest m_manifest;
};

// ─────────────────────────────────────────────────────────────────────────────
// Diálogo principal
// ─────────────────────────────────────────────────────────────────────────────

class BackupDialog : public QDialog {
    Q_OBJECT
public:
    explicit BackupDialog(QWidget* parent = nullptr);
    ~BackupDialog() override;

private slots:
    void onBrowseClicked();
    void onBackupClicked();
    void onRefreshManifest();
    void onRestoreClicked();

    void onProgress(const QString& file, int pct, int current, int total);
    void onLog(const QString& msg);
    void onBackupFinished(bool success, const QString& msg);
    void onRestoreFinished(bool success, const QString& msg);
    void onManifestLoaded(bool ok);

    void onLanguageChanged(int index);
    void saveSettings();

private:
    void buildUI();
    void buildTabs();
    void buildSupportBox();
    void retranslateSupportBox();
    void setControlsEnabled(bool enabled);
    QString formatSize(int64_t bytes);

    // ── Widgets ───────────────────────────────────────────────────────────────
    QVBoxLayout*  m_mainLayout  = nullptr;   // dueño de la barra de idioma + apoyo + tabs
    QGroupBox*    m_supportBox  = nullptr;   // panel de apoyo, visible en todas las solapas
    QLabel*       m_supportLabel = nullptr;
    QComboBox*    m_langCombo   = nullptr;   // persiste al reconstruir los tabs
    QTabWidget*   m_tabs         = nullptr;

    SettingsTabPage* m_settingsTab = nullptr;
    BackupTabPage*   m_backupTab   = nullptr;
    RestoreTabPage*  m_restoreTab  = nullptr;
    DelayTabPage*    m_delayTab    = nullptr;

    // ── Lógica ────────────────────────────────────────────────────────────────
    BackupManager  m_manager;
    BackupManifest m_loadedManifest;

    BackupWorker*  m_worker = nullptr;
    QThread*       m_thread = nullptr;
    BackupWorker::Mode m_activeMode = BackupWorker::Mode::Backup;  // enruta log/progress

    void startWorker(BackupWorker::Mode mode,
                     BackupManifest manifest = {});
    void stopWorker();
};
