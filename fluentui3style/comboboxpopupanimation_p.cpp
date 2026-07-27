#include "comboboxpopupanimation_p.h"
#include "fluentui3styleproperties.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QBoxLayout>
#include <QBrush>
#include <QChildEvent>
#include <QComboBox>
#include <QEasingCurve>
#include <QEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QParallelAnimationGroup>
#include <QPixmap>
#include <QPointer>
#include <QPropertyAnimation>
#include <QScreen>
#include <QVariantAnimation>
#include <QWidget>
#include <QtMath>

// 收起时显示的临时截图窗口。它不接收输入，也不参与 QComboBox 的状态管理。
class PopupSnapshotWidget final : public QWidget
{
public:
    PopupSnapshotWidget( QWidget* owner,
                         const QPixmap& backgroundSnapshot,
                         const QPixmap& viewSnapshot,
                         const QPoint& viewPosition,
                         const QSize& viewSize,
                         const QBrush& viewBackground,
                         int cornerRadius,
                         int shadowBorderWidth,
                         bool opensAbove )
        : QWidget( owner,
                   Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput
                       | Qt::NoDropShadowWindowHint )
        , m_backgroundSnapshot( backgroundSnapshot )
        , m_viewSnapshot( viewSnapshot )
        , m_viewPosition( viewPosition )
        , m_viewSize( viewSize )
        , m_viewBackground( viewBackground )
        , m_cornerRadius( cornerRadius )
        , m_shadowBorderWidth( shadowBorderWidth )
        , m_opensAbove( opensAbove )
    {
        setAttribute( Qt::WA_TransparentForMouseEvents );
        setAttribute( Qt::WA_ShowWithoutActivating );
        setAttribute( Qt::WA_TranslucentBackground );
    }

    void setContentOffset( int y )
    {
        m_contentOffsetY = y;
        update();
    }

    void setViewSnapshot( const QPixmap& viewSnapshot, const QPoint& viewPosition, const QSize& viewSize, const QBrush& viewBackground )
    {
        m_viewSnapshot   = viewSnapshot;
        m_viewPosition   = viewPosition;
        m_viewSize       = viewSize;
        m_viewBackground = viewBackground;
        update();
    }

protected:
    void paintEvent( QPaintEvent* ) override
    {
        QPainter painter( this );
        painter.setRenderHint( QPainter::Antialiasing );

        const qreal radius = qMin<qreal>( m_cornerRadius, height() / 2.0 );
        QPainterPath clipPath;
        clipPath.addRoundedRect( QRectF( rect() ).adjusted( 0.5, 0.5, -0.5, -0.5 ), radius, radius );
        painter.setClipPath( clipPath );

        const QRectF contentRect =
            QRectF( rect() ).adjusted(
                m_shadowBorderWidth,
                m_opensAbove ? m_shadowBorderWidth : 0,
                -m_shadowBorderWidth,
                m_opensAbove ? 0 : -m_shadowBorderWidth );
        const qreal contentRadius =
            qMin<qreal>( m_cornerRadius, contentRect.height() / 2.0 );
        QPainterPath contentClip;
        contentClip.addRoundedRect( contentRect, contentRadius, contentRadius );

        painter.drawPixmap( 0, 0, m_backgroundSnapshot );

        // 一些样式把弹窗容器设为透明，真正的不透明背景由 view 绘制。
        // view 移走后只在容器实际面板区域内补上底色。
        if ( !m_viewSize.isEmpty() )
        {
            painter.save();
            painter.setClipPath( contentClip, Qt::IntersectClip );
            painter.fillRect( QRect( m_viewPosition, m_viewSize ), m_viewBackground );
            painter.restore();
        }

        if ( !m_viewSnapshot.isNull() )
        {
            painter.save();
            painter.setClipPath( contentClip, Qt::IntersectClip );
            painter.drawPixmap( m_viewPosition.x(), m_viewPosition.y() + m_contentOffsetY, m_viewSnapshot );
            painter.restore();
        }
    }

private:
    QPixmap m_backgroundSnapshot;
    QPixmap m_viewSnapshot;
    QPoint m_viewPosition;
    QSize m_viewSize;
    QBrush m_viewBackground;
    int m_cornerRadius      = 4;
    int m_shadowBorderWidth = 2;
    int m_contentOffsetY    = 0;
    bool m_opensAbove       = false;
};

