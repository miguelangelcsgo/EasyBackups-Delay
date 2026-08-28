#pragma once
#include <QWidget>

class QLineEdit;
class QPushButton;
class QCheckBox;
class BackupManager;

// Solapa "Ajustes": elegí la carpeta de copia y guardá los ajustes. Los botones
// son públicos para que BackupDialog conecte sus señales.
class SettingsTabPage : public QWidget {
public:
    explicit SettingsTabPage(BackupManager* manager, QWidget* parent = nullptr);

    QLineEdit*   localPathEdit = nullptr;
    QPushButton* browseBtn     = nullptr;
    QPushButton* saveBtn       = nullptr;
};
