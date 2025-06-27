#include "apiclient.h"
#include <QNetworkRequest>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QUrlQuery>

namespace {

QString parseErrorMessage(const QByteArray &responseData, const QString &defaultPrefix)
{
    QString errorMsg = defaultPrefix; // Сообщение по умолчанию

    if (responseData.isEmpty()) {
        return errorMsg;
    }

    // Пытаемся разобрать как JSON
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(responseData, &parseError);

    if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
        // Успешно разобрали JSON объект
        QJsonObject obj = doc.object();
        if (obj.contains("message") && obj["message"].isString() && !obj["message"].toString().isEmpty()) {
            errorMsg = obj["message"].toString();
        } else if (obj.contains("status") && obj["status"].isString() && !obj["status"].toString().isEmpty()) {
            errorMsg = QString("%1: %2").arg(defaultPrefix).arg(obj["status"].toString());
        } else {
            // JSON есть, но нужных ключей нет, возвращаем префикс + сырой JSON
            errorMsg = QString("%1: %2").arg(defaultPrefix).arg(QString::fromUtf8(responseData));
        }
    } else {
        // Не удалось разобрать как JSON или это не объект, считаем текстом
        if (responseData.size() < 256) { // Ограничение длины
            errorMsg = QString("%1: %2").arg(defaultPrefix).arg(QString::fromUtf8(responseData).trimmed());
        } else {
            errorMsg = QString("%1 (ответ сервера слишком длинный)").arg(defaultPrefix);
        }
    }

    return errorMsg;
}

}

ApiClient::ApiClient(const QString &baseUrl, QObject *parent)
    : QObject(parent), apiBaseUrl(baseUrl)
{
    networkManager = new QNetworkAccessManager(this);
    if (!apiBaseUrl.isEmpty() && !apiBaseUrl.endsWith('/')) {
        apiBaseUrl.append('/');
    }
}

ApiClient::~ApiClient()
{
    qDebug() << "ApiClient уничтожен.";
}

QUrl ApiClient::buildUrl(const QString &endpoint) const
{
    QString cleanEndpoint = endpoint;
    if (cleanEndpoint.startsWith('/')) {
        cleanEndpoint = cleanEndpoint.mid(1);
    }
    return QUrl(apiBaseUrl + cleanEndpoint);
}

void ApiClient::login(const QString &username, const QString &password)
{
    QUrl loginUrl = buildUrl("auth.php");
    QNetworkRequest request(loginUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QUrlQuery postData;
    postData.addQueryItem("username", username);
    postData.addQueryItem("password", password);

    qDebug() << "ApiClient: Отправка POST на" << loginUrl.toString();
    qDebug() << "ApiClient: Данные:" << postData.toString(QUrl::FullyEncoded).toUtf8();

    QNetworkReply *reply = networkManager->post(request, postData.toString(QUrl::FullyEncoded).toUtf8());

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        qDebug() << "ApiClient: Ответ получен для" << reply->url().toString();

        if (reply->error() == QNetworkReply::NoError)
        {
            int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QByteArray responseData = reply->readAll();
            qDebug() << "ApiClient: Статус код:" << statusCode;
            qDebug() << "ApiClient: Тело ответа:" << responseData;

            // Считаем успехом ТОЛЬКО статус 200 OK для логина
            if (statusCode == 200) {
                // Попытка парсинга JSON
                QJsonParseError parseError;
                QJsonDocument doc = QJsonDocument::fromJson(responseData, &parseError);

                if (parseError.error == QJsonParseError::NoError && doc.isObject())
                {
                    QJsonObject jsonObj = doc.object();
                    if (jsonObj.contains("token_api") && jsonObj["token_api"].isString() &&
                        jsonObj.contains("role") && jsonObj["role"].isString())
                    {
                        QString token = jsonObj["token_api"].toString();
                        QString role = jsonObj["role"].toString();
                        if (!token.isEmpty()) {
                            qDebug() << "ApiClient: Успешный парсинг. Токен:" << token << "Роль:" << role;
                            emit loginSuccess(token, role);
                        } else {
                            qWarning() << "ApiClient: Ошибка парсинга - получен пустой токен.";
                            emit loginFailed("Ошибка ответа сервера: получен пустой токен.", statusCode);
                        }
                    } else {
                        qWarning() << "ApiClient: Ошибка парсинга - отсутствуют ключи 'token_api'/'role' или они не строки.";
                        emit loginFailed("Ошибка ответа сервера: неверный формат данных (отсутствуют token_api/role).", statusCode);
                    }
                } else {
                    qWarning() << "ApiClient: Ошибка парсинга JSON:" << parseError.errorString();
                    emit loginFailed("Ошибка ответа сервера: не удалось разобрать JSON (" + parseError.errorString() + ").", statusCode);
                }
            } else {
                QString errorMsg = QString("Неожиданный ответ сервера (Код: %1)").arg(statusCode);
                if (!responseData.isEmpty()) {
                    errorMsg = QString("Сервер вернул ошибку %1: %2").arg(statusCode).arg(QString::fromUtf8(responseData));
                }
                // Специальные сообщения для частых ошибок авторизации
                if (statusCode == 201) {
                    errorMsg = "Неверный логин или пароль.";
                } else if (statusCode >= 500) {
                    errorMsg = QString("Внутренняя ошибка сервера (Код: %1)").arg(statusCode);
                }
                qWarning() << "ApiClient: Запрос завершился с ошибкой или неожиданным статусом:" << statusCode;
                emit loginFailed(errorMsg, statusCode);
            }
        }

        reply->deleteLater();
    });

    connect(reply, &QNetworkReply::errorOccurred, this, [this, reply](QNetworkReply::NetworkError code) {
        if (code == QNetworkReply::NoError) return;
        qWarning() << "ApiClient: Ошибка сети (" << code << ") для" << reply->url().toString() << ":" << reply->errorString();
        if (!reply) return;
        emit loginFailed(QString("Ошибка сети: %1").arg(reply->errorString()), 0); // 0 для сетевых ошибок
        reply->deleteLater();
    });
}

