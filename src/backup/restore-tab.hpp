#pragma once
#include <QWidget>

class QTreeWidget;
class QLineEdit;
class QProgressBar;
class QTextEdit;
class QPushButton;

// Solapa "Restaurar": árbol con el contenido de la copia + nombre de la colección
// + progreso/log + botón. Widgets públicos para que BackupDialog los maneje.
class RestoreTabPage : public QWidget {
public:
    explicit RestoreTabPage(QWidget* parent = nullptr);

    QTreeWidget*  manifestTree  = nullptr;
    QLineEdit*    sceneNameEdit = nullptr;
    QProgressBar* progress      = nullptr;
    QTextEdit*    log           = nullptr;
    QPushButton*  refreshBtn    = nullptr;
    QPushButton*  restoreBtn    = nullptr;
};
