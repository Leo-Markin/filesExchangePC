#include "adminwindow.h"
#include "ui_adminwindow.h" // Подключаем UI АДМИНА
#include "apiclient.h"
#include "ui_userwindow.h"

#include <QMessageBox>
#include <QDebug>
#include <QInputDialog> // Для ввода пароля/имени
#include <QTableWidget> // Для таблицы пользователей
#include <QPushButton>  // Для кнопок в таблице
#include <QHBoxLayout> // Для кнопок в таблице
#include <QHeaderView>

AdminWindow::AdminWindow(const QString &token, ApiClient *client, QWidget *parent) :
    UserWindow(token, client, parent), // Вызываем конструктор базового класса
    adminUi(nullptr)    // Создаем UI АДМИНА
{
    adminUi = new Ui::AdminWindow(); // Создаем UI админа
    adminUi->setupUi(this); // Устанавливаем UI админа для этого окна

    ui = reinterpret_cast<Ui::UserWindow*>(adminUi); // "Притворяемся", что adminUi - это UserWindow ui

    QProgressBar* progressBar = this->findChild<QProgressBar*>("uploadProgressBar"); // Или так, надежнее
    if (progressBar) {
        progressBar->setVisible(false);
        progressBar->setValue(0);
        progressBar->setTextVisible(true);
        progressBar->setFormat("Загрузка: %p%");
    } else {
        qWarning() << "AdminWindow: Не найден uploadProgressBar в adminUi!";
    }

    setupTable();

    QLineEdit* searchEdit = this->findChild<QLineEdit*>("searchLineEdit");
    if (searchEdit) {
        connect(searchEdit, &QLineEdit::textChanged, this, &AdminWindow::on_searchLineEdit_textChanged);
        qDebug() << "AdminWindow: searchLineEdit подключен.";
    } else { qWarning() << "AdminWindow: Не найден searchLineEdit!"; }

    QPushButton* uploadBtn = this->findChild<QPushButton*>("uploadButton");
    if (uploadBtn) {
        uploadBtn->setEnabled(true);
        uploadBtn->setText("Загрузить файл");
        // connect(uploadBtn, &QPushButton::clicked, this, &AdminWindow::on_uploadButton_clicked);
        qDebug() << "AdminWindow: uploadButton подключен.";
    } else { qWarning() << "AdminWindow: Не найден uploadButton!"; }

    // Настройка базового функционала (файлы) уже произошла в конструкторе UserWindow,
    // но нам нужно убедиться, что он использует элементы из adminUi->filesTab.
    // Метод setupTable() унаследован, вызовем его, он должен найти filesTableWidget.
    // Метод requestUserFiles() унаследован, он вызовется в конце.

    // --- Подключение АДМИНСКИХ сигналов API ---
    disconnect(apiClient, &ApiClient::userListSuccess, this, &AdminWindow::handleUserListSuccess);
    connect(apiClient, &ApiClient::userListSuccess, this, &AdminWindow::handleUserListSuccess);
    disconnect(apiClient, &ApiClient::userListFailed, this, &AdminWindow::handleUserListFailed);
    connect(apiClient, &ApiClient::userListFailed, this, &AdminWindow::handleUserListFailed);
    disconnect(apiClient, &ApiClient::deleteUserSuccess, this, &AdminWindow::handleDeleteUserSuccess);
    connect(apiClient, &ApiClient::deleteUserSuccess, this, &AdminWindow::handleDeleteUserSuccess);
    disconnect(apiClient, &ApiClient::deleteUserFailed, this, &AdminWindow::handleDeleteUserFailed);
    connect(apiClient, &ApiClient::deleteUserFailed, this, &AdminWindow::handleDeleteUserFailed);
    disconnect(apiClient, &ApiClient::changePasswordSuccess, this, &AdminWindow::handleChangePasswordSuccess);
    connect(apiClient, &ApiClient::changePasswordSuccess, this, &AdminWindow::handleChangePasswordSuccess);
    disconnect(apiClient, &ApiClient::changePasswordFailed, this, &AdminWindow::handleChangePasswordFailed);
    connect(apiClient, &ApiClient::changePasswordFailed, this, &AdminWindow::handleChangePasswordFailed);
    disconnect(apiClient, &ApiClient::createUserSuccess, this, &AdminWindow::handleCreateUserSuccess);
    connect(apiClient, &ApiClient::createUserSuccess, this, &AdminWindow::handleCreateUserSuccess);
    disconnect(apiClient, &ApiClient::createUserFailed, this, &AdminWindow::handleCreateUserFailed);
    connect(apiClient, &ApiClient::createUserFailed, this, &AdminWindow::handleCreateUserFailed);
    disconnect(apiClient, &ApiClient::backupSuccess, this, &AdminWindow::handleBackupSuccess);
    connect(apiClient, &ApiClient::backupSuccess, this, &AdminWindow::handleBackupSuccess);
    disconnect(apiClient, &ApiClient::backupFailed, this, &AdminWindow::handleBackupFailed);
    connect(apiClient, &ApiClient::backupFailed, this, &AdminWindow::handleBackupFailed);
    // -----------------------------------------

    setupUsersTable(); // Настраиваем таблицу пользователей
    requestUserList(); // Запрашиваем список пользователей при открытии
}

