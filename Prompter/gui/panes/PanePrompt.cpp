#include "PanePrompt.h"
#include "ui_PanePrompt.h"

#include <QSettings>

#include "ConfigManager.h"
#include "workingdirectory/WorkingDirectoryManager.h"

PanePrompt::PanePrompt(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PanePrompt)
{
    ui->setupUi(this);

    m_configManager = new ConfigManager(
        WorkingDirectoryManager::instance()->workingDir(),
        QStringLiteral("Prompt"),
        this);

    ui->listViewConfigs->setModel(m_configManager);

    // Connect selection change AFTER setModel() so the selection model exists.
    connect(ui->listViewConfigs->selectionModel(),
            &QItemSelectionModel::currentChanged,
            this,
            &PanePrompt::_onConfigSelectionChanged);

    // Select the current config in the view and load its data.
    const int curRow = m_configManager->rowForId(m_configManager->currentId());
    if (curRow >= 0) {
        const QModelIndex idx = m_configManager->index(curRow, 0);
        ui->listViewConfigs->setCurrentIndex(idx);
        _loadConfigData(m_configManager->currentId());
    }

    _connectSlots();
}

PanePrompt::~PanePrompt()
{
    // Save current data before the pane is destroyed.
    _saveConfigData(m_configManager->currentId());
    delete ui;
}

void PanePrompt::_connectSlots()
{
    connect(ui->buttonConfigNew,
            &QPushButton::clicked,
            this,
            &PanePrompt::configNew);
    connect(ui->buttonConfigDuplicate,
            &QPushButton::clicked,
            this,
            &PanePrompt::configDuplicate);
    connect(ui->buttonConfigRemove,
            &QPushButton::clicked,
            this,
            &PanePrompt::configRemove);
}

// ---------------------------------------------------------------------------
// Config CRUD slots
// ---------------------------------------------------------------------------

void PanePrompt::configNew()
{
    _saveConfigData(m_configManager->currentId());
    const QString newId  = m_configManager->addConfig();
    const int     newRow = m_configManager->rowForId(newId);
    ui->listViewConfigs->setCurrentIndex(m_configManager->index(newRow, 0));
    ui->listViewConfigs->edit(m_configManager->index(newRow, 0)); // start inline rename
}

void PanePrompt::configDuplicate()
{
    _saveConfigData(m_configManager->currentId());
    const QString newId  = m_configManager->duplicateConfig(m_configManager->currentId());
    if (newId.isEmpty()) return;
    const int     newRow = m_configManager->rowForId(newId);
    ui->listViewConfigs->setCurrentIndex(m_configManager->index(newRow, 0));
}

void PanePrompt::configRemove()
{
    // removeConfig handles updating currentId internally before emitting signals.
    m_configManager->removeConfig(m_configManager->currentId());
    // After removal the model's currentConfigChanged re-selects the view via the signal
    // we connected in the constructor; also sync the view selection explicitly.
    const int curRow = m_configManager->rowForId(m_configManager->currentId());
    if (curRow >= 0) {
        ui->listViewConfigs->setCurrentIndex(m_configManager->index(curRow, 0));
    }
}

// ---------------------------------------------------------------------------
// Selection change
// ---------------------------------------------------------------------------

void PanePrompt::_onConfigSelectionChanged(const QModelIndex &current,
                                            const QModelIndex &previous)
{
    if (previous.isValid()) {
        const QString oldId = previous.data(ConfigManager::InternalIdRole).toString();
        _saveConfigData(oldId);
    }
    if (current.isValid()) {
        const QString newId = current.data(ConfigManager::InternalIdRole).toString();
        m_configManager->selectConfig(newId);
        _loadConfigData(newId);
    }
}

// ---------------------------------------------------------------------------
// Data persistence
// ---------------------------------------------------------------------------

void PanePrompt::_saveConfigData(const QString &internalId)
{
    if (internalId.isEmpty() || m_configManager->rowForId(internalId) < 0) {
        return;
    }
    QSettings s(m_configManager->configDir(internalId).filePath(QStringLiteral("data.ini")),
                QSettings::IniFormat);
    s.setValue(QStringLiteral("promptInput"),
               ui->textEditPromptInput->toPlainText());
    s.setValue(QStringLiteral("promptNeededOutput"),
               ui->textEditPromptInput_2->toPlainText());
    s.setValue(QStringLiteral("maxAttempts"),
               ui->spinBoxMaxAttempts->value());
}

void PanePrompt::_loadConfigData(const QString &internalId)
{
    if (internalId.isEmpty()) {
        return;
    }
    QSettings s(m_configManager->configDir(internalId).filePath(QStringLiteral("data.ini")),
                QSettings::IniFormat);
    ui->textEditPromptInput->setPlainText(
        s.value(QStringLiteral("promptInput")).toString());
    ui->textEditPromptInput_2->setPlainText(
        s.value(QStringLiteral("promptNeededOutput")).toString());
    ui->spinBoxMaxAttempts->setValue(
        s.value(QStringLiteral("maxAttempts"), 1).toInt());
}