// Метод для запроса списка файлов пользователя
void ApiClient::getUserFiles(const QString &token)
{
    if (token.isEmpty()) {
        qWarning() << "ApiClient::getUserFiles: Попытка запроса файлов с пустым токеном.";
        // Отправляем сигнал ошибки немедленно, не делая запрос
        emit userFilesFailed("Внутренняя ошибка: отсутствует токен авторизации.", 0);
        return;
    }
    QUrl filesUrl = buildUrl("user_files.php");
    QNetworkRequest request(filesUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QUrlQuery postData;
    postData.addQueryItem("token_api", token); // Отправляем токен как параметр POST

    qDebug() << "ApiClient: Запрос списка файлов на" << filesUrl.toString();
    qDebug() << "ApiClient: Токен:" << token;

    QNetworkReply *reply = networkManager->post(request, postData.toString(QUrl::FullyEncoded).toUtf8());

    // Соединяем сигналы ответа с лямбдами
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        qDebug() << "ApiClient: Ответ на запрос файлов получен для" << reply->url().toString();

        if (reply->error() == QNetworkReply::NoError) {
            int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QByteArray responseData = reply->readAll();
            qDebug() << "ApiClient: Статус код (файлы):" << statusCode;
            qDebug() << "ApiClient: Тело ответа (файлы):" << responseData;

            if (statusCode == 200) {
                QJsonParseError parseError;
                QJsonDocument doc = QJsonDocument::fromJson(responseData, &parseError);

                if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                    QJsonObject jsonObj = doc.object();
                    // Проверяем статус и наличие массива 'files'
                    if (jsonObj.contains("status") && jsonObj["status"].toString() == "success" &&
                        jsonObj.contains("files") && jsonObj["files"].isArray())
                    {
                        QList<FileInfo> fileList;
                        QJsonArray filesArray = jsonObj["files"].toArray();

                        for (const QJsonValue &value : filesArray) {
                            if (value.isObject()) {
                                QJsonObject fileObj = value.toObject();
                                FileInfo file;
                                file.id = fileObj.value("id").toString(); // Используем value().toString() для безопасности
                                file.fileName = fileObj.value("file_name").toString();
                                file.ownerName = fileObj.value("owner_name").toString();
                                file.fileSize = fileObj.value("file_size").toString();
                                file.fileUrl = fileObj.value("file_url").toString();
                                file.uploadDate = fileObj.value("upload_date").toString();
                                file.countViews = fileObj.value("count_views").toString();

                                if (!file.id.isEmpty() && !file.fileName.isEmpty() && !file.fileUrl.isEmpty()) {
                                    fileList.append(file);
                                } else {
                                    qWarning() << "ApiClient: Пропущен файл с неполными данными:" << fileObj;
                                }
                            }
                        }
                        qDebug() << "ApiClient: Успешно получено и разобрано" << fileList.count() << "файлов.";
                        emit userFilesSuccess(fileList); // Отправляем список файлов

                    } else {
                        qWarning() << "ApiClient: Ошибка ответа сервера (файлы) - неверный статус или отсутствует массив 'files'.";
                        QString errMsg = jsonObj.contains("message") ? jsonObj["message"].toString() : "Неверный формат ответа от сервера.";
                        emit userFilesFailed(errMsg, statusCode);
                    }
                } else {
                    qWarning() << "ApiClient: Ошибка парсинга JSON (файлы):" << parseError.errorString();
                    emit userFilesFailed("Ошибка ответа сервера: не удалось разобрать JSON (" + parseError.errorString() + ").", statusCode);
                }
            } else {
                QString errorMsg = QString("Ошибка сервера при получении файлов (Код: %1)").arg(statusCode);
                if (!responseData.isEmpty()) {
                    // Пытаемся получить текст ошибки из ответа
                    errorMsg = QString("Сервер вернул ошибку %1: %2").arg(statusCode).arg(QString::fromUtf8(responseData));
                }
                if (statusCode == 401 || statusCode == 403) { // Например, если токен невалидный
                    errorMsg = "Ошибка авторизации при доступе к файлам (неверный или истекший токен?).";
                }
                qWarning() << "ApiClient: Запрос файлов завершился с ошибкой или неожиданным статусом:" << statusCode;
                emit userFilesFailed(errorMsg, statusCode);
            }

        }
        // Сетевая ошибка обработается в errorOccurred
        reply->deleteLater();
    });

    // Обработка сетевых ошибок
    connect(reply, &QNetworkReply::errorOccurred, this, [this, reply](QNetworkReply::NetworkError code) {
        if (code == QNetworkReply::NoError) return;
        qWarning() << "ApiClient: Ошибка сети (" << code << ") при запросе файлов для" << reply->url().toString() << ":" << reply->errorString();
        if (!reply) return;
        emit userFilesFailed(QString("Ошибка сети: %1").arg(reply->errorString()), 0); // 0 для сетевых ошибок
        reply->deleteLater();
    });
}

