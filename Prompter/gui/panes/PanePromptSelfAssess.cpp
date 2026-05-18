#include "PanePromptSelfAssess.h"
#include "ui_PanePromptSelfAssess.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QMessageBox>
#include <QSettings>
#include <QTableWidgetItem>
#include <QUrl>

#include "ConfigManager.h"
#include "SelfAssessEngineer.h"
#include "aicli/AvailableCliList.h"
#include "aicli/AvailableCliTable.h"
#include "workingdirectory/WorkingDirectoryManager.h"

// QTableWidgetItem subclass that sorts numerically via Qt::UserRole (double).
class ScoreItem : public QTableWidgetItem
{
public:
    bool operator<(const QTableWidgetItem &other) const override
    {
        return data(Qt::UserRole).toDouble() < other.data(Qt::UserRole).toDouble();
    }
};

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

PanePromptSelfAssess::PanePromptSelfAssess(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PanePromptSelfAssess)
{
    ui->setupUi(this);
    ui->progressBar->hide();

    m_configManager = new ConfigManager(
        WorkingDirectoryManager::instance()->workingDir(),
        QStringLiteral("SelfAssess"),
        this);

    m_cliTable = new AvailableCliTable(this);
    m_cliList  = new AvailableCliList(m_cliTable, this);
    ui->comboBoxCli->setModel(m_cliList);

    m_fsModel = new QFileSystemModel(this);
    m_fsModel->setFilter(QDir::Files | QDir::NoDotAndDotDot);
    ui->treeViewFiles->setModel(m_fsModel);
    ui->treeViewFiles->setColumnHidden(1, true);
    ui->treeViewFiles->setColumnHidden(2, true);
    ui->treeViewFiles->setColumnHidden(3, true);
    ui->treeViewFiles->header()->hide();

    // Results table setup
    ui->tableWidgetResults->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tableWidgetResults->horizontalHeader()->setStretchLastSection(true);
    ui->tableWidgetResults->verticalHeader()->hide();
    ui->tableWidgetResults->setColumnWidth(0, 80);

    m_engineer = new SelfAssessEngineer(this);
    connect(m_engineer, &SelfAssessEngineer::log,
            ui->textEditLogs, &QTextEdit::append);
    connect(m_engineer, &SelfAssessEngineer::log,
            this, &PanePromptSelfAssess::_writeToLogFile);
    connect(m_engineer, &SelfAssessEngineer::progressChanged,
            this, [this](int attempt, int maxAttempts) {
        ui->progressBar->setMaximum(maxAttempts);
        ui->progressBar->setValue(attempt);
    });
    connect(m_engineer, &SelfAssessEngineer::resultReady,
            this, &PanePromptSelfAssess::_onResultReady);
    connect(m_engineer, &SelfAssessEngineer::finished,
            this, [this]() {
        ui->buttonRun->setText(tr("Run"));
        ui->progressBar->hide();
        const int n = ui->tableWidgetResults->rowCount();
        ui->labelRunStatus->setText(
            tr("Run complete — %n candidate(s) evaluated.", "", n));
        ui->labelRunStatus->setStyleSheet(
            QStringLiteral("color: #1565C0; font-weight: bold;"));
        ui->labelRunStatus->show();
        _closeLogFile();
    });

    ui->listViewConfigs->setModel(m_configManager);

    connect(ui->listViewConfigs->selectionModel(),
            &QItemSelectionModel::currentChanged,
            this,
            &PanePromptSelfAssess::_onConfigSelectionChanged);

    const int curRow = m_configManager->rowForId(m_configManager->currentId());
    if (curRow >= 0) {
        ui->listViewConfigs->setCurrentIndex(m_configManager->index(curRow, 0));
        _loadConfigData(m_configManager->currentId());
    }

    _connectSlots();
}

PanePromptSelfAssess::~PanePromptSelfAssess()
{
    _saveConfigData(m_configManager->currentId());
    _closeLogFile();
    delete ui;
}

// ---------------------------------------------------------------------------
// Slots wiring
// ---------------------------------------------------------------------------

void PanePromptSelfAssess::_connectSlots()
{
    connect(ui->buttonConfigNew,      &QPushButton::clicked, this, &PanePromptSelfAssess::configNew);
    connect(ui->buttonConfigDuplicate,&QPushButton::clicked, this, &PanePromptSelfAssess::configDuplicate);
    connect(ui->buttonConfigRemove,   &QPushButton::clicked, this, &PanePromptSelfAssess::configRemove);
    connect(ui->buttonAddFile,        &QPushButton::clicked, this, &PanePromptSelfAssess::_addFiles);
    connect(ui->buttonRemoveFile,     &QPushButton::clicked, this, &PanePromptSelfAssess::_removeFiles);
    connect(ui->buttonOpenInputFileDir,&QPushButton::clicked,this, &PanePromptSelfAssess::_openFilesDir);
    connect(ui->buttonRun,            &QPushButton::clicked, this, &PanePromptSelfAssess::_runOrCancel);
    connect(ui->tableWidgetResults,   &QTableWidget::currentCellChanged,
            this, [this](int, int, int, int) { _onTableSelectionChanged(); });
}

