#include "comboboxpopupanimation_p.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QBoxLayout>
#include <QChildEvent>
#include <QComboBox>
#include <QEasingCurve>
#include <QEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QParallelAnimationGroup>
#include <QPixmap>
#include <QPointer>
#include <QPropertyAnimation>
#include <QRegion>
#include <QVariantAnimation>
#include <QWidget>
#include <QtMath>

#include "fluentui3styleproperties.h"

// 收起时显示的临时截图窗口。它不接收输入，也不参与 QComboBox 的状态管理。
class PopupSnapshotWidget final : public QWidget
{
public:
    PopupSnapshotWidget( QWidget* owner, const QPixmap& snapshot )
        : QWidget( owner,
                   Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput
                       | Qt::NoDropShadowWindowHint )
        , m_snapshot( snapshot )
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

    void setSnapshot( const QPixmap& snapshot )
    {
        m_snapshot = snapshot;
        update();
    }

protected:
    void paintEvent( QPaintEvent* ) override
    {
        QPainter painter( this );
        painter.setCompositionMode( QPainter::CompositionMode_Source );
        painter.fillRect( rect(), Qt::transparent );
        painter.setCompositionMode( QPainter::CompositionMode_SourceOver );
        painter.drawPixmap( 0, m_contentOffsetY, m_snapshot );
    }

private:
    QPixmap m_snapshot;
    int m_contentOffsetY = 0;
};

enum class ComboBoxPopupAnimationMode
{
    Disabled,
    DropDown,
    WinUI3
};

static bool comboBoxAnimationPropertyEnabled( const QComboBox* comboBox, const char* propertyName, bool defaultEnabled )
{
    const QVariant globalValue = qApp->property( propertyName );
    if ( globalValue.isValid() && !globalValue.toBool() )
    {
        return false;
    }

    if ( comboBox )
    {
        const QVariant localValue = comboBox->property( propertyName );
        if ( localValue.isValid() )
        {
            return localValue.toBool();
        }
    }

    return globalValue.isValid() ? globalValue.toBool() : defaultEnabled;
}

static ComboBoxPopupAnimationMode comboBoxPopupAnimationMode( const QComboBox* comboBox )
{
    // 该 Qt 私有属性只选择 Qt 自己的 popup 行为；开启时不叠加任何
    // FluentUI3Style 自定义动画。
    if ( qApp->property( "_q_scrollHint_center" ).toBool() )
    {
        return ComboBoxPopupAnimationMode::Disabled;
    }

    if ( comboBox && !comboBox->isEditable()
         && comboBoxAnimationPropertyEnabled( comboBox, ComboBoxPopupWinUI3AnimationEnabledProperty, false ) )
    {
        return ComboBoxPopupAnimationMode::WinUI3;
    }

    if ( comboBoxAnimationPropertyEnabled( comboBox, ComboBoxPopupDropDownAnimationEnabledProperty, true ) )
    {
        return ComboBoxPopupAnimationMode::DropDown;
    }

    return ComboBoxPopupAnimationMode::Disabled;
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

        const auto children = comboBox->findChildren<QWidget*>( QString(), Qt::FindDirectChildrenOnly );
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

    void setWinUI3Duration( int duration ) { m_winUI3Duration = duration; }

    void setPopupOffset( int offset ) { m_popupOffset = qMax( 0, offset ); }

    void stop()
    {
        if ( m_popup )
        {
            stopAnimation( m_popup );
        }
        stopCloseSnapshot();
        m_activeMode = ComboBoxPopupAnimationMode::Disabled;
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
            // 同一个过滤器同时安装在 qApp 和 popup 上时，同一个事件会
            // 进入两次。每次显示周期只处理第一个 Show/Hide。
            if ( m_popupShown )
            {
                return QObject::eventFilter( watched, event );
            }
            m_popupShown = true;

            const ComboBoxPopupAnimationMode mode = comboBoxPopupAnimationMode( m_comboBox );
            if ( mode != ComboBoxPopupAnimationMode::Disabled )
            {
                animatePopup( m_popup, mode );
            }
            return QObject::eventFilter( watched, event );
        }

        const bool popupHideEvent = watched == m_popup && event->type() == QEvent::Hide;
        if ( popupHideEvent )
        {
            if ( !m_popupShown )
            {
                return QObject::eventFilter( watched, event );
            }
            m_popupShown = false;
        }

        if ( comboBoxPopupAnimationMode( m_comboBox ) == ComboBoxPopupAnimationMode::Disabled )
        {
            stopAnimation( m_popup );
            stopCloseSnapshot();
            m_activeMode = ComboBoxPopupAnimationMode::Disabled;
            removeApplicationEventFilter();
            return QObject::eventFilter( watched, event );
        }

        QWidget* popup = m_popup;

        // 展开完成前吞掉后续输入，避免动画几何尚未恢复时关闭或选中。
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
        if ( m_activeMode == ComboBoxPopupAnimationMode::DropDown && popup->isVisible() && !m_isOpening )
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

        if ( popupHideEvent )
        {
            const ComboBoxPopupAnimationMode activeMode = m_activeMode;
            const bool openingWasInterrupted            = m_isOpening;
            stopAnimation( popup );
            m_activeMode = ComboBoxPopupAnimationMode::Disabled;

            if ( activeMode != ComboBoxPopupAnimationMode::DropDown || openingWasInterrupted )
            {
                stopCloseSnapshot();
            }
            else
            {
                // 普通下拉模式使用真实弹窗的截图播放视觉上的收起动画。
                animateCloseSnapshot( popup );
            }
            restoreHeightConstraints( popup );
        }

        return QObject::eventFilter( watched, event );
    }

