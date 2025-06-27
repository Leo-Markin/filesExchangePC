#ifndef FILESEXCHANGE_H
#define FILESEXCHANGE_H

#include <QMainWindow>
#include "apiclient.h"

QT_BEGIN_NAMESPACE
namespace Ui { class FileseXchange; }
QT_END_NAMESPACE

class FileseXchange : public QMainWindow
{
    Q_OBJECT

public:
    FileseXchange(QWidget *parent = nullptr);
    ~FileseXchange();

private slots:
    void on_button_enter_clicked();

    // Сигнатура слота для соответствия сигналу ApiClient
    void handleLoginSuccess(const QString &token, const QString &role);
    void handleLoginFailure(const QString &errorString, int statusCode);

private:
    Ui::FileseXchange *ui;
    ApiClient *apiClient;

    // Добавляем переменные для хранения состояния сессии
    QString currentUserToken;
    QString currentUserRole;

    // Метод для перехода к следующему этапу
    void proceedToAppInterface();
};
#endif // FILESEXCHANGE_H
