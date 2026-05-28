#include "playlist.h"
#include "ui_playlist.h"

playlist::playlist(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::playlist)
{
    ui->setupUi(this);
}

playlist::~playlist()
{
    delete ui;
}
