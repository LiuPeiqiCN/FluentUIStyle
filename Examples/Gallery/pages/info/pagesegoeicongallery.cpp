#include "pagesegoeicongallery.h"

#include <QClipboard>
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QRegularExpression>
#include <QTableWidget>
#include <QTextStream>
#include <QVBoxLayout>

#include <exinfobar.h>
#include <exinfobarhost.h>
#include <functional>

#include "font-icon/fonticon.h"


namespace {

class IconCardWidget final : public QWidget
{
public:
    IconCardWidget( QString name, QString codeText, QWidget* parent = nullptr )
        : QWidget( parent )
        , m_name( std::move( name ) )
        , m_codeText( std::move( codeText ) )
    {
        setCursor( Qt::PointingHandCursor );
        setAttribute( Qt::WA_Hover, true );
    }

    std::function<void( const QString& name, const QString& codeText )> onClicked;

protected:
    bool event( QEvent* event ) override
    {
        if ( event->type() == QEvent::HoverEnter )
        {
            m_hovered = true;
            update();
        }
        else if ( event->type() == QEvent::HoverLeave )
        {
            m_hovered = false;
            m_pressed = false;
            update();
        }
        return QWidget::event( event );
    }

    void mousePressEvent( QMouseEvent* event ) override
    {
        if ( event->button() == Qt::LeftButton )
        {
            m_pressed = true;
            update();
        }
        QWidget::mousePressEvent( event );
    }

    void mouseReleaseEvent( QMouseEvent* event ) override
    {
        if ( m_pressed && event->button() == Qt::LeftButton )
        {
            m_pressed = false;
            update();
            if ( rect().contains( event->pos() ) && onClicked )
            {
                onClicked( m_name, m_codeText );
            }
        }
        QWidget::mouseReleaseEvent( event );
    }

    void paintEvent( QPaintEvent* event ) override
    {
        QWidget::paintEvent( event );
        if ( m_hovered || m_pressed )
        {
            QPainter painter( this );
            painter.setRenderHint( QPainter::Antialiasing );
            const bool darkLike = palette().color( QPalette::Base ).lightness() < 128;
            QColor bgColor      = darkLike ? QColor( 255, 255, 255 ) : QColor( 0, 0, 0 );
            bgColor.setAlpha( m_pressed ? 22 : 12 );
            painter.setBrush( bgColor );
            painter.setPen( Qt::NoPen );
            painter.drawRoundedRect( rect().adjusted( 2, 2, -2, -2 ), 6.0, 6.0 );
        }
    }

private:
    QString m_name;
    QString m_codeText;
    bool m_hovered = false;
    bool m_pressed = false;
};

}  // namespace

PageSegoeIconGallery::PageSegoeIconGallery( QWidget* parent )
    : QFrame( parent )
{
    setFrameShape( QFrame::StyledPanel );

    initializeUi();
    populateTable();
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

    m_tableWidget = new QTableWidget( this );
    m_tableWidget->setBackgroundRole( QPalette::Base );
    m_tableWidget->setAutoFillBackground( true );
    m_tableWidget->setColumnCount( 10 );
    m_tableWidget->setEditTriggers( QAbstractItemView::NoEditTriggers );
    m_tableWidget->setSelectionMode( QAbstractItemView::NoSelection );
    m_tableWidget->setShowGrid( false );
    m_tableWidget->setFocusPolicy( Qt::NoFocus );
    m_tableWidget->setFrameShape( QFrame::NoFrame );
    m_tableWidget->setWordWrap( true );
    m_tableWidget->setVerticalScrollMode( QAbstractItemView::ScrollPerPixel );
    m_tableWidget->verticalHeader()->setVisible( false );
    m_tableWidget->horizontalHeader()->setVisible( false );
    m_tableWidget->horizontalHeader()->setDefaultSectionSize( 110 );
    m_tableWidget->horizontalHeader()->setMinimumSectionSize( 78 );

    QHBoxLayout* topBarLayout = new QHBoxLayout();
    topBarLayout->setContentsMargins( 0, 0, 0, 0 );
    topBarLayout->addWidget( titleLabel );
    topBarLayout->addStretch();
    topBarLayout->addWidget( m_searchEdit );

    connect( m_searchEdit, &QLineEdit::textChanged, this, [ this ]( const QString& text ) { populateTable( text ); } );

    layout->addLayout( topBarLayout );
    layout->addWidget( m_tableWidget, 1 );
}

