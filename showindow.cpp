#include "showindow.h"
#include "ui_showindow.h"

showindow::showindow(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::showindow)
{
    ui->setupUi(this);
}

showindow::~showindow()
{
    delete ui;
}
