#include "userwindow.h"
#include "ui_userwindow.h"
#include "apiclient.h" // Нужен для вызова методов API и подключения сигналов
#include "filedetailswindow.h"

#include <QMessageBox>
#include <QDebug>
#include <QTableWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QFileDialog>
#include <QDir>
#include <QClipboard>   // Для копирования в буфер
#include <QApplication> // Для доступа к буферу обмена
#include <QToolTip>     // Для подсказки при копировании
#include <QHBoxLayout>  // Для кнопок в таблице

UserWindow::UserWindow(const QString &token, ApiClient *client, QWidget *parent) :
    QWidget(parent), // или QMainWindow(parent)
    ui(nullptr),
    apiToken(token),
    apiClient(client)
{

    // Общая инициализация для обоих случаев (UserWindow и AdminWindow)
    if (!apiClient) {
        QMessageBox::critical(this, "Ошибка инициализации", "Не удалось инициализировать API клиент.");
        qCritical() << "API Client не был предоставлен!";
        return; // Или throw?
    }

    connect(apiClient, &ApiClient::userFilesSuccess, this, &UserWindow::handleFilesSuccess, Qt::UniqueConnection);
    connect(apiClient, &ApiClient::userFilesFailed, this, &UserWindow::handleFilesFailed, Qt::UniqueConnection);
    connect(apiClient, &ApiClient::uploadSuccess, this, &UserWindow::handleUploadSuccess);
    connect(apiClient, &ApiClient::uploadProgress, this, &UserWindow::handleUploadProgress);
    connect(apiClient, &ApiClient::uploadFailed, this, &UserWindow::handleUploadFailed);
    connect(apiClient, &ApiClient::deleteSuccess, this, &UserWindow::handleDeleteSuccess);
    connect(apiClient, &ApiClient::deleteFailed, this, &UserWindow::handleDeleteFailed);

    requestUserFiles(); // Запрашиваем файлы при открытии окна
}

UserWindow::~UserWindow()
{
    // Отключаем сигналы, чтобы избежать вызова слотов после удаления объекта
    if (apiClient) {
        disconnect(apiClient, &ApiClient::userFilesSuccess, this, &UserWindow::handleFilesSuccess);
        disconnect(apiClient, &ApiClient::userFilesFailed, this, &UserWindow::handleFilesFailed);
        disconnect(apiClient, &ApiClient::uploadSuccess, this, &UserWindow::handleUploadSuccess);
        disconnect(apiClient, &ApiClient::uploadProgress, this, &UserWindow::handleUploadProgress);
        disconnect(apiClient, &ApiClient::uploadFailed, this, &UserWindow::handleUploadFailed);
        disconnect(apiClient, &ApiClient::deleteSuccess, this, &UserWindow::handleDeleteSuccess);
        disconnect(apiClient, &ApiClient::deleteFailed, this, &UserWindow::handleDeleteFailed);
    }
    delete ui;
    qDebug() << "UserWindow уничтожен.";
}

void UserWindow::setupUserInterface()
{
    if (ui) return; // Избегаем повторной настройки

    qDebug() << "Настройка UI для UserWindow...";
    ui = new Ui::UserWindow();
    ui->setupUi(this); // <<<=== SetupUi ТЕПЕРЬ ЗДЕСЬ

    // Находим ProgressBar
    QProgressBar *progressBar = this->findChild<QProgressBar*>("uploadProgressBar");
    if (progressBar) {
        progressBar->setVisible(false);
        progressBar->setValue(0);
        progressBar->setTextVisible(true);
        progressBar->setFormat("Загрузка: %p%");
    }

    // Подключаем кнопки и поля UserWindow UI
    QPushButton *uploadBtn = this->findChild<QPushButton*>("uploadButton");
    if(uploadBtn) {
        uploadBtn->setEnabled(true);
        uploadBtn->setText("Загрузить файл");
        // Используем this в connect, т.к. слот принадлежит этому объекту
        // connect(uploadBtn, &QPushButton::clicked, this, &UserWindow::on_uploadButton_clicked);
        qDebug() << "UserWindow UI: uploadButton подключен.";
    }
    QLineEdit *searchEdit = this->findChild<QLineEdit*>("searchLineEdit");
    if (searchEdit) {
        connect(searchEdit, &QLineEdit::textChanged, this, &UserWindow::on_searchLineEdit_textChanged);
        qDebug() << "UserWindow UI: searchLineEdit подключен.";
    }

    // Настраиваем таблицу UserWindow
    setupTable(); // setupTable теперь найдет виджеты, т.к. setupUi уже выполнен
}

