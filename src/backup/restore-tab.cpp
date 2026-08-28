#include "restore-tab.hpp"
#include "i18n.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QTextEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QHeaderView>
#include <QFont>

RestoreTabPage::RestoreTabPage(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(8);

    auto* hdr = new QHBoxLayout;
    hdr->addWidget(new QLabel(T(S::AvailableBackup), this));
    hdr->addStretch();
    refreshBtn = new QPushButton(T(S::Refresh), this);
    hdr->addWidget(refreshBtn);
    layout->addLayout(hdr);

    manifestTree = new QTreeWidget(this);
    manifestTree->setHeaderLabels({T(S::ColItem), T(S::ColType), T(S::ColSize)});
    manifestTree->header()->setStretchLastSection(false);
    manifestTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    manifestTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    manifestTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    layout->addWidget(manifestTree);

    auto* nameRow = new QHBoxLayout;
    nameRow->addWidget(new QLabel(T(S::SceneCollectionName), this));
    sceneNameEdit = new QLineEdit(this);
    sceneNameEdit->setPlaceholderText(T(S::SceneNamePh));
    nameRow->addWidget(sceneNameEdit);
    layout->addLayout(nameRow);

    progress = new QProgressBar(this);
    progress->setRange(0, 100);
    progress->setValue(0);
    layout->addWidget(progress);

    log = new QTextEdit(this);
    log->setReadOnly(true);
    log->setFont(QFont("Consolas", 9));
    log->setMaximumHeight(100);
    layout->addWidget(log);

    restoreBtn = new QPushButton(T(S::RestoreSelected), this);
    restoreBtn->setMinimumHeight(36);
    restoreBtn->setEnabled(false);
    layout->addWidget(restoreBtn);
}
