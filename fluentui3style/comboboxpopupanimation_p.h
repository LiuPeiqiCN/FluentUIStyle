#pragma once

#include <QObject>

class QComboBox;
class QWidget;
class ComboBoxPopupAnimatorImpl;

inline constexpr const char* ComboBoxPopupOpensAboveProperty =
    "_q_fluent_combo_popup_opens_above";

// FluentUI3Style 的 ComboBox popup 动画控制器。
class ComboBoxPopupAnimator final : public QObject
{
public:
    explicit ComboBoxPopupAnimator( QComboBox* comboBox, QObject* parent = nullptr );
    ~ComboBoxPopupAnimator() override;

    void stop();
    void setDuration( int duration );
    void setCornerRadius( int radius );

    // popup 与 ComboBox 主体之间的外部间距。启用本动画器后，最终几何
    // 始终由动画器按展开方向偏移，动画结束后不会恢复到偏移前的位置。
    void setPopupOffset( int offset );

    // 截图代理绘制时排除的 popup 内部阴影区域。
    void setShadowBorderWidth( int width );

    static bool isEnabled( const QComboBox* comboBox );
    static QComboBox* comboBoxForPopup( const QWidget* popup );

private:
    ComboBoxPopupAnimatorImpl* m_impl;
};
