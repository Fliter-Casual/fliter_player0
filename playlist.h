#ifndef PLAYLIST_H
#define PLAYLIST_H

#include <QWidget>

namespace Ui {
class playlist;
}

class playlist : public QWidget
{
    Q_OBJECT

public:
    explicit playlist(QWidget *parent = nullptr);
    ~playlist();

private:
    Ui::playlist *ui;
};

#endif // PLAYLIST_H