// Вспомогательный метод для управления состоянием UI во время загрузки
void UserWindow::setUploadingState(bool uploading)
{
    QPushButton* btn = this->findChild<QPushButton*>("uploadButton");
    QTableWidget* table = this->findChild<QTableWidget*>("filesTableWidget");
    QLineEdit* search = this->findChild<QLineEdit*>("searchLineEdit");
    QProgressBar* progress = this->findChild<QProgressBar*>("uploadProgressBar");

    if(btn) btn->setEnabled(!uploading);
    if(table) table->setEnabled(!uploading);
    if(search) search->setEnabled(!uploading);
    if (progress) {
        progress->setVisible(uploading);
        if (!uploading) progress->setValue(0);
    }
}

// Настройка внешнего вида таблицы
void UserWindow::setupTable()
{
    QTableWidget* table = this->findChild<QTableWidget*>("filesTableWidget");
    if (!table) {
        qWarning() << "UserWindow::setupTable: Не удалось найти filesTableWidget!";
        return;
    }
    table->setColumnCount(5);
    table->setHorizontalHeaderLabels({"Имя файла", "Размер", "Дата загрузки", "Просмотры", "Действия"});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    // table->verticalHeader()->setVisible(false); // Раскомментировать, если нужно
    table->setRowCount(0);
}

// Инициирование запроса списка файлов
void UserWindow::requestUserFiles()
{
    if (!apiClient) return;
    // Блокируем UI, если он уже настроен
    QLineEdit* search = this->findChild<QLineEdit*>("searchLineEdit");
    QTableWidget* table = this->findChild<QTableWidget*>("filesTableWidget");
    if(search) search->setEnabled(false);
    if(table) table->setEnabled(false);
    qDebug() << "UserWindow: Запрос списка файлов...";
    apiClient->getUserFiles(apiToken);
}

// Обработка успешного получения списка файлов
void UserWindow::handleFilesSuccess(const QList<FileInfo> &files) {
    qDebug() << "UserWindow: Получен список из" << files.count() << "файлов.";
    allFiles = files;
    populateTable(allFiles);
    // Разблокировка UI
    QLineEdit* search = this->findChild<QLineEdit*>("searchLineEdit");
    QTableWidget* table = this->findChild<QTableWidget*>("filesTableWidget");
    if(search) search->setEnabled(true);
    if(table) table->setEnabled(true);
}

// Обработка ошибки при получении списка файлов
void UserWindow::handleFilesFailed(const QString &errorString, int statusCode) {
    qWarning() << "UserWindow: Ошибка получения файлов. Статус:" << statusCode << "Ошибка:" << errorString;
    QMessageBox::critical(this, "Ошибка загрузки файлов", errorString);
    // Разблокировка UI и очистка
    QLineEdit* search = this->findChild<QLineEdit*>("searchLineEdit");
    QTableWidget* table = this->findChild<QTableWidget*>("filesTableWidget");
    if(search) search->setEnabled(true);
    if(table) table->setEnabled(true);
    allFiles.clear();
    populateTable(allFiles);
}

