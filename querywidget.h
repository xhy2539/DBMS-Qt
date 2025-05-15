#ifndef QUERYWIDGET_H
#define QUERYWIDGET_H

#include <QWidget>

namespace Ui {
class queryWidget;
}

class queryWidget : public QWidget
{
    Q_OBJECT

public:
    explicit queryWidget(QWidget *parent = nullptr);
    ~queryWidget();
    void clear();
    void setPlainText(const QString& text);
    void appendPlainText(const QString& text);
    void putin_focus();
signals:
    void sendString(const QString& text);

private:
    Ui::queryWidget *ui;
    QString dbName;
};

#endif // QUERYWIDGET_H