// Метод для загрузки файла
void ApiClient::uploadFile(const QString &token, const QString &filePath)
{
    // --- Проверка входных данных ---
    if (token.isEmpty()) {
        qWarning() << "ApiClient::uploadFile: Попытка загрузки с пустым токеном.";
        emit uploadFailed("Внутренняя ошибка: отсутствует токен авторизации.", 0);
        return;
    }
    QFile *file = new QFile(filePath); // Создаем QFile в куче, чтобы управлять его жизнью
    if (!file->exists()) {
        qWarning() << "ApiClient::uploadFile: Файл не найден:" << filePath;
        emit uploadFailed(QString("Ошибка: Файл '%1' не найден.").arg(QFileInfo(filePath).fileName()), 0);
        delete file; // Не забываем удалить объект файла
        return;
    }
    if (!file->open(QIODevice::ReadOnly)) {
        qWarning() << "ApiClient::uploadFile: Не удалось открыть файл для чтения:" << filePath << file->errorString();
        emit uploadFailed(QString("Ошибка: Не удалось открыть файл '%1' для чтения.").arg(QFileInfo(filePath).fileName()), 0);
        delete file;
        return;
    }

    // --- Подготовка запроса ---
    QUrl uploadUrl = buildUrl("upload_file.php");
    QNetworkRequest request(uploadUrl);
    // --- Создание multipart/form-data ---
    // QHttpMultiPart должен существовать до завершения ответа, делаем его дочерним для reply
    QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    // 1. Добавляем токен (как обычное поле формы)
    QHttpPart tokenPart;
    tokenPart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"token_api\""));
    tokenPart.setBody(token.toUtf8());
    multiPart->append(tokenPart);

    // 2. Добавляем файл
    QHttpPart filePart;
    QFileInfo fileInfo(filePath);
    QString fileName = fileInfo.fileName();
    // Устанавливаем Content-Disposition с именем файла
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QString("form-data; name=\"file\"; filename=\"%1\"").arg(fileName)));

    // Устанавливаем Content-Type файла
    QMimeDatabase mimeDb;
    QMimeType mimeType = mimeDb.mimeTypeForFile(fileInfo);
    if (mimeType.isValid()) {
        qDebug() << "ApiClient: Определен MIME-тип файла:" << mimeType.name();
        filePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(mimeType.name()));
    } else {
        qDebug() << "ApiClient: Не удалось определить MIME-тип, используется application/octet-stream";
        filePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("application/octet-stream"));
    }

    filePart.setBodyDevice(file); // Передаем открытый QFile напрямую
    file->setParent(multiPart);
    multiPart->append(filePart);

    // --- Отправка запроса ---
    qDebug() << "ApiClient: Отправка файла" << fileName << "на" << uploadUrl.toString();
    QNetworkReply *reply = networkManager->post(request, multiPart);

    multiPart->setParent(reply);

    // --- Обработка ответа ---
    connect(reply, &QNetworkReply::uploadProgress, this, [this](qint64 bytesSent, qint64 bytesTotal) {
        if (bytesTotal > 0) { // Избегаем деления на ноль и бессмысленных сигналов
            //qDebug() << "Upload progress:" << bytesSent << "/" << bytesTotal;
            emit uploadProgress(bytesSent, bytesTotal);
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, fileName]() { // Захватываем fileName для логов
        qDebug() << "ApiClient: Ответ на загрузку файла" << fileName << "получен.";

        // Проверка на сетевые ошибки
        if (reply->error() == QNetworkReply::NoError) {
            int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QByteArray responseData = reply->readAll();
            qDebug() << "ApiClient: Статус код (загрузка):" << statusCode;
            qDebug() << "ApiClient: Тело ответа (загрузка):" << responseData;

            if (statusCode == 200) {
                qDebug() << "ApiClient: Файл" << fileName << "успешно загружен.";
                emit uploadSuccess();
            } else {
                // Ошибка сервера (не 200 OK)
                QString errorMsg = QString("Ошибка сервера при загрузке файла '%1' (Код: %2)").arg(fileName).arg(statusCode);
                if (!responseData.isEmpty()) {
                    QJsonDocument doc = QJsonDocument::fromJson(responseData);
                    if (!doc.isNull() && doc.isObject() && doc.object().contains("message")) {
                        errorMsg = QString("Ошибка загрузки '%1': %2").arg(fileName).arg(doc.object()["message"].toString());
                    } else if (!responseData.isEmpty()){
                        errorMsg = QString("Ошибка сервера при загрузке '%1' (%2): %3").arg(fileName).arg(statusCode).arg(QString::fromUtf8(responseData));
                    }
                }
                if (statusCode == 201) {
                    errorMsg = QString("Ошибка авторизации при загрузке файла '%1'.").arg(fileName);
                }
                qWarning() << "ApiClient: Загрузка файла" << fileName << "завершилась с ошибкой или неожиданным статусом:" << statusCode;
                emit uploadFailed(errorMsg, statusCode);
            }
        }
        // Сетевая ошибка обработается в errorOccurred

        reply->deleteLater(); // Удалит reply, multiPart и file
    });

    connect(reply, &QNetworkReply::errorOccurred, this, [this, reply, fileName](QNetworkReply::NetworkError code) {
        if (code == QNetworkReply::NoError) return;
        qWarning() << "ApiClient: Ошибка сети (" << code << ") при загрузке файла" << fileName << "для" << reply->url().toString() << ":" << reply->errorString();
        if (!reply) return;
        emit uploadFailed(QString("Ошибка сети при загрузке '%1': %2").arg(fileName).arg(reply->errorString()), 0);
        reply->deleteLater(); // Удалит reply, multiPart и file
    });
}

