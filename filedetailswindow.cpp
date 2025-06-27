#include "filedetailswindow.h"
#include "ui_filedetailswindow.h"
#include "apiclient.h"

#include <QJsonObject>
#include <QMessageBox>
#include <QDebug>
#include <QFileDialog>
#include <QFile>
#include <QProgressBar>

FileDetailsWindow::FileDetailsWindow(const QString &token, const QString &urlIdentifier, const QString &fileId, ApiClient *client, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::FileDetailsWindow),
    apiToken(token),
    fileUrlIdentifier(urlIdentifier), // Инициализируем идентификатор URL
    currentFileId(fileId),
    currentFileName(""),
    apiClient(client),
    downloadProgressBar(nullptr)
{
    ui->setupUi(this);

    // --- Получаем указатель на ProgressBar ---
    downloadProgressBar = ui->downloadProgressBar; // objectName = downloadProgressBar
    if(downloadProgressBar) {
        downloadProgressBar->setVisible(false);
        downloadProgressBar->setValue(0);
        downloadProgressBar->setTextVisible(true);
        downloadProgressBar->setFormat("Скачивание: %p%");
    }
    // ---------------------------------------

    if (!apiClient) {
        QMessageBox::critical(this, "Ошибка инициализации", "Не удалось инициализировать API клиент.");
        setFieldsEnabled(false);
        ui->fileNameLabel->setText("Ошибка: нет API клиента");
        ui->downloadButton->setEnabled(false); // Отключаем кнопку
        return;
    }

    setAttribute(Qt::WA_DeleteOnClose);

    // --- Подключаем сигналы ---
    // getFileInfo
    connect(apiClient, &ApiClient::fileInfoSuccess, this, &FileDetailsWindow::handleInfoSuccess);
    connect(apiClient, &ApiClient::fileInfoFailed, this, &FileDetailsWindow::handleInfoFailed);
    // downloadFile
    connect(apiClient, &ApiClient::downloadSuccess, this, &FileDetailsWindow::handleDownloadSuccess);
    connect(apiClient, &ApiClient::downloadProgress, this, &FileDetailsWindow::handleDownloadProgress);
    connect(apiClient, &ApiClient::downloadFailed, this, &FileDetailsWindow::handleDownloadFailed);
    // ------------------------

    setFieldsEnabled(false); // Блокируем поля на время загрузки инфо
    requestFileInfo();
}

FileDetailsWindow::~FileDetailsWindow()
{
    if (apiClient) {
        disconnect(apiClient, &ApiClient::fileInfoSuccess, this, &FileDetailsWindow::handleInfoSuccess);
        disconnect(apiClient, &ApiClient::fileInfoFailed, this, &FileDetailsWindow::handleInfoFailed);
        disconnect(apiClient, &ApiClient::downloadSuccess, this, &FileDetailsWindow::handleDownloadSuccess);
        disconnect(apiClient, &ApiClient::downloadProgress, this, &FileDetailsWindow::handleDownloadProgress);
        disconnect(apiClient, &ApiClient::downloadFailed, this, &FileDetailsWindow::handleDownloadFailed);
    }
    delete ui;
    qDebug() << "FileDetailsWindow уничтожен.";
}

// Включение/выключение полей и кнопки Скачать
void FileDetailsWindow::setFieldsEnabled(bool enabled)
{
    if(ui->groupBox) {
        ui->groupBox->setEnabled(enabled);
    }
    // Кнопка скачивания активна только если есть имя файла
    ui->downloadButton->setEnabled(enabled && !currentFileName.isEmpty());
}

// Вспомогательный метод для управления UI во время скачивания
void FileDetailsWindow::setDownloadingState(bool downloading)
{
    // Блокируем кнопку скачивания и показываем/скрываем прогресс
    ui->downloadButton->setEnabled(!downloading && !currentFileName.isEmpty()); // Перепроверяем имя файла
    if(downloadProgressBar) {
        downloadProgressBar->setVisible(downloading);
        if (!downloading) {
            downloadProgressBar->setValue(0); // Сброс
        }
    }
}


