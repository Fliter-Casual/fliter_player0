#ifndef CTRLBAR_H
#define CTRLBAR_H

#include <QWidget>

namespace Ui {
class CtrlBar;
}

class CtrlBar : public QWidget
{
    Q_OBJECT

public:
    explicit CtrlBar(QWidget *parent = nullptr);
    ~CtrlBar();

//signals:是一个 Qt 扩展关键字，表示：“这里是信号（Signal）声明区”
signals:
    // CtrlBarui对象发送信号，MainWind对象响应信号
    void SigPlayOrPause();   // 先实现这个(声明一下这个函数就可以了，不用实现它)

private slots:
    void on_play_clicked();

    void on_stop_clicked();

private:
    Ui::CtrlBar *ui;
};

#endif // CTRLBAR_H