// Метод для запроса детальной информации о файле
void ApiClient::getFileInfo(const QString &token, const QString &fileUrlIdentifier)
{
    if (token.isEmpty() || fileUrlIdentifier.isEmpty()) {
        qWarning() << "ApiClient::getFileInfo: Попытка запроса с пустым токеном или идентификатором файла.";
        emit fileInfoFailed("Внутренняя ошибка: отсутствует токен или идентификатор файла.", 0);
        return;
    }

    QUrl infoUrl = buildUrl("file_info.php");
    QNetworkRequest request(infoUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QUrlQuery postData;
    postData.addQueryItem("token_api", token);
    postData.addQueryItem("file_url", fileUrlIdentifier);

    qDebug() << "ApiClient: Запрос информации о файле на" << infoUrl.toString();
    qDebug() << "ApiClient: Токен:" << token << "Идентификатор URL:" << fileUrlIdentifier;

    QNetworkReply *reply = networkManager->post(request, postData.toString(QUrl::FullyEncoded).toUtf8());

    connect(reply, &QNetworkReply::finished, this, [this, reply, fileUrlIdentifier]() {
        qDebug() << "ApiClient: Ответ на запрос информации о файле" << fileUrlIdentifier << "получен.";

        if (reply->error() == QNetworkReply::NoError) {
            int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QByteArray responseData = reply->readAll();
            qDebug() << "ApiClient: Статус код (инфо):" << statusCode;
            qDebug() << "ApiClient: Тело ответа (инфо):" << responseData;

            if (statusCode == 200) {
                QJsonParseError parseError;
                QJsonDocument doc = QJsonDocument::fromJson(responseData, &parseError);

                if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                    QJsonObject jsonObj = doc.object();
                    // Проверяем наличие ключевых полей (можно добавить больше проверок)
                    if (jsonObj.contains("file_name") && jsonObj.contains("file_size")) {
                        qDebug() << "ApiClient: Информация о файле" << fileUrlIdentifier << "успешно получена и разобрана.";
                        emit fileInfoSuccess(jsonObj); // Отправляем весь JSON объект
                    } else {
                        qWarning() << "ApiClient: Ошибка ответа сервера (инфо) - отсутствуют необходимые поля.";
                        emit fileInfoFailed("Ошибка ответа сервера: неверный формат данных.", statusCode);
                    }
                } else {
                    qWarning() << "ApiClient: Ошибка парсинга JSON (инфо):" << parseError.errorString();
                    emit fileInfoFailed("Ошибка ответа сервера: не удалось разобрать JSON (" + parseError.errorString() + ").", statusCode);
                }
            } else {
                QString errorMsg = QString("Ошибка сервера при получении информации о файле (Код: %1)").arg(statusCode);
                if (!responseData.isEmpty()) {
                    // Попытка извлечь сообщение из ответа
                    QJsonDocument doc = QJsonDocument::fromJson(responseData);
                    if (!doc.isNull() && doc.isObject() && doc.object().contains("message")) {
                        errorMsg = doc.object()["message"].toString();
                    } else if (!responseData.isEmpty()){
                        errorMsg = QString("Сервер вернул ошибку %1: %2").arg(statusCode).arg(QString::fromUtf8(responseData));
                    }
                }
                if (statusCode == 401 || statusCode == 403) {
                    errorMsg = "Ошибка авторизации при доступе к информации о файле.";
                }
                qWarning() << "ApiClient: Запрос информации о файле" << fileUrlIdentifier << "завершился с ошибкой:" << statusCode;
                emit fileInfoFailed(errorMsg, statusCode);
            }
        }
        // Сетевая ошибка обработается в errorOccurred
        reply->deleteLater();
    });

    // Обработка сетевых ошибок
    connect(reply, &QNetworkReply::errorOccurred, this, [this, reply, fileUrlIdentifier](QNetworkReply::NetworkError code) {
        if (code == QNetworkReply::NoError) return;
        qWarning() << "ApiClient: Ошибка сети (" << code << ") при запросе инфо о файле" << fileUrlIdentifier << ":" << reply->errorString();
        if (!reply) return;
        emit fileInfoFailed(QString("Ошибка сети: %1").arg(reply->errorString()), 0);
        reply->deleteLater();
    });
}

