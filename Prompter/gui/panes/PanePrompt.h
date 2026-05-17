#ifndef PANEPROMPT_H
#define PANEPROMPT_H

#include <QModelIndex>
#include <QWidget>

namespace Ui {
class PanePrompt;
}

class ConfigManager;

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

private:
    Ui::PanePrompt *ui;
    ConfigManager  *m_configManager = nullptr;

    void _connectSlots();
    void _saveConfigData(const QString &internalId);
    void _loadConfigData(const QString &internalId);
};

#endif // PANEPROMPT_H
