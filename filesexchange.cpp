#include "filesexchange.h"
#include "ui_filesexchange.h"
#include "userwindow.h" // Добавили
#include "adminwindow.h"

#include <QMessageBox>
#include <QDebug>

FileseXchange::FileseXchange(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::FileseXchange)
    , apiClient(nullptr)
    , currentUserToken("") // Инициализируем переменные
    , currentUserRole("")
{
    ui->setupUi(this);

    // Создаем ApiClient
    apiClient = new ApiClient("http://s98036ho.beget.tech/api/", this); // Укажите ваш базовый URL

    // Соединяем сигналы со слотами (сигнатуры теперь совпадают)
    connect(apiClient, &ApiClient::loginSuccess, this, &FileseXchange::handleLoginSuccess);
    connect(apiClient, &ApiClient::loginFailed, this, &FileseXchange::handleLoginFailure);
}

FileseXchange::~FileseXchange()
{
    delete ui;
}

void FileseXchange::on_button_enter_clicked()
{
    QString login = ui->lineEdit_login->text();
    QString password = ui->lineEdit_password->text();

    if (login.isEmpty() || password.isEmpty()) {
        QMessageBox::warning(this, "Ошибка ввода", "Пожалуйста, введите логин и пароль.");
        return;
    }

    ui->button_enter->setEnabled(false);
    ui->statusbar->showMessage("Отправка запроса авторизации...");
    apiClient->login(login, password);
}

void FileseXchange::handleLoginSuccess(const QString &token, const QString &role)
{
    // Восстанавливать UI окна логина, возможно, уже не нужно, т.к. мы его закроем
    // ui->button_enter->setEnabled(true);
    // ui->statusbar->clearMessage();

    qDebug() << "FileseXchange: Авторизация успешна! Токен:" << token << "Роль:" << role;

    // Сохраняем токен и роль (хотя они сразу передаются дальше)
    currentUserToken = token;
    currentUserRole = role;

    // QMessageBox::information(this, "Успех", "Авторизация прошла успешно!"); // Можно убрать это сообщение

    // --- Логика перехода в зависимости от роли ---
    if (currentUserRole == "user") {
        qDebug() << "FileseXchange: Пользователь 'user'. Открываем окно пользователя...";

        // Создаем и показываем окно пользователя, передавая токен и ApiClient
        // Передаем apiClient, чтобы UserWindow мог делать свои запросы
        UserWindow *userWin = new UserWindow(currentUserToken, apiClient, nullptr); // nullptr - родитель, окно будет независимым
        userWin->setupUserInterface();

        // Устанавливаем флаг, чтобы окно удалилось само при закрытии
        userWin->setAttribute(Qt::WA_DeleteOnClose);

        userWin->show(); // Показываем новое окно

        // Закрываем окно логина
        this->close(); // Или hide(), если хотите иметь возможность вернуться

    } else if (currentUserRole == "admin") {
        qDebug() << "FileseXchange: Пользователь 'admin'. Открываем AdminWindow...";
        // --- ИЗМЕНЕНИЕ: Создаем AdminWindow ---
        AdminWindow *adminWin = new AdminWindow(currentUserToken, apiClient, nullptr);
        adminWin->setAttribute(Qt::WA_DeleteOnClose);
        adminWin->show();
        this->close(); // Закрываем окно логина
        // -------------------------------------

    } else {
        qDebug() << "FileseXchange: Неизвестная роль:" << currentUserRole;
        QMessageBox::warning(this, "Неизвестная роль", QString("Получена неизвестная роль пользователя: %1. Вход невозможен.").arg(currentUserRole));
        // Восстанавливаем кнопку логина, если вход не выполнен
        ui->button_enter->setEnabled(true);
        ui->statusbar->clearMessage();
        currentUserToken = ""; // Сбрасываем данные
        currentUserRole = "";
    }
}

void FileseXchange::handleLoginFailure(const QString &errorString, int statusCode)
{
    ui->button_enter->setEnabled(true);
    ui->statusbar->clearMessage();
    qWarning() << "FileseXchange: Ошибка авторизации. Статус:" << statusCode << "Ошибка:" << errorString;
    QMessageBox::warning(this, "Ошибка авторизации", errorString);
    currentUserToken = "";
    currentUserRole = "";
}
