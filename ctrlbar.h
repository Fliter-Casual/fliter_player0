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
    explicit CtrlBar(QWidget *parent = 0);
    ~CtrlBar();

//signals:是一个 Qt 扩展关键字，表示：“这里是信号（Signal）声明区”
signals:
    // CtrlBarui对象发送信号，MainWind对象响应信号
    // 只需声明槽函数，Qt会自动生成槽函数的实现代码（只需声明，无需实现）

    // 信号只声明不实现，槽要声明加实现。emit触发，connect连接
    
    void SigPlayOrPause();  
    void SigStop();

//slots槽:  先不实现这些槽函数了，等后面需要的时候再实现
private slots:
    void on_playOrPauseBtn_clicked();

    void on_stop_clicked();

private:
    Ui::CtrlBar *ui;
};

#endif // CTRLBAR_H
