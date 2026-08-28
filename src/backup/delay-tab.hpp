#pragma once
#include <QPushButton>

#include <vector>
#include <set>
#include <functional>

class QMouseEvent;
class QKeyEvent;
class QFocusEvent;
class QTableWidget;
class QComboBox;
class QCheckBox;

// ─────────────────────────────────────────────────────────────────────────────
// KeyCaptureButton
//
// Botón que, al clickearlo, se pone a "escuchar" el teclado y GUARDA las teclas
// que apretás. Soporta combos: si mantenés varias a la vez las junta y las muestra
// como "Ctrl + Shift + V". Guarda códigos VK de Windows (los mismos que sondea el
// dock con GetAsyncKeyState). Esc = ninguna. Sin señales/slots propios → no necesita
// Q_OBJECT/MOC; avisa los cambios por el callback onChanged.
// ─────────────────────────────────────────────────────────────────────────────
class KeyCaptureButton : public QPushButton {
public:
    explicit KeyCaptureButton(QWidget* parent = nullptr);

    void setKeys(const std::vector<int>& vks);       // set inicial (no dispara onChanged)
    const std::vector<int>& keys() const { return m_vks; }

    std::function<void(const std::vector<int>&)> onChanged;   // combo confirmado

protected:
    void mousePressEvent(QMouseEvent*)   override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*)       override;
    void keyReleaseEvent(QKeyEvent*)     override;
    void focusOutEvent(QFocusEvent*)     override;

private:
    void startListening();
    void finalize(bool cancelled);
    void refreshText();

    std::vector<int> m_vks;       // combo confirmado (persistido)
    std::vector<int> m_capture;   // combo en construcción (unión de lo apretado)
    std::set<int>    m_down;      // teclas físicamente apretadas AHORA
    bool             m_listening = false;
};

// Solapa "Delay": la tecla (o combo) global que activa/desactiva TODOS los delays,
// y la tabla que ata la visibilidad de cada fuente a ese estado.
class DelayTabPage : public QWidget {
public:
    explicit DelayTabPage(QWidget* parent = nullptr);

    KeyCaptureButton* keyCapture    = nullptr;
    QTableWidget*     visTable      = nullptr;   // fuente → cuándo mostrarla
    QComboBox*        micCombo      = nullptr;   // qué fuente silencia el botón del dock
    KeyCaptureButton* micKeyCapture = nullptr;
    QCheckBox*        alertCheck    = nullptr;   // aviso sonoro del audio demorado
    QCheckBox*        monitorCheck  = nullptr;   // escuchar la voz demorada
    QComboBox*        alertLead     = nullptr;   // segundos de anticipación del aviso
    QComboBox*        alertDevice   = nullptr;   // salida por la que suena ese aviso
};
