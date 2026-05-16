#include "PanePromptVideo.h"
#include "ui_PanePromptVideo.h"

PanePromptVideo::PanePromptVideo(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PanePromptVideo)
{
    ui->setupUi(this);
}

PanePromptVideo::~PanePromptVideo()
{
    delete ui;
}
