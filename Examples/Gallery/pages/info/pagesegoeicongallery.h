#pragma once

#include <QFrame>
#include <QList>
#include <QString>

class QLineEdit;
class QTableWidget;

class PageSegoeIconGallery : public QFrame
{
    Q_OBJECT

public:
    explicit PageSegoeIconGallery(QWidget *parent = nullptr);

private:
    struct IconEntry
    {
        QString name;
        int code;
    };

    void initializeUi();
    void populateTable(const QString &keyword = QString());
    static QList<IconEntry> iconEntries();

    QLineEdit *m_searchEdit{nullptr};
    QTableWidget *m_tableWidget{nullptr};
};