void PageSegoeIconGallery::populateTable( const QString& keyword )
{
    const QList<IconEntry> entries = iconEntries();
    const QString filter           = keyword.trimmed().toLower();

    QList<int> matchedIndexes;
    matchedIndexes.reserve( entries.size() );
    for ( int i = 0; i < entries.size(); ++i )
    {
        const QString name          = entries[ i ].name.toLower();
        const QString hexCode       = QString::number( entries[ i ].code, 16 ).toLower();
        const QString hexWithPrefix = QStringLiteral( "0x" ) + hexCode;
        if ( filter.isEmpty() || name.contains( filter ) || hexCode.contains( filter ) || hexWithPrefix.contains( filter ) )
        {
            matchedIndexes.append( i );
        }
    }

    const int columnCount = m_tableWidget->columnCount();
    const int rowCount    = ( matchedIndexes.size() + columnCount - 1 ) / columnCount;
    m_tableWidget->clearContents();
    m_tableWidget->setRowCount( rowCount );

    const bool darkLike = palette().color( QPalette::Base ).lightness() < 128;

    for ( int row = 0; row < rowCount; ++row )
    {
        m_tableWidget->setRowHeight( row, 100 );
    }

    for ( int i = 0; i < matchedIndexes.size(); ++i )
    {
        const IconEntry& entry = entries[ matchedIndexes.at( i ) ];
        const int row          = i / columnCount;
        const int col          = i % columnCount;

        const QString codeText =
            QStringLiteral( "0x%1" ).arg( QString::number( entry.code, 16 ).toUpper().rightJustified( 4, QLatin1Char( '0' ) ) );

        IconCardWidget* cellWidget = new IconCardWidget( entry.name, codeText, m_tableWidget );
        cellWidget->setToolTip( QStringLiteral( "%1\n%2\n点击复制编码" ).arg( entry.name, codeText ) );

        QVBoxLayout* cellLayout = new QVBoxLayout( cellWidget );
        cellLayout->setContentsMargins( 4, 4, 4, 4 );
        cellLayout->setSpacing( 2 );

        QLabel* iconLabel = new QLabel( cellWidget );
        iconLabel->setAttribute( Qt::WA_TransparentForMouseEvents, true );
        iconLabel->setAlignment( Qt::AlignCenter );
        QFont iconFont( QString::fromLatin1( SegoeIcon::SegoeFontName ) );
        iconFont.setPixelSize( 30 );
        iconFont.setHintingPreference( QFont::PreferNoHinting );
        iconLabel->setFont( iconFont );
        iconLabel->setText( QString( QChar( static_cast<char16_t>( entry.code ) ) ) );
        iconLabel->setMinimumHeight( 36 );

        QLabel* nameLabel = new QLabel( entry.name, cellWidget );
        nameLabel->setAttribute( Qt::WA_TransparentForMouseEvents, true );
        nameLabel->setAlignment( Qt::AlignHCenter | Qt::AlignTop );
        nameLabel->setWordWrap( true );

        QLabel* codeLabel = new QLabel( codeText, cellWidget );
        codeLabel->setAttribute( Qt::WA_TransparentForMouseEvents, true );
        codeLabel->setAlignment( Qt::AlignHCenter | Qt::AlignTop );
        QFont codeFont = codeLabel->font();
        codeFont.setPixelSize( 11 );
        codeLabel->setFont( codeFont );

        QPalette codePalette = codeLabel->palette();
        QColor subColor      = codePalette.color( QPalette::WindowText );
        subColor.setAlpha( darkLike ? 160 : 130 );
        codePalette.setColor( QPalette::WindowText, subColor );
        codeLabel->setPalette( codePalette );

        cellLayout->addWidget( iconLabel );
        cellLayout->addWidget( nameLabel );
        cellLayout->addWidget( codeLabel );

        cellWidget->onClicked = [ this ]( const QString& name, const QString& code )
        {
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
        };

        m_tableWidget->setCellWidget( row, col, cellWidget );
    }
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
