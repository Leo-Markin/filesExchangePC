#ifndef APICLIENT_H
#define APICLIENT_H

#include <QObject>
#include <QString>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QUrlQuery>
#include <QUrl>
#include <QJsonObject>
#include <QHttpMultiPart> // <-- для multipart/form-data
#include <QMimeDatabase> // <--  для определения MIME-типа
#include <QFileInfo>     // <--  для получения имени файла
#include <QList>       // Для списка файлов
#include "datatypes.h" // Включаем нашу структуру FileInfo
#include <QFile>

class ApiClient : public QObject
{
    Q_OBJECT

public:
    explicit ApiClient(const QString &baseUrl, QObject *parent = nullptr);
    ~ApiClient();

    // --- Методы API ---
    void login(const QString &username, const QString &password);
    void getUserFiles(const QString &token);
    void uploadFile(const QString &token, const QString &filePath);
    void getFileInfo(const QString &token, const QString &fileUrlIdentifier);
    void downloadFile(const QString &token, const QString &fileId, const QString &originalFileName);
    void deleteFile(const QString &token, const QString &fileId);
    void getUserList(const QString &token);
    void deleteUser(const QString &token, const QString &userId);
    void changeUserPassword(const QString &token, const QString &userId, const QString &newPassword);
    void createNewUser(const QString &token, const QString &username, const QString &password);
    void triggerBackup(const QString &token);

signals:
    // --- Сигналы результата ---
    void loginSuccess(const QString &token, const QString &role);
    void loginFailed(const QString &errorString, int statusCode = 0);

    // Новые сигналы для списка файлов
    void userFilesSuccess(const QList<FileInfo> &files);
    void userFilesFailed(const QString &errorString, int statusCode = 0);

    // Новые сигналы для загрузки файлов
    void uploadSuccess(); // Сигнал при успехе
    void uploadProgress(qint64 bytesSent, qint64 bytesTotal); // Сигнал для прогресса
    void uploadFailed(const QString &errorString, int statusCode = 0); // Сигнал при ошибке

    // Новые сигналы для получения информации о файле
    void fileInfoSuccess(const QJsonObject &fileData);
    void fileInfoFailed(const QString &errorString, int statusCode = 0);

    // Новые сигналы для скачивания файла
    void downloadSuccess(const QString &requestedFileId, const QByteArray &fileData, const QString &originalFileName);
    void downloadProgress(const QString &requestedFileId, qint64 bytesReceived, qint64 bytesTotal);
    void downloadFailed(const QString &requestedFileId, const QString &errorString, int statusCode = 0);

    // Новые сигналы для удаления файла
    void deleteSuccess(const QString &deletedFileId);
    void deleteFailed(const QString &failedFileId, const QString &errorString, int statusCode = 0);

    void userListSuccess(const QList<UserData> &users);
    void userListFailed(const QString &errorString, int statusCode = 0);

    void deleteUserSuccess(const QString &deletedUserId);
    void deleteUserFailed(const QString &failedUserId, const QString &errorString, int statusCode = 0);

    void changePasswordSuccess(const QString &userId);
    void changePasswordFailed(const QString &userId, const QString &errorString, int statusCode = 0);

    void createUserSuccess(const UserData &newUser); // Возвращаем данные нового юзера (ID и имя)
    void createUserFailed(const QString &username, const QString &errorString, int statusCode = 0);

    void backupSuccess(const QString &message); // Сервер может вернуть сообщение
    void backupFailed(const QString &errorString, int statusCode = 0);


private:
    QNetworkAccessManager *networkManager;
    QString apiBaseUrl;

    QUrl buildUrl(const QString &endpoint) const;
};

#endif // APICLIENT_H
