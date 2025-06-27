#ifndef FILEDETAILSWINDOW_H
#define FILEDETAILSWINDOW_H

#include <QDialog>
#include <QString>

namespace Ui { class FileDetailsWindow; }
class ApiClient;
class QJsonObject;
class QProgressBar;

class FileDetailsWindow : public QDialog
{
    Q_OBJECT

public:
    // Конструктор принимает токен, идентификатор файла из URL, ApiClient и родителя
    explicit FileDetailsWindow(const QString &token,
                               const QString &urlIdentifier, // Идентификатор для getFileInfo
                               const QString &fileId,        // ID для скачивания и проверки ответа
                               ApiClient *client,
                               QWidget *parent = nullptr);
    ~FileDetailsWindow();

private slots:
    // Слоты для обработки ответа от ApiClient
    void handleInfoSuccess(const QJsonObject &fileData);
    void handleInfoFailed(const QString &errorString, int statusCode);

    // Слот для кнопки скачивания
    void on_downloadButton_clicked();

    // Слоты для обработки скачивания
    void handleDownloadSuccess(const QString &requestedFileId, const QByteArray &fileData, const QString &originalFileName);
    void handleDownloadFailed(const QString &requestedFileId, const QString &errorString, int statusCode);
    void handleDownloadProgress(const QString &requestedFileId, qint64 bytesReceived, qint64 bytesTotal);

private:
    Ui::FileDetailsWindow *ui;
    QString apiToken;
    QString fileUrlIdentifier; // Идентификатор файла (последняя часть URL)
    QString currentFileId;      // ID файла, для которого открыто окно
    QString currentFileName;    // Имя файла, полученное из getFileInfo
    ApiClient *apiClient;
    QProgressBar *downloadProgressBar; // Указатель на прогресс бар

    void requestFileInfo(); // Запрос информации при открытии
    void setFieldsEnabled(bool enabled); // Вкл/выкл полей ввода
    void setDownloadingState(bool downloading); // Вспомогательный метод
};

#endif // FILEDETAILSWINDOW_H