private:
    void attachPopup( QWidget* popup )
    {
        if ( !popup || !popup->inherits( "QComboBoxPrivateContainer" ) || ComboBoxPopupAnimator::comboBoxForPopup( popup ) != m_comboBox
             || m_popup == popup )
        {
            return;
        }

        if ( m_popup )
        {
            m_popup->removeEventFilter( this );
        }
        m_popup      = popup;
        m_popupShown = popup->isVisible();
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

    bool animatePopup( QWidget* popup, ComboBoxPopupAnimationMode mode )
    {
        stopCloseSnapshot();
        stopAnimation( popup );

        const bool started = mode == ComboBoxPopupAnimationMode::WinUI3 ? beginWinUI3PopupAnimation( popup )
                                                                        : beginDropDownPopupAnimation( popup );
        if ( !started )
        {
            m_activeMode = ComboBoxPopupAnimationMode::Disabled;
            return false;
        }

        installApplicationEventFilter();

        if ( mode == ComboBoxPopupAnimationMode::WinUI3 )
        {
            animateWinUI3Popup( popup );
        }
        else
        {
            animateDropDownPopup( popup );
        }
        return true;
    }

    bool beginDropDownPopupAnimation( QWidget* popup )
    {
        auto* popupView           = m_comboBox->view();
        auto* popupLayout         = popup->layout();
        auto* popupBoxLayout      = qobject_cast<QBoxLayout*>( popupLayout );
        const int viewLayoutIndex = popupLayout ? popupLayout->indexOf( popupView ) : -1;
        if ( !popupView || !popupBoxLayout || viewLayoutIndex < 0 )
        {
            return false;
        }

        m_isOpening              = true;
        m_activeMode             = ComboBoxPopupAnimationMode::DropDown;
        m_finalGeometry          = popup->geometry();
        m_originalMinimumHeight  = popup->minimumHeight();
        m_originalMaximumHeight  = popup->maximumHeight();
        m_heightConstraintsSaved = true;

        const QPoint comboCenter = m_comboBox->mapToGlobal( m_comboBox->rect().center() );
        m_opensAbove             = m_finalGeometry.center().y() < comboCenter.y();
        m_finalGeometry.translate( 0, m_opensAbove ? -m_popupOffset : m_popupOffset );
        popup->setProperty( ComboBoxPopupOpensAboveProperty, m_opensAbove );
        popup->update();

        m_popupLayout       = popupLayout;
        m_viewLayoutIndex   = viewLayoutIndex;
        m_finalViewPosition = popupView->pos();

        popupLayout->removeWidget( popupView );
        m_viewDetached = true;
        return true;
    }

    bool beginWinUI3PopupAnimation( QWidget* popup )
    {
        if ( !popup || !m_comboBox || popup->height() <= 0 )
        {
            return false;
        }

        m_isOpening     = true;
        m_activeMode    = ComboBoxPopupAnimationMode::WinUI3;
        m_finalGeometry = popup->geometry();

        const QPoint comboCenter = m_comboBox->mapToGlobal( m_comboBox->rect().center() );
        m_opensAbove             = m_finalGeometry.center().y() < comboCenter.y();
        popup->setProperty( ComboBoxPopupOpensAboveProperty, m_opensAbove );
        return true;
    }

    void animateDropDownPopup( QWidget* popup )
    {
        auto* popupView          = m_comboBox->view();
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

        popup->setFixedHeight( 1 );
        if ( m_opensAbove )
        {
            popup->move( m_finalGeometry.x(), m_finalGeometry.bottom() );
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
                         popup->move( m_finalGeometry.x(), m_finalGeometry.bottom() - height + 1 );
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
                     restoreHeightConstraints( popup );
                     m_animationGroup = nullptr;
                     m_isOpening      = false;
                     refreshCloseSnapshot( popup );
                 } );

        m_animationGroup->start( QAbstractAnimation::DeleteWhenStopped );
    }

    void animateWinUI3Popup( QWidget* popup )
    {
        const qreal openedLength     = popup->height();
        const int comboCenterY       = m_comboBox->mapToGlobal( m_comboBox->rect().center() ).y();
        const qreal clipCenterY      = comboCenterY - m_finalGeometry.top();
        const qreal offsetFromCenter = clipCenterY - openedLength / 2.0;

        // WinUI SplitOpenThemeAnimation 没有显式设置 ClosedLength，
        // 因而从 OpenedLength 的 50% 开始。若锚点靠近边缘，则放大
        // 初始 Clip，避免裁剪矩形有一部分落在 Popup 外。
        constexpr qreal closedRatio = 0.5;
        qreal initialClipScaleY     = closedRatio;
        const qreal maxOffset       = openedLength * ( 1.0 - closedRatio ) / 2.0;
        if ( qAbs( offsetFromCenter ) > maxOffset )
        {
            const qreal clipLength = openedLength * closedRatio;
            const qreal pixelsOff  = clipLength / 2.0 - ( openedLength / 2.0 - qAbs( offsetFromCenter ) );
            initialClipScaleY      = pixelsOff / openedLength * 2.0 + closedRatio;
        }

        const qreal finalClipScaleY = ( 0.5 + qAbs( offsetFromCenter / openedLength ) ) * 2.0;
        QPointer<QWidget> popupView = m_comboBox->view();
        QPointer<QWidget> popupViewport =
            popupView ? m_comboBox->view()->viewport() : nullptr;

        const auto applyClip =
            [ popup, popupView, popupViewport, clipCenterY, openedLength ]( qreal scaleY )
        {
            const int clipHeight = qMax( 1, qRound( openedLength * scaleY ) );
            const int clipTop    = qRound( clipCenterY - clipHeight / 2.0 );
            const QRect clipRect = QRect( 0, clipTop, popup->width(), clipHeight ).intersected( popup->rect() );
            popup->setMask( QRegion( clipRect ) );

            // setMask() 只改变窗口可见区域，Qt 不一定会重绘新暴露的
            // backing-store 像素。主题切换后必须同步刷新这些区域。
            popup->repaint( clipRect );
            if ( popupView )
            {
                popupView->repaint();
            }
            if ( popupViewport )
            {
                popupViewport->repaint();
            }
        };

        applyClip( initialClipScaleY );

        m_animationGroup = new QParallelAnimationGroup( popup );

        auto* splitAnimation = new QVariantAnimation( m_animationGroup );
        splitAnimation->setStartValue( initialClipScaleY );
        splitAnimation->setEndValue( finalClipScaleY );
        splitAnimation->setDuration( m_winUI3Duration );

        QEasingCurve splitOpenEasing( QEasingCurve::BezierSpline );
        splitOpenEasing.addCubicBezierSegment( QPointF( 0.0, 0.0 ), QPointF( 0.0, 1.0 ), QPointF( 1.0, 1.0 ) );
        splitAnimation->setEasingCurve( splitOpenEasing );

        connect( splitAnimation,
                 &QVariantAnimation::valueChanged,
                 this,
                 [ applyClip ]( const QVariant& value ) { applyClip( value.toReal() ); } );

        m_animationGroup->addAnimation( splitAnimation );

        connect( m_animationGroup,
                 &QParallelAnimationGroup::finished,
                 this,
                 [ this, popup, popupView, popupViewport ]
                 {
                     popup->clearMask();
                     popup->repaint();
                     if ( popupView )
                     {
                         popupView->repaint();
                     }
                     if ( popupViewport )
                     {
                         popupViewport->repaint();
                     }
                     m_animationGroup = nullptr;
                     m_isOpening      = false;
                     removeApplicationEventFilter();
                 } );

        m_animationGroup->start( QAbstractAnimation::DeleteWhenStopped );
    }

    void stopAnimation( QWidget* popup )
    {
        const bool hadAnimation                     = m_animationGroup;
        const ComboBoxPopupAnimationMode activeMode = m_activeMode;

        if ( m_animationGroup )
        {
            m_animationGroup->stop();
            m_animationGroup->deleteLater();
            m_animationGroup = nullptr;
        }

        m_isOpening = false;
        if ( activeMode == ComboBoxPopupAnimationMode::WinUI3 )
        {
            popup->clearMask();
            popup->update();
        }
        else if ( hadAnimation || m_viewDetached )
        {
            restorePopup( popup );
        }
        restoreHeightConstraints( popup );
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

    void refreshCloseSnapshot( QWidget* popup )
    {
        if ( !m_comboBox || !popup->isVisible() )
        {
            return;
        }

        const QPixmap snapshot = popup->grab();
        if ( snapshot.isNull() )
        {
            return;
        }

        if ( !m_snapshotWidget )
        {
            prepareCloseSnapshot( popup, snapshot );
            return;
        }

        const qreal ratio     = snapshot.devicePixelRatio();
        m_snapshotLogicalSize = QSize( qCeil( snapshot.width() / ratio ), qCeil( snapshot.height() / ratio ) );
        m_snapshotWidget->setSnapshot( snapshot );
        updateCloseGeometry( popup );
        if ( m_snapshotWidget->geometry() != m_closeStartGeometry )
        {
            m_snapshotWidget->setGeometry( m_closeStartGeometry );
        }
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

    void prepareCloseSnapshot( QWidget* popup, const QPixmap& snapshot )
    {
        if ( !m_comboBox || !popup || !popup->isVisible() || snapshot.isNull() )
        {
            return;
        }

        const qreal ratio     = snapshot.devicePixelRatio();
        m_snapshotLogicalSize = QSize( qCeil( snapshot.width() / ratio ), qCeil( snapshot.height() / ratio ) );
        updateCloseGeometry( popup );

        m_snapshotWidget = new PopupSnapshotWidget( m_comboBox->window(), snapshot );
        // 在目标屏幕原位创建原生窗口，避免从虚拟桌面外移回时因屏幕
        // DPI 不同而被 QWindowsWindow 再次缩放。0 透明度下不会遮挡 popup。
        m_snapshotWidget->setGeometry( m_closeStartGeometry );
        m_snapshotWidget->setWindowOpacity( 0.0 );
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
            stopCloseSnapshot();
            return;
        }

        updateCloseGeometry( popup );
        const QRect startGeometry = m_closeStartGeometry;
        const int fullHeight      = startGeometry.height();
        if ( fullHeight <= 1 )
        {
            stopCloseSnapshot();
            return;
        }

        const QPoint comboCenter = m_comboBox->mapToGlobal( m_comboBox->rect().center() );
        const bool opensAbove    = startGeometry.center().y() < comboCenter.y();

        if ( m_snapshotWidget->geometry() != startGeometry )
        {
            m_snapshotWidget->setGeometry( startGeometry );
        }
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

                     const qreal progress   = value.toReal();
                     const int targetOffset = opensAbove ? fullHeight : -fullHeight;
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
        m_snapshotLogicalSize = QSize();

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
    int m_viewLayoutIndex                   = -1;
    int m_popupOffset                       = 2;
    int m_originalMinimumHeight             = 0;
    int m_originalMaximumHeight             = QWIDGETSIZE_MAX;
    bool m_heightConstraintsSaved           = false;
    bool m_isOpening                        = false;
    bool m_viewDetached                     = false;
    bool m_opensAbove                       = false;
    bool m_popupShown                       = false;
    bool m_applicationFilterInstalled       = false;
    ComboBoxPopupAnimationMode m_activeMode = ComboBoxPopupAnimationMode::Disabled;
    int m_duration                          = 400;
    int m_winUI3Duration                    = 250;
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

void ComboBoxPopupAnimator::setWinUI3Duration( int duration )
{
    m_impl->setWinUI3Duration( qMax( 0, duration ) );
}

void ComboBoxPopupAnimator::setPopupOffset( int offset )
{
    m_impl->setPopupOffset( offset );
}

bool ComboBoxPopupAnimator::isEnabled( const QComboBox* comboBox )
{
    return comboBoxPopupAnimationMode( comboBox ) != ComboBoxPopupAnimationMode::Disabled;
}

bool ComboBoxPopupAnimator::isWinUI3AnimationEnabled( const QComboBox* comboBox )
{
    return comboBoxPopupAnimationMode( comboBox ) == ComboBoxPopupAnimationMode::WinUI3;
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
