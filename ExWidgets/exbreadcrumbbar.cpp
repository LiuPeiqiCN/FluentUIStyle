#include "exbreadcrumbbar.h"

#include <QAbstractItemDelegate>
#include <QAction>
#include <QApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPersistentModelIndex>
#include <QStandardItemModel>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QToolButton>
#include <QWidgetAction>

namespace
{
// WinUI BreadcrumbBarChevronFontSize=12，ChevronPadding=2,0。
constexpr int SeparatorWidth = 16;

class BreadcrumbButton final : public QToolButton
{
public:
    explicit BreadcrumbButton( QWidget* parent ) : QToolButton( parent )
    {
        setAutoRaise( true );
        setFocusPolicy( Qt::StrongFocus );
        setToolButtonStyle( Qt::ToolButtonTextOnly );
    }

    QPointer<QAbstractItemDelegate> itemTemplate;
    QPersistentModelIndex modelIndex;
    bool current = false;
    bool dropDown = false;

    QSize sizeHint() const override
    {
        QSize contentSize( fontMetrics().horizontalAdvance( text() ), qMax( 20, fontMetrics().height() ) );
        if ( itemTemplate && modelIndex.isValid() )
            contentSize = itemTemplate->sizeHint( viewOption(), modelIndex );
        return contentSize.expandedTo( QSize( 10, 20 ) ) + ( dropDown ? QSize( 22, 16 ) : QSize( 2, 6 ) );
    }

protected:
    void paintEvent( QPaintEvent* ) override
    {
        QPainter painter( this );
        painter.setRenderHint( QPainter::Antialiasing );
        auto option = viewOption();
        if ( dropDown && ( underMouse() || hasFocus() || isDown() ) )
        {
            QColor hover = palette().color( QPalette::WindowText );
            hover.setAlpha( isDown() ? 20 : 12 );
            painter.setPen( Qt::NoPen );
            painter.setBrush( hover );
            painter.drawRoundedRect( rect().adjusted( 1, 1, -1, -1 ), 4, 4 );
        }
        if ( itemTemplate && modelIndex.isValid() )
            itemTemplate->paint( &painter, option, modelIndex );
        else
        {
            QColor color = palette().color( isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::WindowText );
            // 行内项保持透明背景，只改变文字颜色；末项不是带高亮底色的按钮。
            if ( isEnabled() && !current && !dropDown )
                color.setAlphaF( color.alphaF() * ( isDown() ? 0.45 : underMouse() ? 0.70 : 1.0 ) );
            painter.setPen( color );
            painter.drawText( option.rect, Qt::AlignVCenter | Qt::AlignLeading,
                              fontMetrics().elidedText( text(), Qt::ElideRight, option.rect.width() ) );
        }
        if ( hasFocus() )
        {
            QStyleOptionFocusRect focus;
            focus.initFrom( this );
            focus.rect = rect().adjusted( 1, 1, -1, -1 );
            style()->drawPrimitive( QStyle::PE_FrameFocusRect, &focus, &painter, this );
        }
    }

private:
    QStyleOptionViewItem viewOption() const
    {
        QStyleOptionViewItem option;
        option.initFrom( this );
        option.font = font();
        option.fontMetrics = fontMetrics();
        option.widget = this;
        option.rect = rect().adjusted( dropDown ? 11 : 1, dropDown ? 7 : 3,
                                       dropDown ? -11 : -1, dropDown ? -9 : -3 );
        option.textElideMode = Qt::ElideRight;
        option.displayAlignment = Qt::AlignVCenter | Qt::AlignLeading;
        return option;
    }
};
}

ExBreadcrumbBar::ExBreadcrumbBar( QWidget* parent ) : QWidget( parent )
{
    QFont itemFont = font();
    itemFont.setPixelSize( 14 );
    setFont( itemFont );
    setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
    m_model = new QStandardItemModel( this );
    m_overflowButton = new BreadcrumbButton( this );
    m_overflowButton->setText( QStringLiteral( "\u2026" ) );
    m_overflowButton->setAccessibleName( tr( "上级路径" ) );
    m_overflowButton->setToolTip( tr( "显示折叠的路径" ) );
    m_overflowButton->setPopupMode( QToolButton::InstantPopup );
    m_overflowMenu = new QMenu( m_overflowButton );
    m_overflowButton->setMenu( m_overflowMenu );
    m_overflowButton->installEventFilter( this );
    m_overflowButton->hide();
}

ExBreadcrumbBar::~ExBreadcrumbBar()
{
    disconnect( m_templateDestroyed );
    disconnect( m_templateSizeChanged );
}

QVariantList ExBreadcrumbBar::itemsSource() const { return m_items; }
void ExBreadcrumbBar::setItemsSource( const QVariantList& items )
{
    if ( items == m_items )
        return;
    m_items = items;
    rebuildItems();
    Q_EMIT itemsSourceChanged( m_items );
}