// Метод для скачивания файла
void ApiClient::downloadFile(const QString &token, const QString &fileId, const QString &originalFileName)
{
    // --- Проверка входных данных ---
    if (token.isEmpty() || fileId.isEmpty()) {
        qWarning() << "ApiClient::downloadFile: Попытка скачивания с пустым токеном или ID файла.";
        emit downloadFailed(fileId, "Внутренняя ошибка: отсутствует токен или ID файла.", 0);
        return;
    }

    // --- Подготовка URL с параметрами GET ---
    QUrl downloadUrl = buildUrl("download_file.php"); // Базовый URL эндпоинта

    // Создаем объект QUrlQuery для добавления параметров в URL
    QUrlQuery query;
    query.addQueryItem("token_api", token);
    query.addQueryItem("file_id", fileId);
    downloadUrl.setQuery(query); // Добавляем параметры к URL

    // --- Подготовка запроса ---
    QNetworkRequest request(downloadUrl); // URL уже содержит параметры GET

    // --- Отправка запроса GET ---
    qDebug() << "ApiClient: Запрос GET на скачивание файла:" << downloadUrl.toString(); // Теперь URL включает параметры
    QNetworkReply *reply = networkManager->get(request); // ИСПОЛЬЗУЕМ GET вместо POST

    // --- Обработка ответа  ---
    connect(reply, &QNetworkReply::downloadProgress, this, [this, reply, fileId](qint64 bytesReceived, qint64 bytesTotal) {
        emit downloadProgress(fileId, bytesReceived, bytesTotal);
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, fileId, originalFileName]() {
        qDebug() << "ApiClient: Ответ на скачивание файла ID:" << fileId << "получен.";

        if (reply->error() == QNetworkReply::NoError) {
            int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            qDebug() << "ApiClient: Статус код (скачивание):" << statusCode;

            if (statusCode == 200) {
                QByteArray fileData = reply->readAll();
                if (!fileData.isEmpty()) {
                    qDebug() << "ApiClient: Файл ID:" << fileId << "успешно скачан (" << fileData.size() << "байт).";
                    emit downloadSuccess(fileId, fileData, originalFileName);
                } else {
                    qWarning() << "ApiClient: Скачивание файла ID:" << fileId << "завершилось успешно (код 200), но получены пустые данные.";
                    emit downloadFailed(fileId, "Сервер вернул пустой файл.", statusCode);
                }
            } else {
                // Обработка ошибок HTT
                QByteArray errorData = reply->readAll();
                QString errorMsg = QString("Ошибка сервера при скачивании файла (Код: %1)").arg(statusCode);
                // Пытаемся разобрать JSON ошибку, которую возвращает ваш API при коде 201
                if (statusCode == 201 && !errorData.isEmpty()) {
                    QJsonDocument doc = QJsonDocument::fromJson(errorData);
                    if (!doc.isNull() && doc.isObject() && doc.object().contains("status")) {
                        errorMsg = QString("Ошибка скачивания: %1").arg(doc.object()["status"].toString());
                    } else {
                        errorMsg += ": " + QString::fromUtf8(errorData); // Если не JSON
                    }
                } else if (!errorData.isEmpty()) { // Для других кодов ошибок
                    errorMsg += ": " + QString::fromUtf8(errorData);
                }

                qWarning() << "ApiClient: Скачивание файла ID:" << fileId << "завершилось с ошибкой или неожиданным статусом:" << statusCode;
                emit downloadFailed(fileId, errorMsg, statusCode);
            }
        }
        // Сетевая ошибка обработается в errorOccurred

        reply->deleteLater();
    });

    connect(reply, &QNetworkReply::errorOccurred, this, [this, reply, fileId](QNetworkReply::NetworkError code) {
        if (code == QNetworkReply::NoError) return;
        qWarning() << "ApiClient: Ошибка сети (" << code << ") при скачивании файла ID:" << fileId << "для" << reply->url().toString() << ":" << reply->errorString();
        if (!reply) return;
        emit downloadFailed(fileId, QString("Ошибка сети: %1").arg(reply->errorString()), 0);
        reply->deleteLater();
    });
}

