#pragma once

#include <QtCore/qobjectdefs.h>

// Declares a Qt property together with its inline getter, setter declaration,
// change signal and private m_<property> storage. Setter implementations remain
// in the source file.
#define EXWIDGETS_DECLARE_PROPERTY( Type, Property, Getter, Setter, DefaultValue ) \
    Q_PROPERTY( Type Property READ Getter WRITE Setter NOTIFY Property##Changed ) \
    [[nodiscard]] Type Getter() const { return m_##Property; } \
    void Setter( Type value ); \
    Q_SIGNAL void Property##Changed( Type value ); \
private: \
    Type m_##Property = DefaultValue; \
public:

// d 指针版本：只声明属性和访问器，不生成存储，也不在头文件中解引用未完整定义的私有类。
// getter/setter 在源文件中实现，适合转发到子控件或私有数据的属性。
#define EXWIDGETS_DECLARE_PROPERTY_D( Type, Property, Getter, Setter ) \
    Q_PROPERTY( Type Property READ Getter WRITE Setter ) \
    [[nodiscard]] Type Getter() const; \
    void Setter( Type value );
