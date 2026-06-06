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
    // 必须显示调用构造函数
    explicit CtrlBar(QWidget *parent = 0);
    ~CtrlBar();

//signals:是一个 Qt 扩展关键字，表示：“这里是信号（Signal）声明区”
signals:
    // CtrlBarui对象发送信号，MainWind对象响应信号
    // 只需声明槽函数，Qt会自动生成槽函数的实现代码（只需声明，无需实现）

    // 信号只声明不实现，槽要声明加实现。emit触发，connect连接

    void SigPlayOrPause();  
    void SigStop();

private slots:
    // 虽然没有人调用这两个槽函数，他是点击按钮就会执行这个槽，函数的名字和按钮的名字是要对应的
    void on_playOrPauseBtn_clicked(); // on_按钮名称_clicked()

    void on_stop_clicked();

private:
    Ui::CtrlBar *ui;
};

#endif // CTRLBAR_H