static bool comboBoxPopupAnimationEnabled( const QComboBox* comboBox )
{
    if ( qApp->property( "_q_scrollHint_center" ).toBool() )
    {
        return false;
    }

    const QVariant globalValue =
        qApp->property( ComboBoxPopupAnimationEnabledProperty );
    if ( globalValue.isValid() && !globalValue.toBool() )
    {
        return false;
    }

    if ( comboBox )
    {
        const QVariant localValue =
            comboBox->property( ComboBoxPopupAnimationEnabledProperty );
        if ( localValue.isValid() )
        {
            return localValue.toBool();
        }
    }

    return true;
}

// 这个辅助对象只监听 QComboBox 创建的弹出窗口，不需要继承 QComboBox，
// 也不需要重写 showPopup() 或 hidePopup()。
class ComboBoxPopupAnimatorImpl final : public QObject
{
public:
    explicit ComboBoxPopupAnimatorImpl( QComboBox* comboBox, QObject* parent )
        : QObject( parent ? parent : comboBox )
        , m_comboBox( comboBox )
    {
        comboBox->installEventFilter( this );

        const auto children = comboBox->findChildren<QWidget*>(
            QString(), Qt::FindDirectChildrenOnly );
        for ( QWidget* child : children )
        {
            attachPopup( child );
        }
    }

    ~ComboBoxPopupAnimatorImpl() override
    {
        stopCloseSnapshot();
        removeApplicationEventFilter();
    }

    void setDuration( int duration ) { m_duration = duration; }

    void setCornerRadius( int radius ) { m_cornerRadius = qMax( 0, radius ); }

    void setPopupOffset( int offset ) { m_popupOffset = qMax( 0, offset ); }

    void setShadowBorderWidth( int width ) { m_shadowBorderWidth = qMax( 0, width ); }

    void stop()
    {
        if ( m_popup )
        {
            stopAnimation( m_popup );
        }
        stopCloseSnapshot();
        removeApplicationEventFilter();
    }

protected:
    bool eventFilter( QObject* watched, QEvent* event ) override
    {
        if ( !m_comboBox )
        {
            return QObject::eventFilter( watched, event );
        }

        if ( watched == m_comboBox && event->type() == QEvent::ChildPolished )
        {
            auto* childEvent = static_cast<QChildEvent*>( event );
            attachPopup( qobject_cast<QWidget*>( childEvent->child() ) );
            return QObject::eventFilter( watched, event );
        }

        if ( event->type() == QEvent::ApplicationDeactivate
             || ( watched == m_comboBox->window() && ( event->type() == QEvent::Hide || event->type() == QEvent::Close ) ) )
        {
            stopCloseSnapshot();
        }

        if ( !m_popup )
        {
            return QObject::eventFilter( watched, event );
        }

        if ( watched == m_popup && event->type() == QEvent::Show )
        {
            if ( comboBoxPopupAnimationEnabled( m_comboBox ) )
            {
                animatePopup( m_popup );
                installApplicationEventFilter();
            }
            return QObject::eventFilter( watched, event );
        }

        if ( !comboBoxPopupAnimationEnabled( m_comboBox ) )
        {
            stopAnimation( m_popup );
            stopCloseSnapshot();
            removeApplicationEventFilter();
            return QObject::eventFilter( watched, event );
        }

        QWidget* popup = m_popup;

        // 展开完成前吞掉后续输入，避免弹窗尚未准备好截图就关闭。
        if ( m_isOpening )
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

        // 展开完成后，只在可能触发关闭的输入到来时更新截图。
        // 这样既能得到最新的悬停/滚动状态，也不需要监听每一帧 Paint。
        if ( popup->isVisible() && !m_isOpening )
        {
            bool shouldRefresh = event->type() == QEvent::MouseButtonPress;

            if ( event->type() == QEvent::KeyPress )
            {
                const int key = static_cast<QKeyEvent*>( event )->key();
                shouldRefresh = key == Qt::Key_Escape || key == Qt::Key_Enter || key == Qt::Key_Return || key == Qt::Key_Space;
            }

            if ( shouldRefresh )
            {
                refreshCloseSnapshot( popup );
            }
        }

        if ( watched != popup )
        {
            return QObject::eventFilter( watched, event );
        }

        if ( event->type() == QEvent::Hide )
        {
            // 真实弹窗正常隐藏，使用它的截图播放视觉上的收起动画。
            const bool openingWasInterrupted = m_isOpening;
            stopAnimation( popup );
            if ( openingWasInterrupted )
            {
                stopCloseSnapshot();
            }
            else
            {
                animateCloseSnapshot( popup );
            }
            restoreHeightConstraints( popup );
        }

        return QObject::eventFilter( watched, event );
    }

private:
    void attachPopup( QWidget* popup )
    {
        if ( !popup
             || !popup->inherits( "QComboBoxPrivateContainer" )
             || ComboBoxPopupAnimator::comboBoxForPopup( popup ) != m_comboBox
             || m_popup == popup )
        {
            return;
        }

        if ( m_popup )
        {
            m_popup->removeEventFilter( this );
        }
        m_popup = popup;
        m_popup->installEventFilter( this );
    }

