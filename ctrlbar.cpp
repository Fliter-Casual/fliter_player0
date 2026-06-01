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
    ui->playOrPauseBtn->setIcon(icon_play);

    QIcon icon_stop(":/ctrl/icon/stop.png");
    ui->stop->setIcon(icon_stop);
}

CtrlBar::~CtrlBar()
{
    delete ui;
}

void CtrlBar::on_playOrPauseBtn_clicked()
{
    // 1. 更新按钮图标（比如从播放变成暂停图标）
    // ... 
    
    // 2. 向外发送信号，通知 MainWindow 去处理真正的播放逻辑
    qDebug() << "on_playOrPauseBtn_clicked";
    emit SigPlayOrPause();      // 发送信号，这里只发送信号具体是播放还是暂停由播放逻辑判断。
}

void CtrlBar::on_stop_clicked()
{
    // 1. 重置 UI 状态
    // ...

    // 2. 向外发送信号
    qDebug() << "on_stop_clicked";
    emit SigStop();
}
