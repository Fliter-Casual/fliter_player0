#ifndef DISPLAYWIND_H
#define DISPLAYWIND_H

#include <QWidget>

namespace Ui {
class displaywind;
}

class displaywind : public QWidget
{
    Q_OBJECT

public:
    explicit displaywind(QWidget *parent = nullptr);
    ~displaywind();

private:
    Ui::displaywind *ui;
};

#endif // DISPLAYWIND_H
