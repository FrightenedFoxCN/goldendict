/* Thin wrappers for retaining compatibility for Qt6.x */

#ifndef QT4X5_HH
#define QT4X5_HH

#define IS_QT_4    0
#define IS_QT_5    0
#define IS_QT_6    1

#include <QProcess>
#include <QString>
#include <QAtomicInt>
#include <QTextDocument>
#include <QUrl>
#include <QUrlQuery>

namespace Qt4x5
{

inline Qt::SplitBehaviorFlags keepEmptyParts()
{ return Qt::KeepEmptyParts; }
inline Qt::SplitBehaviorFlags skipEmptyParts()
{ return Qt::SkipEmptyParts; }

inline Qt::MouseButton middleButton()
{
  return Qt::MiddleButton;
}

inline QString escape( QString const & plain )
{
  return plain.toHtmlEscaped();
}

namespace AtomicInt
{

inline int loadAcquire( QAtomicInt const & ref )
{
  return ref.loadAcquire();
}

}

namespace Url
{

// This wrapper is created due to behavior change of the setPath() method
// See: https://bugreports.qt-project.org/browse/QTBUG-27728
//       https://codereview.qt-project.org/#change,38257
inline QString ensureLeadingSlash( const QString & path )
{
  QLatin1Char slash( '/' );
  if ( path.startsWith( slash ) )
    return path;
  return slash + path;
}

inline bool hasQueryItem( QUrl const & url, QString const & key )
{
  return QUrlQuery( url ).hasQueryItem( key );
}

inline QString queryItemValue( QUrl const & url, QString const & item )
{
  return QUrlQuery( url ).queryItemValue( item, QUrl::FullyDecoded );
}

inline QByteArray encodedQueryItemValue( QUrl const & url, QString const & item )
{
  return QUrlQuery( url ).queryItemValue( item, QUrl::FullyEncoded ).toLatin1();
}

inline void addQueryItem( QUrl & url, QString const & key, QString const & value )
{
  QUrlQuery urlQuery( url );
  urlQuery.addQueryItem( key, value );
  url.setQuery( urlQuery );
}

inline void removeQueryItem( QUrl & url, QString const & key )
{
  QUrlQuery urlQuery( url );
  urlQuery.removeQueryItem( key );
  url.setQuery( urlQuery );
}

inline QString fullPath( QUrl const & url )
{
  QString path = url.path( QUrl::FullyDecoded );
  if( url.hasQuery() )
  {
    QUrlQuery urlQuery( url );
    path += QString::fromLatin1( "?" ) + urlQuery.toString( QUrl::FullyDecoded );
  }
  return path;
}

inline void setQueryItems( QUrl & url, QList< QPair< QString, QString > > const & query )
{
  QUrlQuery urlQuery( url );
  urlQuery.setQueryItems( query );
  url.setQuery( urlQuery );
}

inline QString path( QUrl const & url )
{
  return url.path( QUrl::FullyDecoded );
}

inline void setFragment( QUrl & url, const QString & fragment )
{
  url.setFragment( fragment, QUrl::DecodedMode );
}

inline QString fragment( const QUrl & url )
{
  return url.fragment( QUrl::FullyDecoded );
}

}

namespace Dom
{

typedef int size_type;

}

namespace Process
{

inline bool startDetached( QString const & command )
{
  auto args = QProcess::splitCommand( command );
  if( args.empty() )
    return false;
  auto const program = args.takeFirst();
  return QProcess::startDetached( program, args );
}

}

}
#endif // QT4X5_HH