// Метод для удаления файла
void ApiClient::deleteFile(const QString &token, const QString &fileId)
{
    // --- Проверка входных данных ---
    if (token.isEmpty() || fileId.isEmpty()) {
        qWarning() << "ApiClient::deleteFile: Попытка удаления с пустым токеном или ID файла.";
        emit deleteFailed(fileId, "Внутренняя ошибка: отсутствует токен или ID файла.", 0);
        return;
    }

    // --- Подготовка запроса ---
    QUrl deleteUrl = buildUrl("delete_file.php");
    QNetworkRequest request(deleteUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    // --- Подготовка данных POST ---
    QUrlQuery postData;
    postData.addQueryItem("token_api", token);
    postData.addQueryItem("file_id", fileId);

    // --- Отправка запроса POST ---
    qDebug() << "ApiClient: Запрос POST на удаление файла ID:" << fileId << "на" << deleteUrl.toString();
    QNetworkReply *reply = networkManager->post(request, postData.toString(QUrl::FullyEncoded).toUtf8());

    // --- Обработка ответа ---
    connect(reply, &QNetworkReply::finished, this, [this, reply, fileId]() { // Захватываем fileId
        qDebug() << "ApiClient: Ответ на удаление файла ID:" << fileId << "получен.";

        if (reply->error() == QNetworkReply::NoError) {
            int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QByteArray responseData = reply->readAll();
            qDebug() << "ApiClient: Статус код (удаление):" << statusCode;
            qDebug() << "ApiClient: Тело ответа (удаление):" << responseData;

            if (statusCode == 200) {
                qDebug() << "ApiClient: Файл ID:" << fileId << "успешно удален.";
                emit deleteSuccess(fileId);

            } else {
                // Ошибка сервера (не 200 OK)
                QString errorMsg = QString("Ошибка сервера при удалении файла (Код: %1)").arg(statusCode);
                if (!responseData.isEmpty()) {
                    // Пытаемся извлечь сообщение из ответа (может быть JSON или текст)
                    QJsonDocument doc = QJsonDocument::fromJson(responseData);
                    if (!doc.isNull() && doc.isObject() && doc.object().contains("message")) {
                        errorMsg = QString("Ошибка удаления: %1").arg(doc.object()["message"].toString());
                    } else if (doc.object().contains("status")) { // Проверка на ваш формат {"status":"..."}
                        errorMsg = QString("Ошибка удаления: %1").arg(doc.object()["status"].toString());
                    } else if (!responseData.isEmpty()){
                        errorMsg = QString("Сервер вернул ошибку %1: %2").arg(statusCode).arg(QString::fromUtf8(responseData));
                    }
                }
                if (statusCode == 401 || statusCode == 403) {
                    errorMsg = "Ошибка авторизации при удалении файла.";
                } else if (statusCode == 404) {
                    errorMsg = "Файл для удаления не найден на сервере.";
                }
                qWarning() << "ApiClient: Удаление файла ID:" << fileId << "завершилось с ошибкой или неожиданным статусом:" << statusCode;
                emit deleteFailed(fileId, errorMsg, statusCode);
            }
        }
        // Сетевая ошибка обработается в errorOccurred

        reply->deleteLater();
    });

    connect(reply, &QNetworkReply::errorOccurred, this, [this, reply, fileId](QNetworkReply::NetworkError code) {
        if (code == QNetworkReply::NoError) return;
        qWarning() << "ApiClient: Ошибка сети (" << code << ") при удалении файла ID:" << fileId << "для" << reply->url().toString() << ":" << reply->errorString();
        if (!reply) return;
        emit deleteFailed(fileId, QString("Ошибка сети: %1").arg(reply->errorString()), 0);
        reply->deleteLater();
    });
}

void ApiClient::getUserList(const QString &token)
{
    if (token.isEmpty()) {
        qWarning() << "ApiClient::getUserList: Пустой токен.";
        emit userListFailed("Внутренняя ошибка: отсутствует токен.", 0);
        return;
    }
    QUrl listUrl = buildUrl("user_list.php");
    QNetworkRequest request(listUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    QUrlQuery postData;
    postData.addQueryItem("token_api", token);

    qDebug() << "ApiClient: Запрос POST списка пользователей на" << listUrl.toString();
    QNetworkReply *reply = networkManager->post(request, postData.toString(QUrl::FullyEncoded).toUtf8());

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QByteArray responseData = reply->readAll();
            if (statusCode == 200) {
                QJsonDocument doc = QJsonDocument::fromJson(responseData);
                if (!doc.isNull() && doc.isObject()) {
                    QJsonObject obj = doc.object();
                    if (obj.value("status").toString() == "success" && obj.contains("users") && obj["users"].isArray()) {
                        QList<UserData> userList;
                        QJsonArray usersArray = obj["users"].toArray();
                        for (const QJsonValue &val : usersArray) {
                            if (val.isObject()) {
                                QJsonObject userObj = val.toObject();
                                UserData user;
                                user.id = userObj.value("id").toString();
                                user.username = userObj.value("username").toString();
                                if (!user.id.isEmpty()) { // Простая валидация
                                    userList.append(user);
                                }
                            }
                        }
                        emit userListSuccess(userList);
                    } else {
                        emit userListFailed("Неверный формат ответа от сервера.", statusCode);
                    }
                } else { /* Ошибка парсинга JSON */ emit userListFailed("Ошибка парсинга JSON.", statusCode); }
            } else { /* Ошибка HTTP */ emit userListFailed("Ошибка сервера: " + QString::fromUtf8(responseData), statusCode); }
        } else { /* Ошибка сети */ emit userListFailed(reply->errorString(), 0); }
        reply->deleteLater();
    });
    connect(reply, &QNetworkReply::errorOccurred, this, [this, reply](QNetworkReply::NetworkError code){/*...*/});
}