// ---------------------------------------------------------------------------
// Config CRUD
// ---------------------------------------------------------------------------

void PanePromptSelfAssess::configNew()
{
    _saveConfigData(m_configManager->currentId());
    const QString newId  = m_configManager->addConfig();
    const int     newRow = m_configManager->rowForId(newId);
    ui->listViewConfigs->setCurrentIndex(m_configManager->index(newRow, 0));
    ui->listViewConfigs->edit(m_configManager->index(newRow, 0));
}

void PanePromptSelfAssess::configDuplicate()
{
    _saveConfigData(m_configManager->currentId());
    const QString newId  = m_configManager->duplicateConfig(m_configManager->currentId());
    if (newId.isEmpty()) return;
    const int     newRow = m_configManager->rowForId(newId);
    ui->listViewConfigs->setCurrentIndex(m_configManager->index(newRow, 0));
}

void PanePromptSelfAssess::configRemove()
{
    m_configManager->removeConfig(m_configManager->currentId());
    const int curRow = m_configManager->rowForId(m_configManager->currentId());
    if (curRow >= 0) {
        ui->listViewConfigs->setCurrentIndex(m_configManager->index(curRow, 0));
    }
}

// ---------------------------------------------------------------------------
// Selection change
// ---------------------------------------------------------------------------

