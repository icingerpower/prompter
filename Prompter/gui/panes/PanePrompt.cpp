#include "PanePrompt.h"
#include "ui_PanePrompt.h"

#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QMessageBox>
#include <QSettings>
#include <QUrl>

#include "ConfigManager.h"
#include "PromptEngineer.h"
#include "aicli/AvailableCliList.h"
#include "aicli/AvailableCliTable.h"
#include "workingdirectory/WorkingDirectoryManager.h"

PanePrompt::PanePrompt(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PanePrompt)
{
    ui->setupUi(this);
    ui->progressBar->hide();

    m_configManager = new ConfigManager(
        WorkingDirectoryManager::instance()->workingDir(),
        QStringLiteral("Prompt"),
        this);

    m_cliTable = new AvailableCliTable(this);
    m_cliList  = new AvailableCliList(m_cliTable, this);
    ui->comboBoxCli->setModel(m_cliList);

    m_fsModel = new QFileSystemModel(this);
    m_fsModel->setFilter(QDir::Files | QDir::NoDotAndDotDot);
    ui->treeViewFiles->setModel(m_fsModel);
    ui->treeViewFiles->setColumnHidden(1, true); // Size
    ui->treeViewFiles->setColumnHidden(2, true); // Type
    ui->treeViewFiles->setColumnHidden(3, true); // Date Modified
    ui->treeViewFiles->header()->hide();

    m_engineer = new PromptEngineer(this);
    connect(m_engineer, &PromptEngineer::log,
            ui->textEditLogs, &QTextEdit::append);
    connect(m_engineer, &PromptEngineer::progressChanged,
            this, [this](int attempt, int maxAttempts) {
        ui->progressBar->setMaximum(maxAttempts);
        ui->progressBar->setValue(attempt);
    });
    connect(m_engineer, &PromptEngineer::candidatePromptChanged,
            ui->textEditPromptCreated, &QTextEdit::setPlainText);
    connect(m_engineer, &PromptEngineer::candidateReplyChanged,
            ui->textEditReplyOfPromptCreated, &QTextEdit::setPlainText);
    connect(m_engineer, &PromptEngineer::finished,
            this, [this](bool success, const QString &prompt, const QString &) {
        ui->buttonRun->setText(tr("Run"));
        ui->progressBar->hide();
        if (success) {
            ui->textEditPromptCreated->setPlainText(prompt);
        }
    });

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
    connect(ui->buttonAddFile,
            &QPushButton::clicked,
            this,
            &PanePrompt::_addFiles);
    connect(ui->buttonRemoveFile,
            &QPushButton::clicked,
            this,
            &PanePrompt::_removeFiles);
    connect(ui->buttonOpenInputFileDir,
            &QPushButton::clicked,
            this,
            &PanePrompt::_openFilesDir);
    connect(ui->buttonRun,
            &QPushButton::clicked,
            this,
            &PanePrompt::_runOrCancel);
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
    _updateFilesDir(internalId);
}

// ---------------------------------------------------------------------------
// File management
// ---------------------------------------------------------------------------

void PanePrompt::_updateFilesDir(const QString &internalId)
{
    if (internalId.isEmpty()) {
        ui->treeViewFiles->setRootIndex({});
        return;
    }
    QDir dir = m_configManager->configDir(internalId);
    dir.mkpath(QStringLiteral("files"));
    const QString path = dir.filePath(QStringLiteral("files"));
    const QModelIndex root = m_fsModel->setRootPath(path);
    ui->treeViewFiles->setRootIndex(root);
}

void PanePrompt::_runOrCancel()
{
    if (m_engineer->isRunning()) {
        m_engineer->cancel();
        ui->buttonRun->setText(tr("Run"));
        ui->progressBar->hide();
        ui->textEditLogs->append(tr("— Cancelled by user —"));
        return;
    }

    AbstractCli *cli = m_cliList->cliAt(ui->comboBoxCli->currentIndex());
    if (!cli) {
        QMessageBox::warning(this, tr("No CLI available"),
            tr("No available CLI is selected. Wait for availability checks to complete "
               "or verify that a supported CLI is installed in PATH."));
        return;
    }

    const QString promptInput = ui->textEditPromptInput->toPlainText().trimmed();
    if (promptInput.isEmpty()) {
        QMessageBox::warning(this, tr("Missing input"),
            tr("Please enter prompt rules in the \"Prompt input\" field."));
        return;
    }

    _saveConfigData(m_configManager->currentId());

    ui->toolBox->setCurrentIndex(1); // switch to "Page Output"
    ui->textEditLogs->clear();
    ui->textEditPromptCreated->clear();
    ui->progressBar->setMaximum(ui->spinBoxMaxAttempts->value());
    ui->progressBar->setValue(0);
    ui->progressBar->show();
    ui->buttonRun->setText(tr("Cancel"));

    m_engineer->start(
        cli,
        promptInput,
        ui->textEditPromptInput_2->toPlainText().trimmed(),
        ui->spinBoxMaxAttempts->value(),
        m_fsModel->rootPath());
}

void PanePrompt::_openFilesDir()
{
    const QString rootPath = m_fsModel->rootPath();
    if (!rootPath.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(rootPath));
    }
}

void PanePrompt::_addFiles()
{
    const QString rootPath = m_fsModel->rootPath();
    if (rootPath.isEmpty()) {
        return;
    }
    const QStringList srcPaths = QFileDialog::getOpenFileNames(
        this, tr("Add Files"), QString{});
    if (srcPaths.isEmpty()) {
        return;
    }
    for (const QString &src : srcPaths) {
        const QString fileName = QFileInfo(src).fileName();
        const QString dst = QDir(rootPath).filePath(fileName);
        if (QFile::exists(dst)) {
            const int ret = QMessageBox::question(
                this,
                tr("Replace File"),
                tr("File \"%1\" already exists. Replace it?").arg(fileName),
                QMessageBox::Yes | QMessageBox::No);
            if (ret != QMessageBox::Yes) {
                continue;
            }
            QFile::remove(dst);
        }
        QFile::copy(src, dst);
    }
}

void PanePrompt::_removeFiles()
{
    const QModelIndexList selected =
        ui->treeViewFiles->selectionModel()->selectedRows();
    if (selected.isEmpty()) {
        return;
    }
    const int ret = QMessageBox::question(
        this,
        tr("Remove Files"),
        tr("Remove %n file(s)?", "", selected.size()),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) {
        return;
    }
    for (const QModelIndex &idx : selected) {
        m_fsModel->remove(idx);
    }
}
