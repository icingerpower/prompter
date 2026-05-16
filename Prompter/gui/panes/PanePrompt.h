#ifndef PANEPROMPT_H
#define PANEPROMPT_H

#include <QWidget>

namespace Ui {
class PanePrompt;
}

class PanePrompt : public QWidget
{
    Q_OBJECT

public:
    explicit PanePrompt(QWidget *parent = nullptr);
    ~PanePrompt();

private:
    Ui::PanePrompt *ui;
};

#endif // PANEPROMPT_H
