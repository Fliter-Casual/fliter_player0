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

private slots:
    void on_play_clicked();

    void on_stop_clicked();

private:
    Ui::CtrlBar *ui;
};

#endif // CTRLBAR_H
