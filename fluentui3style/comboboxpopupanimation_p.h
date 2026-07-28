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
    void setWinUI3Duration( int duration );

    // 普通下拉动画中 popup 与 ComboBox 主体之间的外部间距。
    // WinUI3 中心展开使用 Qt 算出的最终几何，不应用该偏移。
    void setPopupOffset( int offset );

    static bool isEnabled( const QComboBox* comboBox );
    static bool isWinUI3AnimationEnabled( const QComboBox* comboBox );
    static QComboBox* comboBoxForPopup( const QWidget* popup );

private:
    ComboBoxPopupAnimatorImpl* m_impl;
};
