#ifndef PANEPROMPT_SELF_ASSESS_H
#define PANEPROMPT_SELF_ASSESS_H

#include <QWidget>

namespace Ui {
class PanePromptSelfAssess;
}

class PanePromptSelfAssess : public QWidget
{
    Q_OBJECT

public:
    explicit PanePromptSelfAssess(QWidget *parent = nullptr);
    ~PanePromptSelfAssess();

private:
    Ui::PanePromptSelfAssess *ui;
};

#endif // PANEPROMPT_SELF_ASSESS_H
