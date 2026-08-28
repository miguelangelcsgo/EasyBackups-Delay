#include "settings-tab.hpp"
#include "backup-manager.hpp"
#include "i18n.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>

SettingsTabPage::SettingsTabPage(BackupManager* manager, QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(12);

    auto* folderGroup = new QGroupBox(T(S::BackupFolder), this);
    auto* folderLayout = new QGridLayout(folderGroup);

    folderLayout->addWidget(new QLabel(T(S::BackupFolder), this), 0, 0);
    localPathEdit = new QLineEdit(this);
    localPathEdit->setPlaceholderText(T(S::BackupFolderPh));
    localPathEdit->setText(QString::fromStdString(manager->m_localPath));
    folderLayout->addWidget(localPathEdit, 0, 1);

    browseBtn = new QPushButton(T(S::Browse), this);
    folderLayout->addWidget(browseBtn, 0, 2);

    layout->addWidget(folderGroup);

    auto* hintLabel = new QLabel(T(S::HintLocal), this);
    hintLabel->setWordWrap(true);
    hintLabel->setTextFormat(Qt::RichText);
    layout->addWidget(hintLabel);

    // Version instalada. El plugin no busca actualizaciones solo: no habla con
    // ningun servidor. Para actualizar, bajar la version nueva a mano.
    auto* updGroup = new QGroupBox(T(S::UpdatesGroup), this);
    auto* updLayout = new QVBoxLayout(updGroup);
    updLayout->addWidget(new QLabel(
        T(S::UpdateCurrentFmt).arg(QStringLiteral(EASYOBS_VERSION)), this));
    layout->addWidget(updGroup);

    auto* btnRow = new QHBoxLayout;
    saveBtn = new QPushButton(T(S::SaveSettings), this);
    btnRow->addStretch();
    btnRow->addWidget(saveBtn);
    layout->addLayout(btnRow);
    layout->addStretch();
}