    void installApplicationEventFilter()
    {
        if ( !m_applicationFilterInstalled )
        {
            qApp->installEventFilter( this );
            m_applicationFilterInstalled = true;
        }
    }

    void removeApplicationEventFilter()
    {
        if ( m_applicationFilterInstalled )
        {
            qApp->removeEventFilter( this );
            m_applicationFilterInstalled = false;
        }
    }

    void animatePopup( QWidget* popup )
    {
        stopCloseSnapshot();
        stopAnimation( popup );

        auto* popupView   = m_comboBox->view();
        auto* popupLayout = popup->layout();
        auto* popupBoxLayout = qobject_cast<QBoxLayout*>( popupLayout );
        const int viewLayoutIndex = popupLayout ? popupLayout->indexOf( popupView ) : -1;
        if ( !popupView || !popupBoxLayout || viewLayoutIndex < 0 )
        {
            return;
        }

        m_isOpening              = true;
        m_finalGeometry          = popup->geometry();
        m_originalMinimumHeight  = popup->minimumHeight();
        m_originalMaximumHeight  = popup->maximumHeight();
        m_heightConstraintsSaved = true;

        const QPoint comboCenter = m_comboBox->mapToGlobal( m_comboBox->rect().center() );
        m_opensAbove             = m_finalGeometry.center().y() < comboCenter.y();
        m_finalGeometry.translate( 0, m_opensAbove ? -m_popupOffset : m_popupOffset );
        popup->setProperty( ComboBoxPopupOpensAboveProperty, m_opensAbove );
        popup->update();

        m_popupLayout        = popupLayout;
        m_viewLayoutIndex    = viewLayoutIndex;
        m_finalViewPosition  = popupView->pos();

        const QPoint snapshotViewPosition = popupView->mapTo( popup, QPoint( 0, 0 ) );
        const QPixmap fullSnapshot = popup->grab();
        const QPixmap viewSnapshot = copyFromSnapshot( fullSnapshot, QRect( snapshotViewPosition, popupView->size() ) );

        popupLayout->removeWidget( popupView );
        m_viewDetached = true;

        QPoint startViewPosition = m_finalViewPosition;
        if ( m_opensAbove )
        {
            // 父容器向上展开时已经带着 view 一起移动；view 只需从
            // 折叠容器的底边回到布局位置，不能再移动一个完整高度。
            startViewPosition.setY( 1 );
        }
        else
        {
            startViewPosition.ry() -= popupView->height();
        }

        // 单独截取没有 view 的容器背景。收起时背景保持不动，
        // 只有 view 的截图移动，避免移走后露出透明区域。
        popupView->move( m_finalViewPosition.x(), -popupView->height() - 1 );
        const QPixmap backgroundSnapshot = popup->grab();
        prepareCloseSnapshot( popup,
                              backgroundSnapshot,
                              viewSnapshot,
                              snapshotViewPosition,
                              popupView->size(),
                              popupView->viewport()->palette().brush( QPalette::Base ) );
        if ( !m_isOpening || !popup->isVisible() )
        {
            stopAnimation( popup );
            return;
        }

        popup->setFixedHeight( 1 );
        if ( m_opensAbove )
        {
            popup->move(
                m_finalGeometry.x(),
                m_finalGeometry.bottom() );
        }
        else
        {
            popup->move( m_finalGeometry.topLeft() );
        }
        popupView->move( startViewPosition );

        m_animationGroup = new QParallelAnimationGroup( popup );

        auto* heightAnimation = new QVariantAnimation( m_animationGroup );
        heightAnimation->setStartValue( 1 );
        heightAnimation->setEndValue( m_finalGeometry.height() );
        heightAnimation->setDuration( m_duration );
        heightAnimation->setEasingCurve( QEasingCurve::OutCubic );

        connect( heightAnimation,
                 &QVariantAnimation::valueChanged,
                 this,
                 [ this, popup ]( const QVariant& value )
                 {
                     const int height = value.toInt();
                     popup->setFixedHeight( height );

                     if ( m_opensAbove )
                     {
                         popup->move(
                             m_finalGeometry.x(),
                             m_finalGeometry.bottom() - height + 1 );
                     }
                     else
                     {
                         popup->move( m_finalGeometry.topLeft() );
                     }
                 } );

        auto* viewAnimation = new QPropertyAnimation( popupView, "pos", m_animationGroup );
        viewAnimation->setStartValue( startViewPosition );
        viewAnimation->setEndValue( m_finalViewPosition );
        viewAnimation->setDuration( m_duration );
        viewAnimation->setEasingCurve( QEasingCurve::OutCubic );

        m_animationGroup->addAnimation( heightAnimation );
        m_animationGroup->addAnimation( viewAnimation );

        connect( m_animationGroup,
                 &QParallelAnimationGroup::finished,
                 this,
                 [ this, popup ]
                 {
                     restorePopup( popup );
                     m_animationGroup = nullptr;
                     m_isOpening      = false;
                     refreshCloseSnapshot( popup );
                 } );

        m_animationGroup->start( QAbstractAnimation::DeleteWhenStopped );
    }