// Запрос информации о файле
void FileDetailsWindow::requestFileInfo()
{
    if (apiClient && !apiToken.isEmpty() && !fileUrlIdentifier.isEmpty()) {
        qDebug() << "FileDetailsWindow: Запрос информации для URL ID:" << fileUrlIdentifier; // Исправлен лог
        ui->fileNameLabel->setText("Загрузка данных...");
        // Передаем идентификатор URL в getFileInfo
        apiClient->getFileInfo(apiToken, fileUrlIdentifier);
    } else {
        qDebug() << "FileDetailsWindow: Недостаточно данных для запроса информации.";
        handleInfoFailed("Внутренняя ошибка: нет токена или идентификатора URL файла.", 0);
    }
    // ----------------------------------------------------
}

// Обработка успешного ответа с информацией о файле
void FileDetailsWindow::handleInfoSuccess(const QJsonObject &fileData)
{
    QString responseFileId = fileData.value("id").toString(); // Предполагаем, что API возвращает 'id'
    if (!responseFileId.isEmpty() && responseFileId != currentFileId) {
        qWarning() << "FileDetailsWindow: Получен ответ fileInfoSuccess для другого файла!"
                   << "Ожидали ID:" << currentFileId << ", получили ID:" << responseFileId
                   << "для URL ID:" << fileUrlIdentifier << ". Игнорируем.";
        return; // Просто игнорируем этот ответ
    }
    // ----------------------------------------------------------------------------------

    qDebug() << "FileDetailsWindow: Получена информация о файле ID:" << currentFileId << "(запрошено по URL ID:" << fileUrlIdentifier << ")" << fileData;

    currentFileName = fileData.value("file_name").toString("Неизвестное имя");
    ui->fileNameLabel->setText(currentFileName);
    ui->fileSizeLabel->setText(fileData.value("file_size").toString());
    ui->ownerNameLabel->setText(fileData.value("owner_name").toString());
    ui->uploadDateLabel->setText(fileData.value("upload_date").toString());
    ui->countViewsLabel->setText(fileData.value("count_views").toString());
    ui->countDownloadsLabel->setText(fileData.value("count_downloads").toString());

    this->setWindowTitle(QString("Информация: %1").arg(currentFileName));

    setFieldsEnabled(true);
}

// Обработка ошибки при получении информации
void FileDetailsWindow::handleInfoFailed(const QString &errorString, int statusCode)
{

    qWarning() << "FileDetailsWindow: Ошибка получения информации для ID:" << currentFileId << "Статус:" << statusCode << "Ошибка:" << errorString;
    setFieldsEnabled(false);
    ui->fileNameLabel->setText("Ошибка загрузки данных");
    QMessageBox::warning(this, "Ошибка загрузки", errorString);
}

// Нажатие кнопки "Скачать"
void FileDetailsWindow::on_downloadButton_clicked()
{
    if (!apiClient || apiToken.isEmpty() || currentFileId.isEmpty()) { // Используем currentFileId
        QMessageBox::critical(this, "Ошибка", "Невозможно начать скачивание: отсутствует токен или ID файла.");
        return;
    }
    qDebug() << "FileDetailsWindow: Нажата кнопка 'Скачать' для файла ID:" << currentFileId << "Имя:" << currentFileName; // Лог использует currentFileId
    setDownloadingState(true);
    if(downloadProgressBar) downloadProgressBar->setFormat("Скачивание: %p%");
    // Передаем ID файла в downloadFile
    apiClient->downloadFile(apiToken, currentFileId, currentFileName);
}