AdminWindow::~AdminWindow()
{
    // Отключаем АДМИНСКИЕ сигналы
    if (apiClient) {
        disconnect(apiClient, &ApiClient::userListSuccess, this, &AdminWindow::handleUserListSuccess);
        disconnect(apiClient, &ApiClient::userListFailed, this, &AdminWindow::handleUserListFailed);
        // ... отключаем остальные админские сигналы ...
        disconnect(apiClient, &ApiClient::deleteUserSuccess, this, &AdminWindow::handleDeleteUserSuccess);
        disconnect(apiClient, &ApiClient::deleteUserFailed, this, &AdminWindow::handleDeleteUserFailed);
        disconnect(apiClient, &ApiClient::changePasswordSuccess, this, &AdminWindow::handleChangePasswordSuccess);
        disconnect(apiClient, &ApiClient::changePasswordFailed, this, &AdminWindow::handleChangePasswordFailed);
        disconnect(apiClient, &ApiClient::createUserSuccess, this, &AdminWindow::handleCreateUserSuccess);
        disconnect(apiClient, &ApiClient::createUserFailed, this, &AdminWindow::handleCreateUserFailed);
        disconnect(apiClient, &ApiClient::backupSuccess, this, &AdminWindow::handleBackupSuccess);
        disconnect(apiClient, &ApiClient::backupFailed, this, &AdminWindow::handleBackupFailed);
    }
    // Деструктор базового класса UserWindow позаботится об отключении своих сигналов и удалении ui (если он не null)
    // Нам нужно удалить adminUi
    delete adminUi;
    ui = nullptr; // Обнуляем указатель базового класса
    qDebug() << "AdminWindow уничтожен.";
}

// --- Настройка и заполнение таблицы ПОЛЬЗОВАТЕЛЕЙ ---
void AdminWindow::setupUsersTable()
{
    if (!adminUi || !adminUi->usersTableWidget) return;
    adminUi->usersTableWidget->setColumnCount(2); // Имя, Действия
    adminUi->usersTableWidget->setHorizontalHeaderLabels({"Имя пользователя", "Действия"});
    adminUi->usersTableWidget->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    adminUi->usersTableWidget->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    adminUi->usersTableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    adminUi->usersTableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    adminUi->usersTableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    adminUi->usersTableWidget->verticalHeader()->setVisible(false);
    adminUi->usersTableWidget->setRowCount(0); // Очистка
}

void AdminWindow::requestUserList()
{
    if (!apiClient || !adminUi || !adminUi->usersTableWidget) return;
    adminUi->usersTableWidget->setEnabled(false);
    apiClient->getUserList(apiToken); // Используем apiToken, унаследованный от UserWindow
}