    void stopAnimation( QWidget* popup )
    {
        const bool hadAnimation = m_animationGroup;

        if ( m_animationGroup )
        {
            m_animationGroup->stop();
            m_animationGroup->deleteLater();
            m_animationGroup = nullptr;
        }

        m_isOpening = false;
        if ( hadAnimation || m_viewDetached )
        {
            restorePopup( popup );
        }
    }

    void restorePopup( QWidget* popup )
    {
        if ( m_finalGeometry.isValid() )
        {
            popup->setFixedHeight( m_finalGeometry.height() );
            popup->move( m_finalGeometry.topLeft() );
        }

        if ( !m_viewDetached || !m_comboBox || !m_popupLayout )
        {
            return;
        }

        auto* popupView = m_comboBox->view();
        popupView->move( m_finalViewPosition );

        auto* boxLayout = qobject_cast<QBoxLayout*>( m_popupLayout.data() );
        if ( !boxLayout )
        {
            return;
        }

        boxLayout->insertWidget( m_viewLayoutIndex, popupView );

        m_viewDetached = false;
    }

    void restoreHeightConstraints( QWidget* popup )
    {
        if ( !m_heightConstraintsSaved )
        {
            return;
        }

        popup->setMinimumHeight( m_originalMinimumHeight );
        popup->setMaximumHeight( m_originalMaximumHeight );
        m_heightConstraintsSaved = false;
    }

    static QPixmap copyFromSnapshot( const QPixmap& snapshot, const QRect& logicalRect )
    {
        if ( snapshot.isNull() || !logicalRect.isValid() )
        {
            return {};
        }

        const qreal ratio = snapshot.devicePixelRatio();
        const int left    = qFloor( logicalRect.left() * ratio );
        const int top     = qFloor( logicalRect.top() * ratio );
        const int right   = qCeil( ( logicalRect.left() + logicalRect.width() ) * ratio );
        const int bottom  = qCeil( ( logicalRect.top() + logicalRect.height() ) * ratio );
        const QRect pixelRect( left, top, right - left, bottom - top );

        QPixmap result = snapshot.copy( pixelRect.intersected( QRect( QPoint( 0, 0 ), snapshot.size() ) ) );
        result.setDevicePixelRatio( ratio );
        return result;
    }

    void refreshCloseSnapshot( QWidget* popup )
    {
        if ( !m_snapshotWidget || !m_comboBox || !popup->isVisible() )
        {
            return;
        }

        auto* popupView            = m_comboBox->view();
        const QPoint viewPosition  = popupView->mapTo( popup, QPoint( 0, 0 ) );
        const QPixmap fullSnapshot = popup->grab();
        if ( fullSnapshot.isNull() )
        {
            return;
        }

        const QPixmap viewSnapshot = copyFromSnapshot( fullSnapshot, QRect( viewPosition, popupView->size() ) );

        m_snapshotWidget->setViewSnapshot(
            viewSnapshot, viewPosition, popupView->size(), popupView->viewport()->palette().brush( QPalette::Base ) );
        m_closeViewHeight   = popupView->height();
        m_closeViewPosition = viewPosition;
        updateCloseGeometry( popup );
    }

