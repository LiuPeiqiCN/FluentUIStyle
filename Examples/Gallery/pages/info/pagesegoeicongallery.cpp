#include "pagesegoeicongallery.h"

#include <QAbstractListModel>
#include <QClipboard>
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QPalette>
#include <QRegularExpression>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>
#include <QTextStream>
#include <QVBoxLayout>

#include <exinfobar.h>
#include <exinfobarhost.h>

#include "font-icon/fonticon.h"

namespace {

enum SegoeIconCustomRoles {
    NameRole = Qt::UserRole + 1,
    CodeRole,
    CodeTextRole,
    SearchRole,
};

// =============================================================================
// SegoeIconModel：只读数据模型，管理 1403 个图标的数据提供
// =============================================================================
class SegoeIconModel : public QAbstractListModel
{
public:
    explicit SegoeIconModel( QObject* parent = nullptr )
        : QAbstractListModel( parent )
        , m_entries( PageSegoeIconGallery::iconEntries() )
    {
    }

    int rowCount( const QModelIndex& parent = QModelIndex() ) const override
    {
        if ( parent.isValid() )
        {
            return 0;
        }
        return m_entries.size();
    }

    QVariant data( const QModelIndex& index, int role = Qt::DisplayRole ) const override
    {
        if ( !index.isValid() || index.row() < 0 || index.row() >= m_entries.size() )
        {
            return {};
        }

        const auto& entry = m_entries.at( index.row() );
        switch ( role )
        {
            case Qt::DisplayRole :
            case NameRole :
                return entry.name;
            case CodeRole :
                return entry.code;
            case CodeTextRole :
                return QStringLiteral( "0x%1" ).arg( QString::number( entry.code, 16 ).toUpper().rightJustified( 4, QLatin1Char( '0' ) ) );
            case SearchRole :
                return QStringLiteral( "%1 %2 0x%2" ).arg( entry.name, QString::number( entry.code, 16 ) );
            case Qt::ToolTipRole :
            {
                const QString codeText = QStringLiteral( "0x%1" ).arg( QString::number( entry.code, 16 ).toUpper().rightJustified( 4, QLatin1Char( '0' ) ) );
                return QStringLiteral( "%1\n%2\n点击复制编码" ).arg( entry.name, codeText );
            }
            default :
                break;
        }
        return {};
    }

private:
    QList<PageSegoeIconGallery::IconEntry> m_entries;
};

// =============================================================================
// SegoeIconDelegate：高性能轻量化卡片绘制委托
// =============================================================================
class SegoeIconDelegate : public QStyledItemDelegate
{
public:
    explicit SegoeIconDelegate( QObject* parent = nullptr )
        : QStyledItemDelegate( parent )
    {
    }

    QSize sizeHint( const QStyleOptionViewItem& /*option*/, const QModelIndex& /*index*/ ) const override
    {
        return QSize( 114, 104 );
    }

    void paint( QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index ) const override
    {
        if ( !index.isValid() )
        {
            return;
        }

        painter->save();
        painter->setRenderHint( QPainter::Antialiasing, true );
        painter->setRenderHint( QPainter::TextAntialiasing, true );

        const QRect rect     = option.rect.adjusted( 3, 3, -3, -3 );
        const bool isDark    = option.palette.color( QPalette::Base ).lightness() < 128;
        const bool isHover   = ( option.state & QStyle::State_MouseOver );
        const bool isPressed = ( option.state & QStyle::State_Sunken );

        // 绘制悬停与按下态高光圆角底色
        if ( isHover || isPressed )
        {
            QColor bgColor = isDark ? QColor( 255, 255, 255 ) : QColor( 0, 0, 0 );
            bgColor.setAlpha( isPressed ? 24 : 12 );
            painter->setBrush( bgColor );
            painter->setPen( Qt::NoPen );
            painter->drawRoundedRect( rect, 6.0, 6.0 );
        }

        const QString name     = index.data( NameRole ).toString();
        const int code         = index.data( CodeRole ).toInt();
        const QString codeText = index.data( CodeTextRole ).toString();

        // 1. 绘制 Segoe Fluent Icons 图标字体
        QFont iconFont( QString::fromLatin1( SegoeIcon::SegoeFontName ) );
        iconFont.setPixelSize( 30 );
        iconFont.setHintingPreference( QFont::PreferNoHinting );
        painter->setFont( iconFont );
        painter->setPen( isDark ? QColor( 255, 255, 255 ) : QColor( 26, 26, 26 ) );

        const QRect iconRect( rect.left(), rect.top() + 6, rect.width(), 36 );
        painter->drawText( iconRect, Qt::AlignCenter, QString( QChar( static_cast<char16_t>( code ) ) ) );

        // 2. 绘制图标名称
        QFont nameFont = option.font;
        nameFont.setPixelSize( 11 );
        painter->setFont( nameFont );
        painter->setPen( isDark ? QColor( 230, 230, 230 ) : QColor( 32, 32, 32 ) );

        const QRect nameRect( rect.left() + 4, iconRect.bottom() + 4, rect.width() - 8, 30 );
        painter->drawText( nameRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, name );

        // 3. 绘制 16 进制编码
        QFont codeFont = nameFont;
        codeFont.setPixelSize( 10 );
        painter->setFont( codeFont );
        QColor subColor = option.palette.color( QPalette::WindowText );
        subColor.setAlpha( isDark ? 160 : 130 );
        painter->setPen( subColor );

        const QRect codeRect( rect.left() + 4, rect.bottom() - 16, rect.width() - 8, 15 );
        painter->drawText( codeRect, Qt::AlignHCenter | Qt::AlignBottom, codeText );

        painter->restore();
    }
};

}  // namespace

