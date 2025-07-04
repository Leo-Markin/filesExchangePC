// datatypes.h
#ifndef DATATYPES_H
#define DATATYPES_H

#include <QString>
#include <QMetaType> // Для Q_DECLARE_METATYPE

// Структура для хранения информации о файле
struct FileInfo {
    QString id;
    QString fileName;
    QString ownerName;
    QString fileSize;
    QString fileUrl;
    QString uploadDate;
    QString countViews;
};

// Регистрируем тип, чтобы его можно было использовать в QVariant
Q_DECLARE_METATYPE(FileInfo)

// Cтруктура для данных пользователя
struct UserData {
    QString id;
    QString username;
};
Q_DECLARE_METATYPE(UserData) // Регистрируем тип

#endif // DATATYPES_H