    void updateCloseGeometry( QWidget* popup )
    {
        const QRect popupGeometry = popup->geometry();
        m_closeStartGeometry      = popupGeometry;
        m_closeStartGeometry.setSize( popupGeometry.size().expandedTo( m_snapshotLogicalSize ) );

        if ( m_opensAbove )
        {
            m_closeStartGeometry.moveBottom( popupGeometry.bottom() );
        }
    }

    void moveSnapshotOffscreen()
    {
        if ( !m_snapshotWidget )
        {
            removeApplicationEventFilter();
            return;
        }

        QRect virtualGeometry;
        const auto screens = QGuiApplication::screens();
        for ( QScreen* screen : screens )
        {
            virtualGeometry = virtualGeometry.united( screen->virtualGeometry() );
        }

        QRect offscreenGeometry = m_closeStartGeometry;
        if ( virtualGeometry.isValid() )
        {
            offscreenGeometry.moveTopLeft(
                QPoint( virtualGeometry.right() + offscreenGeometry.width() + 100,
                        virtualGeometry.bottom() + offscreenGeometry.height() + 100 ) );
        }
        else
        {
            offscreenGeometry.moveTopLeft( QPoint( 100000, 100000 ) );
        }

        m_snapshotWidget->setGeometry( offscreenGeometry );
    }

    void prepareCloseSnapshot( QWidget* popup,
                               const QPixmap& backgroundSnapshot,
                               const QPixmap& viewSnapshot,
                               const QPoint& viewPosition,
                               const QSize& viewSize,
                               const QBrush& viewBackground )
    {
        if ( !m_comboBox || !popup || !popup->isVisible() )
        {
            return;
        }

        stopCloseSnapshot();

        if ( backgroundSnapshot.isNull() )
        {
            return;
        }

        const qreal ratio     = backgroundSnapshot.devicePixelRatio();
        m_snapshotLogicalSize = QSize( qCeil( backgroundSnapshot.width() / ratio ), qCeil( backgroundSnapshot.height() / ratio ) );
        updateCloseGeometry( popup );

        m_snapshotWidget = new PopupSnapshotWidget(
            m_comboBox->window(),
            backgroundSnapshot,
            viewSnapshot,
            viewPosition,
            viewSize,
            viewBackground,
            m_cornerRadius,
            m_shadowBorderWidth,
            m_opensAbove );
        m_closeViewHeight   = viewSize.height();
        m_closeViewPosition = viewPosition;
        moveSnapshotOffscreen();
        m_snapshotWidget->setWindowOpacity( 1.0 );
        m_snapshotWidget->show();
    }

    void animateCloseSnapshot( QWidget* popup )
    {
        if ( !m_comboBox || !m_comboBox->window()->isVisible() || QGuiApplication::applicationState() != Qt::ApplicationActive )
        {
            stopCloseSnapshot();
            return;
        }

        if ( !m_snapshotWidget )
        {
            return;
        }

        const QRect startGeometry = m_closeStartGeometry;
        const int fullHeight      = startGeometry.height();
        if ( fullHeight <= 1 )
        {
            stopCloseSnapshot();
            return;
        }

        const QPoint comboCenter = m_comboBox->mapToGlobal( m_comboBox->rect().center() );
        const bool opensAbove    = startGeometry.center().y() < comboCenter.y();

        m_snapshotWidget->setGeometry( startGeometry );
        m_snapshotWidget->setContentOffset( 0 );
        m_snapshotWidget->setWindowOpacity( 1.0 );
        m_snapshotWidget->raise();

        m_closeAnimation = new QVariantAnimation( m_snapshotWidget );
        m_closeAnimation->setStartValue( 0.0 );
        m_closeAnimation->setEndValue( 1.0 );
        m_closeAnimation->setDuration( m_duration );
        m_closeAnimation->setEasingCurve( QEasingCurve::InCubic );

        connect( m_closeAnimation,
                 &QVariantAnimation::valueChanged,
                 this,
                 [ this, startGeometry, fullHeight, opensAbove ]( const QVariant& value )
                 {
                     if ( !m_snapshotWidget )
                     {
                         return;
                     }

                     const qreal progress = value.toReal();
                     const int height = qMax( 1, qRound( fullHeight - ( fullHeight - 1 ) * progress ) );

                     QRect geometry = startGeometry;
                     geometry.setHeight( height );

                     if ( opensAbove )
                     {
                         geometry.moveBottom( startGeometry.bottom() );
                     }

                     m_snapshotWidget->setGeometry( geometry );

                     // 模拟真实 view 离开容器：下方弹窗向上移，
                     // 上方弹窗主要依靠父容器下移，只补偿到折叠底边。
                     const int targetOffset = opensAbove ? qMax( 0, 1 - m_closeViewPosition.y() ) : -m_closeViewHeight;
                     const int offset       = qRound( targetOffset * progress );
                     m_snapshotWidget->setContentOffset( offset );
                 } );

        connect( m_closeAnimation, &QVariantAnimation::finished, this, [ this ] { stopCloseSnapshot(); } );

        m_closeAnimation->start();
    }

