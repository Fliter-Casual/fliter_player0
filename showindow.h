#ifndef SHOWINDOW_H
#define SHOWINDOW_H

#include <QWidget>

namespace Ui {
class showindow;
}

class showindow : public QWidget
{
    Q_OBJECT

public:
    explicit showindow(QWidget *parent = nullptr);
    ~showindow();

private:
    Ui::showindow *ui;
};

#endif // SHOWINDOW_H
