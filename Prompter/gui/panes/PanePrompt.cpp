#include "PanePrompt.h"
#include "ui_PanePrompt.h"

PanePrompt::PanePrompt(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PanePrompt)
{
    ui->setupUi(this);
}

PanePrompt::~PanePrompt()
{
    delete ui;
}
