#include "menupopupanimation_p.h"

#include <QApplication>
#include <QCursor>
#include <QEasingCurve>
#include <QEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QRegion>
#include <QVariantAnimation>
#include <QtMath>

#include "fluentui3styleproperties.h"

class MenuSnapshotOverlay final : public QWidget
{
public:
    MenuSnapshotOverlay( const QPixmap& snapshot, QWidget* parent )
        : QWidget( parent )
        , m_snapshot( snapshot )
    {
        setAttribute( Qt::WA_TransparentForMouseEvents );
        setAttribute( Qt::WA_NoSystemBackground );
        setAttribute( Qt::WA_TranslucentBackground );
    }

protected:
    void paintEvent( QPaintEvent* ) override
    {
        QPainter painter( this );
        painter.drawPixmap( 0, 0, m_snapshot );
    }

private:
    QPixmap m_snapshot;
};

static bool menuPopupAnimationEnabled( const QMenu* menu )
{
    const QVariant globalValue = qApp->property( MenuPopupAnimationEnabledProperty );
    if ( globalValue.isValid() && !globalValue.toBool() )
    {
        return false;
    }

    if ( menu )
    {
        const QVariant localValue = menu->property( MenuPopupAnimationEnabledProperty );
        if ( localValue.isValid() )
        {
            return localValue.toBool();
        }
    }

    return true;
}

class MenuPopupAnimatorImpl final : public QObject
{
public:
    explicit MenuPopupAnimatorImpl( QMenu* menu, QObject* parent )
        : QObject( parent ? parent : menu )
        , m_menu( menu )
    {
        menu->installEventFilter( this );
        connect( menu,
                 &QMenu::aboutToShow,
                 this,
                 [ this ] { prepareForShow(); } );
    }

    ~MenuPopupAnimatorImpl() override { stop(); }

    void setDuration( int duration ) { m_duration = qMax( 0, duration ); }

    void stop()
    {
        if ( m_animation )
        {
            m_animation->stop();
            m_animation->deleteLater();
            m_animation = nullptr;
        }

        restoreMenu();
    }

protected:
    bool eventFilter( QObject* watched, QEvent* event ) override
    {
        if ( watched != m_menu )
        {
            return QObject::eventFilter( watched, event );
        }

        if ( event->type() == QEvent::Show )
        {
            if ( menuPopupAnimationEnabled( m_menu ) && m_menu->windowType() == Qt::Popup )
            {
                if ( !m_preparedForShow )
                {
                    prepareForShow();
                }

                QWidget* menuParent = m_menu->parentWidget();
                if ( menuParent && menuParent->inherits( "QMenuBar" ) )
                {
                    const bool opensAbove = m_menu->geometry().center().y() < QCursor::pos().y();
                    m_menu->move( m_menu->pos().x() + 3, m_menu->pos().y() + ( opensAbove ? -4 : 4 ) );
                }
                animate();
            }
        }
        else if ( event->type() == QEvent::Hide || event->type() == QEvent::Close )
        {
            // QMenu 收起时只恢复真实绘制，不播放动画。
            stop();
        }
        else if ( m_isOpening )
        {
            switch ( event->type() )
            {
                case QEvent::MouseButtonPress :
                case QEvent::MouseButtonRelease :
                case QEvent::MouseButtonDblClick :
                    return true;
                case QEvent::KeyPress :
                case QEvent::KeyRelease :
                {
                    const int key = static_cast<QKeyEvent*>( event )->key();
                    if ( key == Qt::Key_Escape || key == Qt::Key_Enter || key == Qt::Key_Return || key == Qt::Key_Space )
                    {
                        return true;
                    }
                    break;
                }
                default :
                    break;
            }
        }

        return QObject::eventFilter( watched, event );
    }

private:
    void prepareForShow()
    {
        stop();

        if ( !m_menu
             || !menuPopupAnimationEnabled( m_menu )
             || m_menu->windowType() != Qt::Popup )
        {
            return;
        }

        // QMenu::aboutToShow 发生在原生菜单窗口显示前。先禁止真实菜单
        // 绘制，避免 Qt 5 的按钮菜单先提交一帧完整内容。
        m_preparedForShow = true;
        m_menu->setProperty(
            MenuPopupSuppressPaintingProperty, true );
    }