void ApiClient::deleteUser(const QString &token, const QString &userId)
{
    if (token.isEmpty() || userId.isEmpty()) {
        qWarning() << "ApiClient::deleteUser: Пустой токен или ID пользователя.";
        emit deleteUserFailed(userId, "Внутренняя ошибка: отсутствует токен или ID пользователя.", 0);
        return;
    }
    QUrl deleteUrl = buildUrl("delete_user.php");
    QNetworkRequest request(deleteUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    QUrlQuery postData;
    postData.addQueryItem("token_api", token);
    postData.addQueryItem("user_id", userId);

    qDebug() << "ApiClient: Запрос POST на удаление пользователя ID:" << userId;
    QNetworkReply *reply = networkManager->post(request, postData.toString(QUrl::FullyEncoded).toUtf8());

    connect(reply, &QNetworkReply::finished, this, [this, reply, userId]() {
        qDebug() << "ApiClient: Ответ на удаление пользователя ID:" << userId << "получен.";
        if (reply->error() == QNetworkReply::NoError) {
            int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QByteArray responseData = reply->readAll();
            qDebug() << "ApiClient: Статус код (удаление user):" << statusCode << "Тело:" << responseData;
            if (statusCode == 200) {
                emit deleteUserSuccess(userId);
            } else {
                QString errorMsg = parseErrorMessage(responseData, "Ошибка удаления пользователя");
                emit deleteUserFailed(userId, errorMsg, statusCode);
            }
        } else { /* Сетевая ошибка */ emit deleteUserFailed(userId, reply->errorString(), 0); }
        reply->deleteLater();
    });
    connect(reply, &QNetworkReply::errorOccurred, this, [this, reply, userId](QNetworkReply::NetworkError code) {
        if(code == QNetworkReply::NoError) return;
        qWarning() << "ApiClient: Сетевая ошибка при удалении пользователя ID:" << userId << code << reply->errorString();
        if (!reply) return;
        emit deleteUserFailed(userId, reply->errorString(), 0);
        reply->deleteLater();
    });
}


void ApiClient::changeUserPassword(const QString &token, const QString &userId, const QString &newPassword)
{
    if (token.isEmpty() || userId.isEmpty() || newPassword.isEmpty()) {
        qWarning() << "ApiClient::changeUserPassword: Пустой токен, ID пользователя или пароль.";
        emit changePasswordFailed(userId, "Внутренняя ошибка: не все данные предоставлены.", 0);
        return;
    }
    QUrl changePassUrl = buildUrl("change_password.php");
    QNetworkRequest request(changePassUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    QUrlQuery postData;
    postData.addQueryItem("token_api", token);
    postData.addQueryItem("user_id", userId);
    postData.addQueryItem("password", newPassword);

    qDebug() << "ApiClient: Запрос POST на смену пароля для пользователя ID:" << userId;
    QNetworkReply *reply = networkManager->post(request, postData.toString(QUrl::FullyEncoded).toUtf8());

    connect(reply, &QNetworkReply::finished, this, [this, reply, userId]() {
        qDebug() << "ApiClient: Ответ на смену пароля для ID:" << userId << "получен.";
        if (reply->error() == QNetworkReply::NoError) {
            int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QByteArray responseData = reply->readAll();
            qDebug() << "ApiClient: Статус код (смена пароля):" << statusCode << "Тело:" << responseData;
            if (statusCode == 200) { // Считаем 200 успехом
                emit changePasswordSuccess(userId);
            } else {
                QString errorMsg = parseErrorMessage(responseData, "Ошибка смены пароля");
                emit changePasswordFailed(userId, errorMsg, statusCode);
            }
        } else { /* Сетевая ошибка */ emit changePasswordFailed(userId, reply->errorString(), 0); }
        reply->deleteLater();
    });
    connect(reply, &QNetworkReply::errorOccurred, this, [this, reply, userId](QNetworkReply::NetworkError code) {
        if(code == QNetworkReply::NoError) return;
        qWarning() << "ApiClient: Сетевая ошибка при смене пароля для ID:" << userId << code << reply->errorString();
        if (!reply) return;
        emit changePasswordFailed(userId, reply->errorString(), 0);
        reply->deleteLater();
    });
}


void ApiClient::createNewUser(const QString &token, const QString &username, const QString &password)
{
    if (token.isEmpty() || username.isEmpty() || password.isEmpty()) {
        qWarning() << "ApiClient::createNewUser: Пустой токен, имя пользователя или пароль.";
        emit createUserFailed(username, "Внутренняя ошибка: не все данные предоставлены.", 0);
        return;
    }
    QUrl createUserUrl = buildUrl("new_user.php");
    QNetworkRequest request(createUserUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    QUrlQuery postData;
    postData.addQueryItem("token_api", token);
    postData.addQueryItem("username", username);
    postData.addQueryItem("password", password);

    qDebug() << "ApiClient: Запрос POST на создание пользователя:" << username;
    QNetworkReply *reply = networkManager->post(request, postData.toString(QUrl::FullyEncoded).toUtf8());

    connect(reply, &QNetworkReply::finished, this, [this, reply, username, token]() {
        // -----------------------------------------------------------------
        qDebug() << "ApiClient: Ответ на создание пользователя" << username << "получен.";
        if (reply->error() == QNetworkReply::NoError) {
            int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QByteArray responseData = reply->readAll();
            qDebug() << "ApiClient: Статус код (создание user):" << statusCode << "Тело:" << responseData;

            if (statusCode == 200) {
                QJsonDocument doc = QJsonDocument::fromJson(responseData);
                if (!doc.isNull() && doc.isObject()) {
                    QJsonObject obj = doc.object();
                    // Проверяем статус успеха от API
                    if (obj.value("status").toString() == "success") {
                        qDebug() << "ApiClient: Пользователь" << username << "успешно создан. Запрашиваем обновленный список.";
                        this->getUserList(token); // Теперь 'token' доступен здесь
                    } else {
                        QString errMsg = parseErrorMessage(responseData, "Ошибка создания пользователя"); // Используем парсер
                        emit createUserFailed(username, errMsg, statusCode);
                    }
                } else { /* Ошибка парсинга JSON */
                    emit createUserFailed(username, "Ошибка парсинга JSON ответа.", statusCode);
                }
            } else { /* Ошибка HTTP (не 200) */
                QString errorMsg = parseErrorMessage(responseData, "Ошибка создания пользователя");
                emit createUserFailed(username, errorMsg, statusCode);
            }
        } else { /* Сетевая ошибка */
            emit createUserFailed(username, reply->errorString(), 0);
        }
        reply->deleteLater();
    });

    connect(reply, &QNetworkReply::errorOccurred, this, [this, reply, username](QNetworkReply::NetworkError code) {
        if(code == QNetworkReply::NoError) return;
        qWarning() << "ApiClient: Сетевая ошибка при создании пользователя" << username << code << reply->errorString();
        if (!reply) return;
        emit createUserFailed(username, reply->errorString(), 0);
        reply->deleteLater();
    });
}


void ApiClient::triggerBackup(const QString &token)
{
    if (token.isEmpty()) {
        qWarning() << "ApiClient::triggerBackup: Пустой токен.";
        emit backupFailed("Внутренняя ошибка: отсутствует токен.", 0);
        return;
    }
    QUrl backupUrl = buildUrl("make_backup.php");
    QNetworkRequest request(backupUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    QUrlQuery postData;
    postData.addQueryItem("token_api", token);

    qDebug() << "ApiClient: Запрос POST на запуск бэкапа.";
    QNetworkReply *reply = networkManager->post(request, postData.toString(QUrl::FullyEncoded).toUtf8());

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        qDebug() << "ApiClient: Ответ на запуск бэкапа получен.";
        if (reply->error() == QNetworkReply::NoError) {
            int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QByteArray responseData = reply->readAll();
            qDebug() << "ApiClient: Статус код (бэкап):" << statusCode << "Тело:" << responseData;
            if (statusCode == 200) { // Считаем 200 OK успехом
                // Попытаемся извлечь сообщение из ответа
                QString message = "";
                QJsonDocument doc = QJsonDocument::fromJson(responseData);
                if (!doc.isNull() && doc.isObject()) {
                    if (doc.object().contains("message")) {
                        message = doc.object()["message"].toString();
                    } else if (doc.object().contains("status")) { // Если вдруг вернется status
                        message = doc.object()["status"].toString();
                    }
                }
                if (message.isEmpty() && !responseData.isEmpty() && responseData.size() < 100) { // Если просто текст
                    message = QString::fromUtf8(responseData);
                }
                emit backupSuccess(message);
            } else {
                QString errorMsg = parseErrorMessage(responseData, "Ошибка запуска резервного копирования");
                emit backupFailed(errorMsg, statusCode);
            }
        } else { /* Сетевая ошибка */ emit backupFailed(reply->errorString(), 0); }
        reply->deleteLater();
    });
    connect(reply, &QNetworkReply::errorOccurred, this, [this, reply](QNetworkReply::NetworkError code) {
        if(code == QNetworkReply::NoError) return;
        qWarning() << "ApiClient: Сетевая ошибка при запуске бэкапа" << code << reply->errorString();
        if (!reply) return;
        emit backupFailed(reply->errorString(), 0);
        reply->deleteLater();
    });
}