void PanePromptSelfAssess::_onConfigSelectionChanged(const QModelIndex &current,
                                                      const QModelIndex &previous)
{
    if (previous.isValid()) {
        _saveConfigData(previous.data(ConfigManager::InternalIdRole).toString());
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

void PanePromptSelfAssess::_saveConfigData(const QString &internalId)
{
    if (internalId.isEmpty() || m_configManager->rowForId(internalId) < 0) {
        return;
    }
    QSettings s(m_configManager->configDir(internalId).filePath(QStringLiteral("data.ini")),
                QSettings::IniFormat);
    s.setValue(QStringLiteral("promptInput"),         ui->textEditPromptInput->toPlainText());
    s.setValue(QStringLiteral("assessmentCriteria"),  ui->textEditAssessmentCriteria->toPlainText());
    s.setValue(QStringLiteral("maxAttempts"),          ui->spinBoxMaxAttempts->value());
}

void PanePromptSelfAssess::_loadConfigData(const QString &internalId)
{
    if (internalId.isEmpty()) {
        return;
    }
    QSettings s(m_configManager->configDir(internalId).filePath(QStringLiteral("data.ini")),
                QSettings::IniFormat);
    ui->textEditPromptInput->setPlainText(
        s.value(QStringLiteral("promptInput")).toString());
    ui->textEditAssessmentCriteria->setPlainText(
        s.value(QStringLiteral("assessmentCriteria")).toString());
    ui->spinBoxMaxAttempts->setValue(
        s.value(QStringLiteral("maxAttempts"), 3).toInt());
    _updateFilesDir(internalId);
}

// ---------------------------------------------------------------------------
// File management
// ---------------------------------------------------------------------------

void PanePromptSelfAssess::_updateFilesDir(const QString &internalId)
{
    if (internalId.isEmpty()) {
        ui->treeViewFiles->setRootIndex({});
        return;
    }
    QDir dir = m_configManager->configDir(internalId);
    dir.mkpath(QStringLiteral("files"));
    const QString path = dir.filePath(QStringLiteral("files"));
    ui->treeViewFiles->setRootIndex(m_fsModel->setRootPath(path));
}

void PanePromptSelfAssess::_openFilesDir()
{
    const QString p = m_fsModel->rootPath();
    if (!p.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(p));
    }
}

void PanePromptSelfAssess::_addFiles()
{
    const QString rootPath = m_fsModel->rootPath();
    if (rootPath.isEmpty()) {
        return;
    }
    const QStringList srcPaths = QFileDialog::getOpenFileNames(this, tr("Add Files"));
    for (const QString &src : srcPaths) {
        const QString fileName = QFileInfo(src).fileName();
        const QString dst = QDir(rootPath).filePath(fileName);
        if (QFile::exists(dst)) {
            const int ret = QMessageBox::question(
                this, tr("Replace File"),
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

void PanePromptSelfAssess::_removeFiles()
{
    const QModelIndexList selected = ui->treeViewFiles->selectionModel()->selectedRows();
    if (selected.isEmpty()) {
        return;
    }
    if (QMessageBox::question(this, tr("Remove Files"),
                              tr("Remove %n file(s)?", "", selected.size()),
                              QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    for (const QModelIndex &idx : selected) {
        m_fsModel->remove(idx);
    }
}

// ---------------------------------------------------------------------------
// Run / cancel
// ---------------------------------------------------------------------------

void PanePromptSelfAssess::_runOrCancel()
{
    if (m_engineer->isRunning()) {
        m_engineer->cancel();
        ui->buttonRun->setText(tr("Run"));
        ui->progressBar->hide();
        ui->textEditLogs->append(tr("— Cancelled by user —"));
        ui->labelRunStatus->setText(tr("Cancelled."));
        ui->labelRunStatus->setStyleSheet(QStringLiteral("color: gray; font-weight: bold;"));
        ui->labelRunStatus->show();
        _closeLogFile();
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

    ui->labelRunStatus->hide();
    ui->tableWidgetResults->setRowCount(0);
    ui->textEditSelectedPrompt->clear();
    ui->textEditSelectedReply->clear();
    ui->textEditLogs->clear();
    ui->progressBar->setMaximum(ui->spinBoxMaxAttempts->value());
    ui->progressBar->setValue(0);
    ui->progressBar->show();
    ui->buttonRun->setText(tr("Cancel"));
    ui->toolBox->setCurrentIndex(1); // switch to "Output" page

    const QString assessmentCriteria = ui->textEditAssessmentCriteria->toPlainText().trimmed();
    _openLogFile(cli->getName(), promptInput, assessmentCriteria, ui->spinBoxMaxAttempts->value());

    m_engineer->start(
        cli,
        promptInput,
        assessmentCriteria,
        ui->spinBoxMaxAttempts->value(),
        m_fsModel->rootPath());
}

// ---------------------------------------------------------------------------
// Results table
// ---------------------------------------------------------------------------

void PanePromptSelfAssess::_onResultReady(int index,
                                           const QString &prompt,
                                           const QString &output,
                                           double score,
                                           const QString &explanation)
{
    const bool wasEmpty = (ui->tableWidgetResults->rowCount() == 0);

    const int row = ui->tableWidgetResults->rowCount();
    ui->tableWidgetResults->insertRow(row);

    // Column 0: prompt number; also carries prompt + output for detail view.
    auto *numItem = new QTableWidgetItem(QString::number(index + 1));
    numItem->setData(Qt::UserRole + 1, prompt);
    numItem->setData(Qt::UserRole + 2, output);
    numItem->setToolTip(explanation);
    numItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    ui->tableWidgetResults->setItem(row, 0, numItem);

    // Column 1: score with numeric sort support.
    auto *scoreItem = new ScoreItem;
    scoreItem->setText(QString::number(score, 'f', 1));
    scoreItem->setData(Qt::UserRole, score);
    scoreItem->setToolTip(explanation);
    scoreItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    ui->tableWidgetResults->setItem(row, 1, scoreItem);

    // Sort by score descending (ScoreItem::operator< handles numeric comparison).
    ui->tableWidgetResults->sortItems(1, Qt::DescendingOrder);

    // Auto-select the first (best) row only when the table was empty before.
    if (wasEmpty) {
        ui->tableWidgetResults->selectRow(0);
    }
}

// ---------------------------------------------------------------------------
// Log file
// ---------------------------------------------------------------------------

void PanePromptSelfAssess::_openLogFile(const QString &cliName,
                                         const QString &promptInput,
                                         const QString &assessmentCriteria,
                                         int maxAttempts)
{
    _closeLogFile();

    const QString logsDir =
        m_configManager->configDir(m_configManager->currentId()).filePath(QStringLiteral("logs"));
    QDir().mkpath(logsDir);

    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString path = QDir(logsDir).filePath(QStringLiteral("run_%1.txt").arg(timestamp));

    m_logFile = new QFile(path);
    if (!m_logFile->open(QIODevice::WriteOnly | QIODevice::Text)) {
        delete m_logFile;
        m_logFile = nullptr;
        return;
    }

    const QString header = QStringLiteral(
        "=== Run started: %1 ===\n"
        "Pane: Self-assess prompt\n"
        "CLI: %2\n"
        "Max attempts: %3\n"
        "--- Prompt input ---\n%4\n"
        "--- Assessment criteria ---\n%5\n"
        "===========================================\n\n")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODate))
        .arg(cliName)
        .arg(maxAttempts)
        .arg(promptInput)
        .arg(assessmentCriteria);
    m_logFile->write(header.toUtf8());
    m_logFile->flush();
}

void PanePromptSelfAssess::_writeToLogFile(const QString &msg)
{
    if (m_logFile && m_logFile->isOpen()) {
        m_logFile->write((msg + u'\n').toUtf8());
        m_logFile->flush();
    }
}

void PanePromptSelfAssess::_closeLogFile()
{
    if (m_logFile) {
        if (m_logFile->isOpen()) {
            const QString footer = QStringLiteral("\n=== Run ended: %1 ===\n")
                .arg(QDateTime::currentDateTime().toString(Qt::ISODate));
            m_logFile->write(footer.toUtf8());
            m_logFile->close();
        }
        delete m_logFile;
        m_logFile = nullptr;
    }
}

void PanePromptSelfAssess::_onTableSelectionChanged()
{
    const int row = ui->tableWidgetResults->currentRow();
    if (row < 0) {
        ui->textEditSelectedPrompt->clear();
        ui->textEditSelectedReply->clear();
        return;
    }
    const QTableWidgetItem *item = ui->tableWidgetResults->item(row, 0);
    if (!item) {
        return;
    }
    ui->textEditSelectedPrompt->setPlainText(item->data(Qt::UserRole + 1).toString());
    ui->textEditSelectedReply->setPlainText(item->data(Qt::UserRole + 2).toString());
}