// Заполнение таблицы данными
void UserWindow::populateTable(const QList<FileInfo> &filesToDisplay) {
    QTableWidget* table = this->findChild<QTableWidget*>("filesTableWidget");
    if (!table) {
        qWarning() << "UserWindow::populateTable: Не удалось найти filesTableWidget!";
        return;
    }
    table->setSortingEnabled(false);
    table->setRowCount(0);
    for (int row = 0; row < filesToDisplay.count(); ++row) {
        const FileInfo &file = filesToDisplay.at(row);
        table->insertRow(row);

        table->setItem(row, 0, new QTableWidgetItem(file.fileName));
        table->setItem(row, 1, new QTableWidgetItem(file.fileSize));
        table->setItem(row, 2, new QTableWidgetItem(file.uploadDate));
        table->setItem(row, 3, new QTableWidgetItem(file.countViews));

        // Кнопки действий
        QWidget *actionsWidget = new QWidget();
        QHBoxLayout *actionsLayout = new QHBoxLayout(actionsWidget);
        actionsLayout->setContentsMargins(2, 0, 2, 0);
        actionsLayout->setSpacing(3);

        QPushButton *detailsButton = new QPushButton("Детали");
        detailsButton->setToolTip("Просмотреть подробную информацию");
        QString urlIdentifier;
        if (!file.fileUrl.isEmpty()) {
            QStringList urlParts = file.fileUrl.split('/');
            if (!urlParts.isEmpty()) urlIdentifier = urlParts.last();
        }
        detailsButton->setProperty("fileId", file.id);
        if (!urlIdentifier.isEmpty()) { detailsButton->setProperty("urlIdentifier", urlIdentifier); }
        else { detailsButton->setEnabled(false); detailsButton->setToolTip("Нет URL ID"); }
        connect(detailsButton, &QPushButton::clicked, this, &UserWindow::viewFileDetails);

        QPushButton *copyButton = new QPushButton("Копировать");
        copyButton->setToolTip("Скопировать ссылку на файл");
        copyButton->setProperty("fileUrl", file.fileUrl);
        connect(copyButton, &QPushButton::clicked, this, &UserWindow::copyFileLink);

        QPushButton *deleteButton = new QPushButton("Удалить");
        deleteButton->setToolTip("Удалить файл с сервера");
        deleteButton->setStyleSheet("QPushButton { color: red; }");
        deleteButton->setProperty("fileId", file.id);
        deleteButton->setProperty("fileName", file.fileName);
        connect(deleteButton, &QPushButton::clicked, this, &UserWindow::deleteFileClicked);

        actionsLayout->addWidget(detailsButton);
        actionsLayout->addWidget(copyButton);
        actionsLayout->addWidget(deleteButton);
        actionsLayout->addStretch();
        actionsWidget->setLayout(actionsLayout);

        table->setCellWidget(row, 4, actionsWidget);
    }
    table->setSortingEnabled(true);
}

// Слот для фильтрации таблицы
void UserWindow::on_searchLineEdit_textChanged(const QString &text)
{
    QString filter = text.trimmed().toLower();
    if (filter.isEmpty()) {
        populateTable(allFiles);
    } else {
        QList<FileInfo> filteredFiles;
        for (const FileInfo &file : allFiles) {
            if (file.fileName.toLower().contains(filter)) {
                filteredFiles.append(file);
            }
        }
        populateTable(filteredFiles);
    }
}

// Слот для кнопки "Копировать ссылку"
void UserWindow::copyFileLink()
{
    // Получаем кнопку, которая вызвала сигнал
    QPushButton *button = qobject_cast<QPushButton*>(sender());
    if (button) {
        // Извлекаем URL из свойства кнопки
        QString url = button->property("fileUrl").toString();
        if (!url.isEmpty()) {
            QClipboard *clipboard = QApplication::clipboard();
            clipboard->setText(url);
            qDebug() << "UserWindow: Ссылка скопирована:" << url;
            // (Опционально) Показать уведомление пользователю
            //statusBar()->showMessage("Ссылка скопирована!", 2000); // Показать на 2 сек
            QToolTip::showText(button->mapToGlobal(QPoint(0,0)), "Ссылка скопирована!", button, button->rect(), 2000);

        } else {
            qWarning() << "UserWindow: Не удалось получить URL для копирования.";
        }
    }
}

// Слот для кнопки "Детали"
void UserWindow::viewFileDetails()
{
    QPushButton *button = qobject_cast<QPushButton*>(sender());
    if (!button) return;

    // --- ИЗМЕНЕНИЕ: Извлекаем ОБА идентификатора ---
    QString fileId = button->property("fileId").toString();
    QString urlIdentifier = button->property("urlIdentifier").toString(); // Извлекаем URL ID
    // -------------------------------------------

    if (fileId.isEmpty() || urlIdentifier.isEmpty()) { // Проверяем оба
        qWarning() << "UserWindow: Не удалось получить ID файла или URL идентификатор для просмотра деталей.";
        QMessageBox::warning(this, "Ошибка", "Не удалось определить идентификаторы файла.");
        return;
    }

    qDebug() << "UserWindow: Открытие окна деталей для файла ID:" << fileId << "URL ID:" << urlIdentifier;

    // --- ИЗМЕНЕНИЕ: Передаем оба идентификатора ---
    FileDetailsWindow *detailsWin = new FileDetailsWindow(apiToken, urlIdentifier, fileId, apiClient, this);
    // -------------------------------------------
    detailsWin->exec(); // Показываем модально
}