// =============================================================================
// PageSegoeIconGallery 实现
// =============================================================================
PageSegoeIconGallery::PageSegoeIconGallery( QWidget* parent )
    : QFrame( parent )
{
    setFrameShape( QFrame::StyledPanel );

    initializeUi();
}

void PageSegoeIconGallery::initializeUi()
{
    QVBoxLayout* layout = new QVBoxLayout( this );
    layout->setContentsMargins( 9, 9, 9, 9 );
    layout->setSpacing( 14 );

    QLabel* titleLabel = new QLabel( QStringLiteral( "Segoe Fluent Icons 图标" ), this );
    titleLabel->setStyleSheet( QStringLiteral( "font-size: 18pt; font-weight: 800;" ) );

    m_searchEdit = new QLineEdit( this );
    m_searchEdit->setPlaceholderText( QStringLiteral( "输入名称或编码筛选" ) );
    m_searchEdit->setClearButtonEnabled( true );
    m_searchEdit->setFixedWidth( 240 );

    QHBoxLayout* topBarLayout = new QHBoxLayout();
    topBarLayout->setContentsMargins( 0, 0, 0, 0 );
    topBarLayout->addWidget( titleLabel );
    topBarLayout->addStretch();
    topBarLayout->addWidget( m_searchEdit );

    // 构建 Model - View - Delegate 架构
    m_model      = new SegoeIconModel( this );
    m_proxyModel = new QSortFilterProxyModel( this );
    m_proxyModel->setSourceModel( m_model );
    m_proxyModel->setFilterRole( SearchRole );
    m_proxyModel->setFilterCaseSensitivity( Qt::CaseInsensitive );

    m_listView = new QListView( this );
    m_listView->setViewMode( QListView::IconMode );
    m_listView->setResizeMode( QListView::Adjust );
    m_listView->setUniformItemSizes( true );
    m_listView->setMovement( QListView::Static );
    m_listView->setSpacing( 4 );
    m_listView->setGridSize( QSize( 114, 104 ) );
    m_listView->setSelectionMode( QAbstractItemView::NoSelection );
    m_listView->setEditTriggers( QAbstractItemView::NoEditTriggers );
    m_listView->setFrameShape( QFrame::NoFrame );
    m_listView->setVerticalScrollMode( QAbstractItemView::ScrollPerPixel );
    m_listView->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    m_listView->setWordWrap( true );
    m_listView->setMouseTracking( true );
    m_listView->setItemDelegate( new SegoeIconDelegate( m_listView ) );
    m_listView->setModel( m_proxyModel );

    // 搜索实时联动
    connect( m_searchEdit, &QLineEdit::textChanged, m_proxyModel, &QSortFilterProxyModel::setFilterFixedString );

    // 点击复制到剪贴板并提示
    connect( m_listView, &QListView::clicked, this, [ this ]( const QModelIndex& index ) {
        if ( !index.isValid() )
        {
            return;
        }

        const QString name = index.data( NameRole ).toString();
        const QString code = index.data( CodeTextRole ).toString();

        QGuiApplication::clipboard()->setText( code );

        ExInfoBarHost* host = ExInfoBarHost::defaultHost();
        if ( !host )
        {
            host = new ExInfoBarHost( window() ? window() : this, this );
        }
        host->showInfoBar( ExInfoBar::Success,
                           QStringLiteral( "已复制到剪贴板" ),
                           QStringLiteral( "图标: %1   编码: %2" ).arg( name, code ),
                           ExInfoBarHost::Top,
                           2000 );
    } );

    layout->addLayout( topBarLayout );
    layout->addWidget( m_listView, 1 );
}

QList<PageSegoeIconGallery::IconEntry> PageSegoeIconGallery::iconEntries()
{
    static QList<IconEntry> entries;
    if ( !entries.isEmpty() )
    {
        return entries;
    }

    QFile enumFile( QStringLiteral( ":/resource/SegoeFluentIconsEnum.txt" ) );
    if ( !enumFile.open( QIODevice::ReadOnly | QIODevice::Text ) )
    {
        return entries;
    }

    const QRegularExpression enumLinePattern( QStringLiteral( "^\\s*([A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*0x([0-9A-Fa-f]+)\\s*,?\\s*$" ) );

    QTextStream stream( &enumFile );
    while ( !stream.atEnd() )
    {
        const QString line                  = stream.readLine();
        const QRegularExpressionMatch match = enumLinePattern.match( line );
        if ( !match.hasMatch() )
        {
            continue;
        }

        bool ok        = false;
        const int code = match.captured( 2 ).toInt( &ok, 16 );
        if ( !ok )
        {
            continue;
        }

        entries.append( { match.captured( 1 ), code } );
    }

    return entries;
}
