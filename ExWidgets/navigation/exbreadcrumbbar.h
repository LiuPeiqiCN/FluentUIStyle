#pragma once

#include "exwidgets_global.h"
#include <QAbstractItemDelegate>
#include <QPointer>
#include <QVariant>
#include <QWidget>

class QMenu;
class QToolButton;
class QStandardItemModel;

// 路径由调用方维护，点击只发送原始索引，不自动截断路径。
class EXWIDGETS_EXPORT ExBreadcrumbBar final : public QWidget
{
    Q_OBJECT
    Q_PROPERTY( QVariantList itemsSource READ itemsSource WRITE setItemsSource NOTIFY itemsSourceChanged )
    Q_PROPERTY( QAbstractItemDelegate* itemTemplate READ itemTemplate WRITE setItemTemplate NOTIFY itemTemplateChanged )

public:
    explicit ExBreadcrumbBar( QWidget* parent = nullptr );
    ~ExBreadcrumbBar() override;
    QVariantList itemsSource() const;
    void setItemsSource( const QVariantList& items );
    // Qt 委托对应 WinUI ItemTemplate；不接管所有权。
    // DisplayRole 是默认文本，UserRole 是 ItemsSource 中未经转换的原始数据。
    QAbstractItemDelegate* itemTemplate() const;
    void setItemTemplate( QAbstractItemDelegate* itemTemplate );
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

Q_SIGNALS:
    void itemsSourceChanged( const QVariantList& items );
    void itemTemplateChanged( QAbstractItemDelegate* itemTemplate );
    // 对应 WinUI ItemClickedEventArgs.Index / Item。
    void itemClicked( int index, const QVariant& item );

protected:
    void resizeEvent( QResizeEvent* event ) override;
    void changeEvent( QEvent* event ) override;
    void paintEvent( QPaintEvent* event ) override;
    bool eventFilter( QObject* watched, QEvent* event ) override;

private:
    void layoutItems();
    void rebuildItems();
    void activateItem( int index );
    QVariantList m_items;
    QStandardItemModel* m_model = nullptr;
    QPointer<QAbstractItemDelegate> m_itemTemplate;
    QMetaObject::Connection m_templateDestroyed;
    QMetaObject::Connection m_templateSizeChanged;
    QList<QToolButton*> m_buttons;
    QToolButton* m_overflowButton = nullptr;
    QMenu* m_overflowMenu = nullptr;
    QList<QRect> m_separators;
    int m_firstVisible = -1;
};