// Слот для кнопки "Загрузить"
void UserWindow::on_uploadButton_clicked()
{
    QString filePath = QFileDialog::getOpenFileName(this, "Выбрать файл для загрузки", QDir::homePath(), "Все файлы (*.*)");
    if (!filePath.isEmpty()) {
        qDebug() << "UserWindow: Выбран файл для загрузки:" << filePath;
        setUploadingState(true);
        QProgressBar* progress = this->findChild<QProgressBar*>("uploadProgressBar");
        if(progress) progress->setFormat("Загрузка: %p%");
        apiClient->uploadFile(apiToken, filePath);
    } else {
        qDebug() << "UserWindow: Выбор файла отменен.";
    }
}

// Слот обработки успешной загрузки
void UserWindow::handleUploadSuccess() {
    qDebug() << "UserWindow: Загрузка файла успешно завершена.";
    setUploadingState(false);
    QMessageBox::information(this, "Загрузка завершена", "Файл успешно загружен на сервер.");
    requestUserFiles();
}

// Слот обработки ошибки загрузки
void UserWindow::handleUploadFailed(const QString &errorString, int statusCode) {
    qWarning() << "UserWindow: Ошибка загрузки файла. Статус:" << statusCode << "Ошибка:" << errorString;
    setUploadingState(false);
    QMessageBox::critical(this, "Ошибка загрузки", errorString);
}

// Слот для обновления прогресс бара
void UserWindow::handleUploadProgress(qint64 bytesSent, qint64 bytesTotal) {
    QProgressBar* progress = this->findChild<QProgressBar*>("uploadProgressBar");
    if (progress && bytesTotal > 0) {
        int percent = static_cast<int>((static_cast<double>(bytesSent) / static_cast<double>(bytesTotal)) * 100.0);
        progress->setValue(percent);
    } else if (progress && bytesTotal <= 0) {
        progress->setMaximum(0); progress->setMinimum(0); progress->setValue(-1);
    }
}

// Слот для кнопки "Удалить" в строке таблицы
void UserWindow::deleteFileClicked()
{
    QPushButton *button = qobject_cast<QPushButton*>(sender());
    if (!button) return;

    QString fileId = button->property("fileId").toString();
    QString fileName = button->property("fileName").toString(); // Получаем имя файла

    if (fileId.isEmpty()) {
        qWarning() << "UserWindow: Не удалось получить ID файла для удаления.";
        QMessageBox::warning(this, "Ошибка", "Не удалось определить ID файла для удаления.");
        return;
    }

    // Запрашиваем подтверждение у пользователя
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this,
                                  "Подтверждение удаления",
                                  QString("Вы уверены, что хотите удалить файл '%1'?\nЭто действие необратимо!").arg(fileName),
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No); // Кнопка по умолчанию - "Нет"

    if (reply == QMessageBox::Yes) {
        qDebug() << "UserWindow: Запрос на удаление файла ID:" << fileId << "Имя:" << fileName;
        // (Опционально) Можно временно заблокировать кнопку или строку
        // button->setEnabled(false);
        // statusBar()->showMessage("Удаление файла...");
        apiClient->deleteFile(apiToken, fileId); // Вызываем метод API клиента
    } else {
        qDebug() << "UserWindow: Удаление файла ID:" << fileId << "отменено пользователем.";
    }
}

// Обработка успешного удаления
void UserWindow::handleDeleteSuccess(const QString &deletedFileId)
{
    qDebug() << "UserWindow: Файл с ID" << deletedFileId << "успешно удален.";
    // statusBar()->showMessage("Файл успешно удален.", 3000); // Показать сообщение на 3 сек
    QMessageBox::information(this, "Удаление завершено", "Файл успешно удален.");

    // Обновляем список файлов после успешного удаления
    requestUserFiles();
}

// Обработка ошибки при удалении
void UserWindow::handleDeleteFailed(const QString &failedFileId, const QString &errorString, int statusCode)
{
    qWarning() << "UserWindow: Ошибка удаления файла ID:" << failedFileId << "Статус:" << statusCode << "Ошибка:" << errorString;
    // statusBar()->clearMessage();
    QMessageBox::warning(this, "Ошибка удаления", errorString);
    // (Опционально) Разблокировать кнопку/строку, если блокировали
}
// ----------------------------------