QAbstractItemDelegate* ExBreadcrumbBar::itemTemplate() const { return m_itemTemplate.data(); }
void ExBreadcrumbBar::setItemTemplate( QAbstractItemDelegate* itemTemplate )
{
    if ( itemTemplate == m_itemTemplate )
        return;
    disconnect( m_templateDestroyed );
    disconnect( m_templateSizeChanged );
    m_itemTemplate = itemTemplate;
    if ( itemTemplate )
    {
        m_templateDestroyed = connect( itemTemplate, &QObject::destroyed, this, [this]()
        {
            m_itemTemplate.clear();
            rebuildItems();
            Q_EMIT itemTemplateChanged( nullptr );
        } );
        m_templateSizeChanged = connect( itemTemplate, &QAbstractItemDelegate::sizeHintChanged, this, [this]()
        {
            layoutItems();
            updateGeometry();
        } );
    }
    rebuildItems();
    Q_EMIT itemTemplateChanged( itemTemplate );
}

void ExBreadcrumbBar::rebuildItems()
{
    QWidget* focused = QApplication::focusWidget();
    const bool hadFocus = focused && ( focused == this || isAncestorOf( focused ) );
    m_overflowMenu->hide();
    m_overflowMenu->clear();
    for ( auto* button : m_buttons )
    {
        button->hide();
        button->removeEventFilter( this );
        disconnect( button, nullptr, this, nullptr );
        // 点击回调允许同步替换路径；不要在按钮自身的事件处理中销毁它。
        button->deleteLater();
    }
    m_buttons.clear();
    m_model->clear();
    for ( int i = 0; i < m_items.size(); ++i )
    {
        auto* item = new QStandardItem( m_items.at( i ).toString() );
        item->setData( m_items.at( i ), Qt::UserRole );
        m_model->appendRow( item );
        auto* button = new BreadcrumbButton( this );
        button->modelIndex = m_model->index( i, 0 );
        button->itemTemplate = m_itemTemplate;
        button->current = i == m_items.size() - 1;
        button->setText( item->text() );
        button->setAccessibleName( item->text() );
        button->setToolTip( item->text() );
        button->installEventFilter( this );
        connect( button, &QToolButton::clicked, this, [this, i]() { activateItem( i ); } );
        m_buttons.append( button );
    }
    m_firstVisible = -1;
    layoutItems();
    if ( hadFocus && !m_buttons.isEmpty() )
        m_buttons.last()->setFocus( Qt::OtherFocusReason );
    updateGeometry();
}

void ExBreadcrumbBar::activateItem( int index )
{
    if ( index < 0 || index >= m_items.size() )
        return;
    const QVariant item = m_items.at( index );
    Q_EMIT itemClicked( index, item );
}

QSize ExBreadcrumbBar::sizeHint() const
{
    int w = 0;
    int h = qMax( 26, fontMetrics().height() + 6 );
    for ( const auto* button : m_buttons )
    {
        w += button->sizeHint().width();
        h = qMax( h, button->sizeHint().height() );
    }
    return QSize( w + qMax( 0, int( m_buttons.size() ) - 1 ) * SeparatorWidth, h );
}
QSize ExBreadcrumbBar::minimumSizeHint() const
{
    return QSize( m_items.isEmpty() ? 0 : ( m_items.size() == 1 ? 16 : 56 ), sizeHint().height() );
}