    void stopCloseSnapshot()
    {
        if ( m_closeAnimation )
        {
            m_closeAnimation->stop();
            m_closeAnimation->deleteLater();
            m_closeAnimation = nullptr;
        }

        if ( m_snapshotWidget )
        {
            m_snapshotWidget->hide();
            m_snapshotWidget->deleteLater();
            m_snapshotWidget = nullptr;
        }

        if ( !m_isOpening )
        {
            removeApplicationEventFilter();
        }
    }

    QPointer<QComboBox> m_comboBox;
    QPointer<QWidget> m_popup;
    QPointer<QParallelAnimationGroup> m_animationGroup;
    QPointer<QVariantAnimation> m_closeAnimation;
    QPointer<PopupSnapshotWidget> m_snapshotWidget;
    QPointer<QLayout> m_popupLayout;
    QRect m_finalGeometry;
    QRect m_closeStartGeometry;
    QSize m_snapshotLogicalSize;
    QPoint m_finalViewPosition;
    QPoint m_closeViewPosition;
    int m_viewLayoutIndex           = -1;
    int m_closeViewHeight           = 0;
    int m_cornerRadius              = 4;
    int m_popupOffset               = 2;
    int m_shadowBorderWidth         = 2;
    int m_originalMinimumHeight     = 0;
    int m_originalMaximumHeight     = QWIDGETSIZE_MAX;
    bool m_heightConstraintsSaved   = false;
    bool m_isOpening                = false;
    bool m_viewDetached             = false;
    bool m_opensAbove               = false;
    bool m_applicationFilterInstalled = false;
    int m_duration                  = 400;
};

ComboBoxPopupAnimator::ComboBoxPopupAnimator( QComboBox* comboBox, QObject* parent )
    : QObject( parent ? parent : comboBox )
    , m_impl( new ComboBoxPopupAnimatorImpl( comboBox, this ) )
{
}

ComboBoxPopupAnimator::~ComboBoxPopupAnimator() = default;

void ComboBoxPopupAnimator::stop()
{
    m_impl->stop();
}

void ComboBoxPopupAnimator::setDuration( int duration )
{
    m_impl->setDuration( qMax( 0, duration ) );
}

void ComboBoxPopupAnimator::setCornerRadius( int radius )
{
    m_impl->setCornerRadius( radius );
}

void ComboBoxPopupAnimator::setPopupOffset( int offset )
{
    m_impl->setPopupOffset( offset );
}

void ComboBoxPopupAnimator::setShadowBorderWidth( int width )
{
    m_impl->setShadowBorderWidth( width );
}

bool ComboBoxPopupAnimator::isEnabled( const QComboBox* comboBox )
{
    return comboBoxPopupAnimationEnabled( comboBox );
}

QComboBox* ComboBoxPopupAnimator::comboBoxForPopup( const QWidget* popup )
{
    const QWidget* parent = popup;
    while ( parent )
    {
        if ( auto* comboBox = qobject_cast<const QComboBox*>( parent ) )
        {
            return const_cast<QComboBox*>( comboBox );
        }
        parent = parent->parentWidget();
    }
    return nullptr;
}
