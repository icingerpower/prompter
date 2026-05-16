#ifndef PANEPROMPT_VIDEO_H
#define PANEPROMPT_VIDEO_H

#include <QWidget>

namespace Ui {
class PanePromptVideo;
}

class PanePromptVideo : public QWidget
{
    Q_OBJECT

public:
    explicit PanePromptVideo(QWidget *parent = nullptr);
    ~PanePromptVideo();

private:
    Ui::PanePromptVideo *ui;
};

#endif // PANEPROMPT_VIDEO_H
