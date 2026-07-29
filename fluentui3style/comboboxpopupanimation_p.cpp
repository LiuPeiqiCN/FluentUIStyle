#include "comboboxpopupanimation_p.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QBoxLayout>
#include <QComboBox>
#include <QEasingCurve>
#include <QEvent>
#include <QKeyEvent>
#include <QParallelAnimationGroup>
#include <QPointer>
#include <QPropertyAnimation>
#include <QVariantAnimation>
#include <QWidget>

#include "fluentui3styleproperties.h"

static constexpr int PopupOffset       = 3;
static constexpr int AnimationDuration = 300;

static bool comboBoxAnimationPropertyEnabled( const QComboBox* comboBox,
                                              const char* propertyName,
                                              bool defaultEnabled )
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

static bool comboBoxPopupAnimationEnabled( const QComboBox* comboBox )
{
    // 该 Qt 私有属性只选择 Qt 自己的 popup 行为；开启时不叠加任何
    // FluentUI3Style 自定义动画。
    if ( qApp->property( "_q_scrollHint_center" ).toBool() )
    {
        return false;
    }

    return comboBoxAnimationPropertyEnabled(
        comboBox,
        ComboBoxPopupDropDownAnimationEnabledProperty,
        true );
}

// 普通 QComboBox 无法在 hidePopup() 前接管关闭过程，因此这里只通过
// QComboBoxPrivateContainer 的 Show 事件实现展开动画，关闭完全交还 Qt。
class ComboBoxPopupAnimatorImpl final : public QObject
{
public:
    explicit ComboBoxPopupAnimatorImpl( QComboBox* comboBox, QObject* parent )
        : QObject( parent ? parent : comboBox )
        , m_comboBox( comboBox )
    {
        attachPopup( comboBox->view()->window() );
    }

    ~ComboBoxPopupAnimatorImpl() override
    {
        stop();
    }

