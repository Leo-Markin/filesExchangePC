#ifndef ADMINWINDOW_H
#define ADMINWINDOW_H

#include "userwindow.h" // Наследуемся от UserWindow
#include "datatypes.h"  // Для UserData

namespace Ui { class AdminWindow; } // Используем UI админа

class AdminWindow : public UserWindow // Наследование
{
    Q_OBJECT

public:
    // Конструктор вызывает конструктор базового класса
    explicit AdminWindow(const QString &token, ApiClient *client, QWidget *parent = nullptr);
    ~AdminWindow(); // Деструктор

private slots:
    // --- Слоты для управления пользователями ---
    void on_addUserButton_clicked();
    void on_backupButton_clicked();

    // Слоты для кнопок в таблице пользователей
    void deleteUserClicked();
    void changePasswordClicked();

    // Слоты для обработки ответов API (пользователи и бэкап)
    void handleUserListSuccess(const QList<UserData> &users);
    void handleUserListFailed(const QString &errorString, int statusCode);
    void handleDeleteUserSuccess(const QString &deletedUserId);
    void handleDeleteUserFailed(const QString &failedUserId, const QString &errorString, int statusCode);
    void handleChangePasswordSuccess(const QString &userId);
    void handleChangePasswordFailed(const QString &userId, const QString &errorString, int statusCode);
    void handleCreateUserSuccess(const UserData &newUser);
    void handleCreateUserFailed(const QString &username, const QString &errorString, int statusCode);
    void handleBackupSuccess(const QString &message);
    void handleBackupFailed(const QString &errorString, int statusCode);

private:
    Ui::AdminWindow *adminUi; // Используем отдельный указатель на UI админа

    QList<UserData> allUsers; // Хранение списка пользователей

    void setupUsersTable(); // Настройка таблицы пользователей
    void requestUserList(); // Запрос списка пользователей
    void populateUsersTable(const QList<UserData> &usersToDisplay); // Заполнение таблицы пользователей
};

#endif // ADMINWINDOW_H
