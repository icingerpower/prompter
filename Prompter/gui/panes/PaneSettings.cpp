#include "PaneSettings.h"
#include "ui_PaneSettings.h"

#include <QHeaderView>

#include "aicli/AvailableCliTable.h"

PaneSettings::PaneSettings(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PaneSettings)
    , m_cliTable(new AvailableCliTable(this))
{
    ui->setupUi(this);
    ui->tableViewCli->setModel(m_cliTable);
    ui->tableViewCli->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->tableViewCli->horizontalHeader()->setStretchLastSection(true);
    ui->tableViewCli->verticalHeader()->hide();
    ui->tableViewCli->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableViewCli->setEditTriggers(QAbstractItemView::NoEditTriggers);
}

PaneSettings::~PaneSettings()
{
    delete ui;
}
