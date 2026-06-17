#include "urldialog.h"
#include "ui_urldialog.h"

urldialog::urldialog(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::urldialog)
{
    ui->setupUi(this);
}

urldialog::~urldialog()
{
    delete ui;
}