// Обработка успешного скачивания
void FileDetailsWindow::handleDownloadSuccess(const QString &requestedFileId, const QByteArray &fileData, const QString &originalFileName)
{
    // --- Проверка, для того ли файла пришел ответ ---
    if (requestedFileId != currentFileId) {
        qDebug() << "FileDetailsWindow: Получен ответ downloadSuccess для другого файла (" << requestedFileId << "), игнорируем.";
        return;
    }
    // ----------------------------------------------

    qDebug() << "FileDetailsWindow: Скачивание для ID:" << currentFileId << "завершено успешно.";
    setDownloadingState(false); // Разблокируем кнопку, скрываем прогресс

    // Запрашиваем у пользователя путь для сохранения
    QString savePath = QFileDialog::getSaveFileName(this,
                                                    "Сохранить файл",
                                                    QDir::homePath() + "/" + originalFileName, // Предлагаем имя файла в домашней директории
                                                    "Все файлы (*.*)");

    if (savePath.isEmpty()) {
        qDebug() << "FileDetailsWindow: Сохранение файла отменено пользователем.";
        QMessageBox::information(this, "Скачивание отменено", "Вы отменили сохранение файла.");
        return;
    }

    // Пытаемся записать данные в файл
    QFile file(savePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "FileDetailsWindow: Не удалось открыть файл для записи:" << savePath << file.errorString();
        QMessageBox::critical(this, "Ошибка сохранения", QString("Не удалось сохранить файл '%1':\n%2").arg(QFileInfo(savePath).fileName()).arg(file.errorString()));
        return;
    }

    qint64 bytesWritten = file.write(fileData);
    file.close(); // Закрываем файл

    if (bytesWritten == fileData.size()) {
        qDebug() << "FileDetailsWindow: Файл успешно сохранен как" << savePath;
        QMessageBox::information(this, "Файл сохранен", QString("Файл '%1' успешно сохранен.").arg(QFileInfo(savePath).fileName()));
    } else {
        qWarning() << "FileDetailsWindow: Ошибка записи файла. Записано" << bytesWritten << "из" << fileData.size();
        QMessageBox::critical(this, "Ошибка сохранения", QString("Произошла ошибка при записи файла '%1'.").arg(QFileInfo(savePath).fileName()));
        // Возможно, стоит удалить частично записанный файл
        // file.remove();
    }
}

// Обработка ошибки скачивания
void FileDetailsWindow::handleDownloadFailed(const QString &requestedFileId, const QString &errorString, int statusCode)
{
    // --- Проверка, для того ли файла пришел ответ ---
    if (requestedFileId != currentFileId) {
        qDebug() << "FileDetailsWindow: Получен ответ downloadFailed для другого файла (" << requestedFileId << "), игнорируем.";
        return;
    }
    // ----------------------------------------------

    qWarning() << "FileDetailsWindow: Ошибка скачивания файла ID:" << currentFileId << "Статус:" << statusCode << "Ошибка:" << errorString;
    setDownloadingState(false); // Разблокируем кнопку, скрываем прогресс
    QMessageBox::critical(this, "Ошибка скачивания", errorString);
}

// Обновление прогресс-бара скачивания
void FileDetailsWindow::handleDownloadProgress(const QString &requestedFileId, qint64 bytesReceived, qint64 bytesTotal)
{
    // --- Проверка, для того ли файла пришел ответ ---
    if (requestedFileId != currentFileId) {
        return;
    }
    // ----------------------------------------------

    if (downloadProgressBar && bytesTotal > 0) {
        int percent = static_cast<int>((static_cast<double>(bytesReceived) / static_cast<double>(bytesTotal)) * 100.0);
        downloadProgressBar->setValue(percent);
        downloadProgressBar->setFormat(QString("Скачивание: %1 / %2 (%p%)")
                                           .arg(QLocale::system().formattedDataSize(bytesReceived)) // Форматирование байт
                                           .arg(QLocale::system().formattedDataSize(bytesTotal)));
    } else if (downloadProgressBar) {
        downloadProgressBar->setFormat(QString("Скачивание: %1").arg(QLocale::system().formattedDataSize(bytesReceived)));
        downloadProgressBar->setMaximum(0);
        downloadProgressBar->setMinimum(0);
    }
}