void ExBreadcrumbBar::layoutItems()
{
    if ( !m_overflowButton || !m_overflowMenu )
        return;
    m_separators.clear();
    const int count = int( m_buttons.size() );
    const int available = qMax( 0, contentsRect().width() );
    const int overflowWidth = qMax( 20, m_overflowButton->sizeHint().width() );
    int first = 0;
    // 对齐 BreadcrumbLayout：先测量全部项，再从末项向前寻找可完整保留的后缀。
    if ( count > 1 && sizeHint().width() > available )
    {
        first = count - 1;
        int used = overflowWidth + SeparatorWidth + m_buttons.last()->sizeHint().width();
        while ( first > 1 && used + SeparatorWidth + m_buttons.at( first - 1 )->sizeHint().width() <= available )
        {
            --first;
            used += SeparatorWidth + m_buttons.at( first )->sizeHint().width();
        }
    }
    if ( first != m_firstVisible )
    {
        m_overflowMenu->hide();
        m_overflowMenu->clear();
        // WinUI CloneEllipsisItemSource 按逆序展示：最近的上级最先出现。
        for ( int i = first - 1; i >= 0; --i )
        {
            QAction* action = nullptr;
            if ( m_itemTemplate )
            {
                auto* widgetAction = new QWidgetAction( m_overflowMenu );
                auto* button = new BreadcrumbButton( m_overflowMenu );
                button->dropDown = true;
                button->itemTemplate = m_itemTemplate;
                button->modelIndex = m_model->index( i, 0 );
                button->setText( m_items.at( i ).toString() );
                widgetAction->setDefaultWidget( button );
                m_overflowMenu->addAction( widgetAction );
                connect( button, &QToolButton::clicked, widgetAction, [this, widgetAction]()
                {
                    m_overflowMenu->hide();
                    widgetAction->trigger();
                } );
                action = widgetAction;
            }
            else
            {
                QString text = m_items.at( i ).toString();
                text.replace( QStringLiteral( "&" ), QStringLiteral( "&&" ) );
                action = m_overflowMenu->addAction( text );
            }
            connect( action, &QAction::triggered, this, [this, i]() { activateItem( i ); } );
        }
        m_firstVisible = first;
    }
    const bool overflow = first > 0;
    m_overflowButton->setVisible( overflow );
    int x = contentsRect().left();
    const int h = qMin( contentsRect().height(), sizeHint().height() );
    const int y = contentsRect().top() + ( contentsRect().height() - h ) / 2;
    const auto place = [this, y, h]( QWidget* widget, int left, int w )
    {
        widget->setGeometry( QStyle::visualRect( layoutDirection(), contentsRect(), QRect( left, y, qMax( 0, w ), h ) ) );
    };
    if ( overflow )
    {
        place( m_overflowButton, x, qMin( available, overflowWidth ) );
        x += overflowWidth;
    }
    bool restoreFocus = false;
    for ( int i = 0; i < count; ++i )
    {
        auto* button = m_buttons.at( i );
        restoreFocus |= i < first && button->hasFocus();
        button->setVisible( i >= first );
        if ( i < first )
            continue;
        if ( i > first || overflow )
        {
            m_separators.append( QStyle::visualRect( layoutDirection(), contentsRect(), QRect( x, y, SeparatorWidth, h ) ) );
            x += SeparatorWidth;
        }
        const int w = qMin( button->sizeHint().width(), qMax( 0, contentsRect().right() + 1 - x ) );
        place( button, x, w );
        x += w;
    }
    if ( restoreFocus )
        m_overflowButton->setFocus( Qt::OtherFocusReason );
    setFocusProxy( overflow ? m_overflowButton : ( count ? m_buttons.first() : nullptr ) );
    update();
}

void ExBreadcrumbBar::resizeEvent( QResizeEvent* event )
{
    QWidget::resizeEvent( event );
    layoutItems();
}
void ExBreadcrumbBar::changeEvent( QEvent* event )
{
    QWidget::changeEvent( event );
    if ( event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange
         || event->type() == QEvent::LayoutDirectionChange )
    {
        layoutItems();
        updateGeometry();
    }
}
void ExBreadcrumbBar::paintEvent( QPaintEvent* )
{
    QPainter painter( this );
    painter.setRenderHint( QPainter::Antialiasing );
    painter.setPen( QPen( palette().color( isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::WindowText ),
                         1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
    for ( const QRect& separator : m_separators )
    {
        const QPointF center = separator.center();
        const qreal direction = isRightToLeft() ? -1.0 : 1.0;
        QPainterPath chevron;
        chevron.moveTo( center + QPointF( -2 * direction, -4 ) );
        chevron.lineTo( center + QPointF( 2 * direction, 0 ) );
        chevron.lineTo( center + QPointF( -2 * direction, 4 ) );
        painter.drawPath( chevron );
    }
}

bool ExBreadcrumbBar::eventFilter( QObject* watched, QEvent* event )
{
    if ( event->type() == QEvent::KeyPress )
    {
        auto* key = static_cast<QKeyEvent*>( event );
        auto* button = qobject_cast<QToolButton*>( watched );
        if ( button && ( key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter ) )
        {
            if ( button == m_overflowButton )
                button->showMenu();
            else
                button->click();
            return true;
        }
        QList<QToolButton*> visible;
        if ( !m_overflowButton->isHidden() )
            visible.append( m_overflowButton );
        for ( auto* item : m_buttons )
            if ( !item->isHidden() )
                visible.append( item );
        const int index = visible.indexOf( button );
        if ( index >= 0 && key->modifiers() == Qt::NoModifier )
        {
            int next = index;
            switch ( key->key() )
            {
                case Qt::Key_Home: next = 0; break;
                case Qt::Key_End: next = visible.size() - 1; break;
                case Qt::Key_Left: next += isRightToLeft() ? 1 : -1; break;
                case Qt::Key_Right: next += isRightToLeft() ? -1 : 1; break;
                default: return QWidget::eventFilter( watched, event );
            }
            visible.at( qBound( 0, next, int( visible.size() ) - 1 ) )->setFocus( Qt::OtherFocusReason );
            return true;
        }
    }
    return QWidget::eventFilter( watched, event );
}
