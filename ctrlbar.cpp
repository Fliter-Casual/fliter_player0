#include "ctrlbar.h"
#include "ui_ctrlbar.h"
#include <QDebug>

CtrlBar::CtrlBar(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::CtrlBar)
{
    ui->setupUi(this);

    //设置图标

    QIcon icon_play(":/ctrl/icon/play.png");
    ui->play->setIcon(icon_play);

    QIcon icon_stop(":/ctrl/icon/stop.png");
    ui->stop->setIcon(icon_stop);
}

CtrlBar::~CtrlBar()
{
    delete ui;
}

void CtrlBar::on_play_clicked()
{
    qDebug() << "display video";
}

void CtrlBar::on_stop_clicked()
{
    qDebug() << "stop video";
}
