#ifndef PANEPROMPT_SELF_ASSESS_H
#define PANEPROMPT_SELF_ASSESS_H

#include <QModelIndex>
#include <QWidget>

class QFile;

namespace Ui {
class PanePromptSelfAssess;
}

class AvailableCliList;
class AvailableCliTable;
class ConfigManager;
class QFileSystemModel;
class SelfAssessEngineer;

class PanePromptSelfAssess : public QWidget
{
    Q_OBJECT

public:
    explicit PanePromptSelfAssess(QWidget *parent = nullptr);
    ~PanePromptSelfAssess();

public slots:
    void configNew();
    void configDuplicate();
    void configRemove();

private slots:
    void _onConfigSelectionChanged(const QModelIndex &current, const QModelIndex &previous);
    void _onResultReady(int index, const QString &prompt, const QString &output,
                        double score, const QString &explanation);
    void _onTableSelectionChanged();
    void _writeToLogFile(const QString &msg);
    void _addFiles();
    void _removeFiles();
    void _openFilesDir();
    void _runOrCancel();

private:
    Ui::PanePromptSelfAssess *ui;
    ConfigManager            *m_configManager = nullptr;
    AvailableCliTable        *m_cliTable       = nullptr;
    AvailableCliList         *m_cliList        = nullptr;
    QFileSystemModel         *m_fsModel        = nullptr;
    SelfAssessEngineer       *m_engineer       = nullptr;
    QFile                    *m_logFile        = nullptr;

    void _connectSlots();
    void _openLogFile(const QString &cliName, const QString &promptInput,
                      const QString &assessmentCriteria, int maxAttempts);
    void _closeLogFile();
    void _saveConfigData(const QString &internalId);
    void _loadConfigData(const QString &internalId);
    void _updateFilesDir(const QString &internalId);
};

#endif // PANEPROMPT_SELF_ASSESS_H