    void stop()
    {
        if ( m_popup )
        {
            stopAnimation( m_popup );
        }
        removeApplicationEventFilter();
    }

protected:
    bool eventFilter( QObject* watched, QEvent* event ) override
    {
        if ( !m_comboBox || !m_popup )
        {
            return QObject::eventFilter( watched, event );
        }

        if ( event->type() == QEvent::ApplicationDeactivate
             || ( watched == m_comboBox->window()
                  && ( event->type() == QEvent::Hide
                       || event->type() == QEvent::Close ) ) )
        {
            stopAnimation( m_popup );
            removeApplicationEventFilter();
            return QObject::eventFilter( watched, event );
        }

        if ( watched == m_popup && event->type() == QEvent::Show )
        {
            if ( !m_popupShown )
            {
                m_popupShown = true;
                if ( comboBoxPopupAnimationEnabled( m_comboBox ) )
                {
                    animatePopup( m_popup );
                }
            }
            return QObject::eventFilter( watched, event );
        }

        if ( watched == m_popup && event->type() == QEvent::Hide )
        {
            m_popupShown = false;
            stopAnimation( m_popup );
            removeApplicationEventFilter();
            return QObject::eventFilter( watched, event );
        }

        if ( !comboBoxPopupAnimationEnabled( m_comboBox ) )
        {
            stopAnimation( m_popup );
            removeApplicationEventFilter();
            return QObject::eventFilter( watched, event );
        }

        // 展开完成前吞掉后续输入，避免动画几何尚未恢复时关闭或选中。
        // view/viewport 会直接接收这些事件，所以动画期间临时从 qApp
        // 过滤，而不是给整个应用永久安装过滤器。
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
                    if ( key == Qt::Key_Escape
                         || key == Qt::Key_Enter
                         || key == Qt::Key_Return
                         || key == Qt::Key_Space )
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
    void attachPopup( QWidget* popup )
    {
        if ( !popup || !popup->inherits( "QComboBoxPrivateContainer" )
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

    void animatePopup( QWidget* popup )
    {
        stopAnimation( popup );

        if ( !beginPopupAnimation( popup ) )
        {
            return;
        }

        installApplicationEventFilter();
        startPopupAnimation( popup );
    }

    bool beginPopupAnimation( QWidget* popup )
    {
        auto* popupView           = m_comboBox->view();
        auto* popupLayout         = popup->layout();
        auto* popupBoxLayout      = qobject_cast<QBoxLayout*>( popupLayout );
        const int viewLayoutIndex = popupLayout
                                        ? popupLayout->indexOf( popupView )
                                        : -1;
        if ( !popupView || !popupBoxLayout || viewLayoutIndex < 0 )
        {
            return false;
        }

        m_isOpening     = true;
        m_finalGeometry = popup->geometry();

        const QPoint comboCenter =
            m_comboBox->mapToGlobal( m_comboBox->rect().center() );
        m_opensAbove =
            m_finalGeometry.center().y() < comboCenter.y();
        m_finalGeometry.translate(
            0,
            m_opensAbove ? -PopupOffset : PopupOffset );
        popup->setProperty(
            ComboBoxPopupOpensAboveProperty,
            m_opensAbove );
        popup->update();

        m_popupLayout       = popupLayout;
        m_viewLayoutIndex   = viewLayoutIndex;
        m_finalViewPosition = popupView->pos();

        popupLayout->removeWidget( popupView );
        m_viewDetached = true;
        return true;
    }

    void startPopupAnimation( QWidget* popup )
    {
        auto* popupView          = m_comboBox->view();
        QPoint startViewPosition = m_finalViewPosition;
        if ( m_opensAbove )
        {
            startViewPosition.setY( 1 );
        }
        else
        {
            startViewPosition.ry() -= popupView->height();
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

        auto* heightAnimation =
            new QVariantAnimation( m_animationGroup );
        heightAnimation->setStartValue( 1 );
        heightAnimation->setEndValue( m_finalGeometry.height() );
        heightAnimation->setDuration( AnimationDuration );
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

        auto* viewAnimation =
            new QPropertyAnimation(
                popupView,
                "pos",
                m_animationGroup );
        viewAnimation->setStartValue( startViewPosition );
        viewAnimation->setEndValue( m_finalViewPosition );
        viewAnimation->setDuration( AnimationDuration );
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
                     removeApplicationEventFilter();
                 } );

        m_animationGroup->start(
            QAbstractAnimation::DeleteWhenStopped );
    }

    void stopAnimation( QWidget* popup )
    {
        const bool hadAnimation = m_animationGroup;

        if ( m_animationGroup )
        {
            m_animationGroup->stop();
            m_animationGroup = nullptr;
        }

        m_isOpening = false;
        if ( hadAnimation || m_viewDetached )
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

        auto* boxLayout =
            qobject_cast<QBoxLayout*>( m_popupLayout.data() );
        if ( !boxLayout )
        {
            return;
        }

        boxLayout->insertWidget( m_viewLayoutIndex, popupView );
        m_viewDetached = false;
    }

    void restoreHeightConstraints( QWidget* popup )
    {
        popup->setMinimumHeight( 0 );
        popup->setMaximumHeight( QWIDGETSIZE_MAX );
    }

    QPointer<QComboBox> m_comboBox;
    QPointer<QWidget> m_popup;
    QPointer<QParallelAnimationGroup> m_animationGroup;
    QPointer<QLayout> m_popupLayout;
    QRect m_finalGeometry;
    QPoint m_finalViewPosition;
    int m_viewLayoutIndex             = -1;
    bool m_isOpening                  = false;
    bool m_viewDetached               = false;
    bool m_opensAbove                 = false;
    bool m_popupShown                 = false;
    bool m_applicationFilterInstalled = false;
};

ComboBoxPopupAnimator::ComboBoxPopupAnimator( QComboBox* comboBox,
                                              QObject* parent )
    : QObject( parent ? parent : comboBox )
    , m_impl( new ComboBoxPopupAnimatorImpl( comboBox, this ) )
{}

ComboBoxPopupAnimator::~ComboBoxPopupAnimator() = default;

void ComboBoxPopupAnimator::stop()
{
    m_impl->stop();
}

bool ComboBoxPopupAnimator::isEnabled( const QComboBox* comboBox )
{
    return comboBoxPopupAnimationEnabled( comboBox );
}

QComboBox* ComboBoxPopupAnimator::comboBoxForPopup(
    const QWidget* popup )
{
    const QWidget* parent = popup;
    while ( parent )
    {
        if ( auto* comboBox =
                 qobject_cast<const QComboBox*>( parent ) )
        {
            return const_cast<QComboBox*>( comboBox );
        }
        parent = parent->parentWidget();
    }
    return nullptr;
}