void AdminWindow::populateUsersTable(const QList<UserData> &usersToDisplay)
{
    if (!adminUi || !adminUi->usersTableWidget) return;
    adminUi->usersTableWidget->setRowCount(0); // Очищаем таблицу
    allUsers = usersToDisplay; // Сохраняем список

    for (int row = 0; row < usersToDisplay.count(); ++row) {
        const UserData &user = usersToDisplay.at(row);
        adminUi->usersTableWidget->insertRow(row);

        QTableWidgetItem *usernameItem = new QTableWidgetItem(user.username);
        adminUi->usersTableWidget->setItem(row, 0, usernameItem);

        // --- Кнопки действий для пользователя ---
        QWidget *actionsWidget = new QWidget();
        QHBoxLayout *actionsLayout = new QHBoxLayout(actionsWidget);
        actionsLayout->setContentsMargins(2, 0, 2, 0);
        actionsLayout->setSpacing(3);

        QPushButton *changePassButton = new QPushButton("Пароль");
        changePassButton->setToolTip("Изменить пароль пользователя");
        changePassButton->setProperty("userId", user.id);
        changePassButton->setProperty("username", user.username); // Для диалога
        connect(changePassButton, &QPushButton::clicked, this, &AdminWindow::changePasswordClicked);

        QPushButton *deleteButton = new QPushButton("Удалить");
        deleteButton->setToolTip("Удалить пользователя");
        deleteButton->setStyleSheet("QPushButton { color: red; }");
        deleteButton->setProperty("userId", user.id);
        deleteButton->setProperty("username", user.username); // Для диалога
        connect(deleteButton, &QPushButton::clicked, this, &AdminWindow::deleteUserClicked);

        actionsLayout->addWidget(changePassButton);
        actionsLayout->addWidget(deleteButton);
        actionsLayout->addStretch();
        actionsWidget->setLayout(actionsLayout);

        adminUi->usersTableWidget->setCellWidget(row, 1, actionsWidget);
    }
    adminUi->usersTableWidget->setEnabled(true); // Разблокируем после заполнения
}
// ---------------------------------------------

// --- Слоты для кнопок управления пользователями ---
void AdminWindow::on_addUserButton_clicked()
{
    bool ok1, ok2;
    QString username = QInputDialog::getText(this, "Новый пользователь", "Введите имя пользователя:", QLineEdit::Normal, "", &ok1);
    if (!ok1 || username.trimmed().isEmpty()) return; // Отмена или пустое имя

    QString password = QInputDialog::getText(this, "Новый пользователь", QString("Введите пароль для '%1':").arg(username), QLineEdit::Password, "", &ok2);
    if (!ok2 || password.isEmpty()) return; // Отмена или пустой пароль

    qDebug() << "AdminWindow: Запрос на создание пользователя" << username;
    apiClient->createNewUser(apiToken, username.trimmed(), password);
}

void AdminWindow::on_backupButton_clicked()
{
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "Создание бэкапа", "Запустить процесс создания резервной копии?", QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (reply == QMessageBox::Yes) {
        qDebug() << "AdminWindow: Запрос на создание бэкапа...";
        // (Опционально) Блокируем кнопку
        adminUi->backupButton->setEnabled(false);
        apiClient->triggerBackup(apiToken);
    }
}
// ---------------------------------------------

// --- Слоты для кнопок ВНУТРИ таблицы пользователей ---
void AdminWindow::deleteUserClicked()
{
    QPushButton *button = qobject_cast<QPushButton*>(sender());
    if (!button) return;
    QString userId = button->property("userId").toString();
    QString username = button->property("username").toString();
    if (userId.isEmpty()) return;

    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "Удаление пользователя", QString("Вы уверены, что хотите удалить пользователя '%1' (ID: %2)?").arg(username).arg(userId), QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        qDebug() << "AdminWindow: Запрос на удаление пользователя ID:" << userId;
        apiClient->deleteUser(apiToken, userId);
    }
}

