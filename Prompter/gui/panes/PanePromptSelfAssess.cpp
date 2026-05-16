#include "PanePromptSelfAssess.h"
#include "ui_PanePromptSelfAssess.h"

PanePromptSelfAssess::PanePromptSelfAssess(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PanePromptSelfAssess)
{
    ui->setupUi(this);
}

PanePromptSelfAssess::~PanePromptSelfAssess()
{
    delete ui;
}
