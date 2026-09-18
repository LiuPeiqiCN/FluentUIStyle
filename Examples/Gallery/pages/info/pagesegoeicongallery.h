#pragma once

#include <QFrame>
#include <QList>
#include <QString>

class QLineEdit;
class QListView;
class QAbstractItemModel;
class QSortFilterProxyModel;

class PageSegoeIconGallery : public QFrame
{
    Q_OBJECT

public:
    struct IconEntry
    {
        QString name;
        int code;
    };

    explicit PageSegoeIconGallery(QWidget *parent = nullptr);

    static QList<IconEntry> iconEntries();

private:
    void initializeUi();

    QLineEdit *m_searchEdit{nullptr};
    QListView *m_listView{nullptr};
    QAbstractItemModel *m_model{nullptr};
    QSortFilterProxyModel *m_proxyModel{nullptr};
};
