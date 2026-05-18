#ifndef PANEPROMPT_H
#define PANEPROMPT_H

#include <QModelIndex>
#include <QWidget>

class QFile;

namespace Ui {
class PanePrompt;
}

class AvailableCliList;
class AvailableCliTable;
class ConfigManager;
class PromptEngineer;
class QFileSystemModel;

class PanePrompt : public QWidget
{
    Q_OBJECT

public:
    explicit PanePrompt(QWidget *parent = nullptr);
    ~PanePrompt();

public slots:
    void configNew();
    void configDuplicate();
    void configRemove();

private slots:
    void _onConfigSelectionChanged(const QModelIndex &current, const QModelIndex &previous);
    void _writeToLogFile(const QString &msg);
    void _addFiles();
    void _removeFiles();
    void _openFilesDir();
    void _runOrCancel();

private:
    Ui::PanePrompt    *ui;
    ConfigManager     *m_configManager  = nullptr;
    AvailableCliTable *m_cliTable       = nullptr;
    AvailableCliList  *m_cliList        = nullptr;
    QFileSystemModel  *m_fsModel        = nullptr;
    PromptEngineer    *m_engineer       = nullptr;
    QFile             *m_logFile        = nullptr;

    void _connectSlots();
    void _openLogFile(const QString &cliName, const QString &promptInput,
                      const QString &neededOutput, int maxAttempts);
    void _closeLogFile();
    void _saveConfigData(const QString &internalId);
    void _loadConfigData(const QString &internalId);
    void _updateFilesDir(const QString &internalId);
};

#endif // PANEPROMPT_H
