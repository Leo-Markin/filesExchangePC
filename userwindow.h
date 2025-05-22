#ifndef USERWINDOW_H
#define USERWINDOW_H

#include <QWidget> // или QMainWindow
#include <QList>
#include "datatypes.h" // Наша структура FileInfo

// Прямые объявления для уменьшения зависимостей в заголовке
namespace Ui { class UserWindow; }
class ApiClient;
class QTableWidgetItem;
class QProgressBar;
class FileDetailsWindow;

class UserWindow : public QWidget // или QMainWindow
{
    Q_OBJECT

public:
    // Конструктор принимает токен, указатель на ApiClient и родителя
    explicit UserWindow(const QString &token, ApiClient *client, QWidget *parent = nullptr);
    virtual ~UserWindow();
    void setupUserInterface();

protected slots:
    // Слоты для connect в setupUserInterface и для наследников
    void on_searchLineEdit_textChanged(const QString &text);
    void on_uploadButton_clicked();

    // Приватные слоты для сигналов API
private slots:
    void handleFilesSuccess(const QList<FileInfo> &files);
    void handleFilesFailed(const QString &errorString, int statusCode);
    void handleUploadSuccess();
    void handleUploadFailed(const QString &errorString, int statusCode);
    void handleUploadProgress(qint64 bytesSent, qint64 bytesTotal);
    void copyFileLink();
    void viewFileDetails();
    void deleteFileClicked();
    void handleDeleteSuccess(const QString &deletedFileId);
    void handleDeleteFailed(const QString &failedFileId, const QString &errorString, int statusCode);

protected:
    Ui::UserWindow *ui;
    QString apiToken;       // Храним токен для возможных будущих запросов из этого окна
    ApiClient *apiClient;   // Используем переданный экземпляр клиента
    QList<FileInfo> allFiles; // Полный список файлов для фильтрации
    void requestUserFiles(); // Метод для инициирования запроса файлов
    void setupTable();       // Настройка таблицы (заголовки, колонки)
    virtual void populateTable(const QList<FileInfo> &filesToDisplay); // Заполнение таблицы данными
    void setUploadingState(bool uploading); // Вспомогательный метод для блокировки/разблокировки UI
};

#endif // USERWINDOW_H
