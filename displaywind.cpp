#include "displaywind.h"
#include "ui_displaywind.h"

displaywind::displaywind(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::displaywind)
{
    ui->setupUi(this);
}

displaywind::~displaywind()
{
    delete ui;
}
