#pragma once
#include <QWidget>

class QLabel;
class QProgressBar;
class QTextEdit;
class QPushButton;

// Solapa "Copia": estado + barra de progreso + log + botón de iniciar, más los
// paneles de donación / redes. Los widgets son públicos para que BackupDialog
// los actualice (progreso/log) y conecte el botón.
class BackupTabPage : public QWidget {
public:
    explicit BackupTabPage(QWidget* parent = nullptr);

    QLabel*       statusLabel = nullptr;
    QProgressBar* progress    = nullptr;
    QTextEdit*    log         = nullptr;
    QPushButton*  startBtn    = nullptr;
};