    QPixmap renderMenuSnapshot()
    {
        if ( !m_menu || m_menu->size().isEmpty() )
        {
            return {};
        }

        const qreal ratio = m_menu->devicePixelRatioF();
        QPixmap snapshot(
            qCeil( m_menu->width() * ratio ),
            qCeil( m_menu->height() * ratio ) );
        snapshot.setDevicePixelRatio( ratio );
        snapshot.fill( Qt::transparent );

        // 临时允许 Style 绘制，但只渲染到离屏 QPixmap；这里不调用
        // QWidget::grab()，避免 Qt 5 将完整菜单刷新到真实 backing store。
        m_menu->setProperty(
            MenuPopupSuppressPaintingProperty, QVariant() );
        {
            QPainter painter( &snapshot );
            m_menu->render(
                &painter,
                QPoint(),
                QRegion( m_menu->rect() ),
                QWidget::DrawWindowBackground
                    | QWidget::DrawChildren );
        }
        m_menu->setProperty(
            MenuPopupSuppressPaintingProperty, true );
        return snapshot;
    }

    void animate()
    {
        if ( !m_menu || m_menu->height() <= 1 || m_menu->width() <= 0 )
        {
            restoreMenu();
            return;
        }

        const QPixmap snapshot = renderMenuSnapshot();
        if ( snapshot.isNull() )
        {
            restoreMenu();
            return;
        }

        const auto children = m_menu->findChildren<QWidget*>( QString(), Qt::FindDirectChildrenOnly );
        for ( QWidget* child : children )
        {
            if ( child->isVisible() )
            {
                m_hiddenChildren.append( child );
                child->hide();
            }
        }

        m_overlay = new MenuSnapshotOverlay( snapshot, m_menu );
        m_overlay->resize( m_menu->size() );

        const bool opensAbove = m_menu->geometry().center().y() < QCursor::pos().y();
        m_startSnapshotY      = opensAbove ? m_menu->height() : -m_menu->height();
        m_overlay->move( 0, m_startSnapshotY );
        // Qt 5 会在 show() 时立即提交子窗口首帧。必须先放到动画
        // 起点再显示，否则完整截图会在 (0, 0) 闪现一帧。
        m_overlay->show();
        m_overlay->raise();

        m_isOpening = true;
        m_preparedForShow = false;

        // 立即清掉 Qt 5 可能已经准备好的真实菜单 backing store。此时
        // overlay 位于裁剪区外，所以首帧保持透明。
        m_menu->repaint();

        m_animation = new QVariantAnimation( m_menu );
        m_animation->setStartValue( 0.0 );
        m_animation->setEndValue( 1.0 );
        m_animation->setDuration( m_duration );
        m_animation->setEasingCurve( QEasingCurve::OutCubic );

        connect( m_animation,
                 &QVariantAnimation::valueChanged,
                 this,
                 [ this ]( const QVariant& value )
                 {
                     if ( !m_menu || !m_overlay )
                     {
                         return;
                     }

                     const qreal progress = value.toReal();
                     const int snapshotY  = qRound( m_startSnapshotY * ( 1.0 - progress ) );
                     m_overlay->move( 0, snapshotY );
                 } );

        connect( m_animation,
                 &QVariantAnimation::finished,
                 this,
                 [ this ]
                 {
                     m_animation = nullptr;
                     restoreMenu();
                 } );

        m_animation->start( QAbstractAnimation::DeleteWhenStopped );
    }

    void restoreMenu()
    {
        m_isOpening      = false;
        m_preparedForShow = false;

        if ( m_menu )
        {
            m_menu->setProperty( MenuPopupSuppressPaintingProperty, QVariant() );
        }

        for ( const QPointer<QWidget>& child : m_hiddenChildren )
        {
            if ( child )
            {
                child->show();
            }
        }
        m_hiddenChildren.clear();

        if ( m_menu )
        {
            // overlay 仍覆盖在最上层时先准备好真实菜单的最终帧，
            // 再移除 overlay，避免 Qt 5 在动画结束处再闪一个空白帧。
            m_menu->repaint();
        }

        if ( m_overlay )
        {
            m_overlay->hide();
            m_overlay->deleteLater();
            m_overlay = nullptr;
        }

        if ( m_menu )
        {
            m_menu->update();
        }
    }

    QPointer<QMenu> m_menu;
    QPointer<MenuSnapshotOverlay> m_overlay;
    QPointer<QVariantAnimation> m_animation;
    QList<QPointer<QWidget>> m_hiddenChildren;
    int m_startSnapshotY = 0;
    int m_duration       = 400;
    bool m_isOpening     = false;
    bool m_preparedForShow = false;
};

MenuPopupAnimator::MenuPopupAnimator( QMenu* menu, QObject* parent )
    : QObject( parent ? parent : menu )
    , m_impl( new MenuPopupAnimatorImpl( menu, this ) )
{
}

MenuPopupAnimator::~MenuPopupAnimator() = default;

void MenuPopupAnimator::stop()
{
    m_impl->stop();
}

void MenuPopupAnimator::setDuration( int duration )
{
    m_impl->setDuration( duration );
}