void AdminWindow::changePasswordClicked()
{
    QPushButton *button = qobject_cast<QPushButton*>(sender());
    if (!button) return;
    QString userId = button->property("userId").toString();
    QString username = button->property("username").toString();
    if (userId.isEmpty()) return;

    bool ok;
    QString newPassword = QInputDialog::getText(this, "Смена пароля", QString("Введите новый пароль для '%1':").arg(username), QLineEdit::Password, "", &ok);
    if (ok && !newPassword.isEmpty()) {
        qDebug() << "AdminWindow: Запрос на смену пароля для пользователя ID:" << userId;
        apiClient->changeUserPassword(apiToken, userId, newPassword);
    } else if (ok && newPassword.isEmpty()) {
        QMessageBox::warning(this, "Смена пароля", "Пароль не может быть пустым.");
    }
}
// -----------------------------------------------

// --- Слоты обработки ответов API (пользователи, бэкап) ---
void AdminWindow::handleUserListSuccess(const QList<UserData> &users)
{
    qDebug() << "AdminWindow: Получен список пользователей:" << users.count();
    populateUsersTable(users);
}

void AdminWindow::handleUserListFailed(const QString &errorString, int statusCode)
{
    qWarning() << "AdminWindow: Ошибка получения списка пользователей. Статус:" << statusCode << "Ошибка:" << errorString;
    QMessageBox::critical(this, "Ошибка списка пользователей", errorString);
    adminUi->usersTableWidget->setEnabled(true); // Разблокируем в случае ошибки
}

void AdminWindow::handleDeleteUserSuccess(const QString &deletedUserId)
{
    qDebug() << "AdminWindow: Пользователь ID:" << deletedUserId << "успешно удален.";
    QMessageBox::information(this, "Успех", "Пользователь успешно удален.");
    requestUserList(); // Обновляем список
}

void AdminWindow::handleDeleteUserFailed(const QString &failedUserId, const QString &errorString, int statusCode)
{
    qWarning() << "AdminWindow: Ошибка удаления пользователя ID:" << failedUserId << "Статус:" << statusCode << "Ошибка:" << errorString;
    QMessageBox::warning(this, "Ошибка удаления", errorString);
}

void AdminWindow::handleChangePasswordSuccess(const QString &userId)
{
    qDebug() << "AdminWindow: Пароль для пользователя ID:" << userId << "успешно изменен.";
    QMessageBox::information(this, "Успех", "Пароль пользователя успешно изменен.");
}

void AdminWindow::handleChangePasswordFailed(const QString &userId, const QString &errorString, int statusCode)
{
    qWarning() << "AdminWindow: Ошибка смены пароля для ID:" << userId << "Статус:" << statusCode << "Ошибка:" << errorString;
    QMessageBox::warning(this, "Ошибка смены пароля", errorString);
}

void AdminWindow::handleCreateUserSuccess(const UserData &newUser)
{
    qDebug() << "AdminWindow: Пользователь ID:" << newUser.id << "Имя:" << newUser.username << "успешно создан.";
    QMessageBox::information(this, "Успех", QString("Пользователь '%1' успешно создан.").arg(newUser.username));
    requestUserList(); // Обновляем список
}

void AdminWindow::handleCreateUserFailed(const QString &username, const QString &errorString, int statusCode)
{
    qWarning() << "AdminWindow: Ошибка создания пользователя" << username << "Статус:" << statusCode << "Ошибка:" << errorString;
    QMessageBox::warning(this, "Ошибка создания пользователя", errorString);
}

void AdminWindow::handleBackupSuccess(const QString &message)
{
    qDebug() << "AdminWindow: Бэкап успешно запущен/завершен. Сообщение:" << message;
    QMessageBox::information(this, "Резервное копирование", message.isEmpty() ? "Процесс резервного копирования успешно запущен." : message);
    adminUi->backupButton->setEnabled(true); // Разблокируем кнопку
}

void AdminWindow::handleBackupFailed(const QString &errorString, int statusCode)
{
    qWarning() << "AdminWindow: Ошибка резервного копирования. Статус:" << statusCode << "Ошибка:" << errorString;
    QMessageBox::critical(this, "Ошибка резервного копирования", errorString);
    adminUi->backupButton->setEnabled(true); // Разблокируем кнопку
}
// -------------------------------------------------------
