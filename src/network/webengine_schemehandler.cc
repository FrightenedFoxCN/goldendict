/* This file is (c) 2026 GoldenDict contributors
 * Part of GoldenDict. Licensed under GPLv3 or later, see the LICENSE file */

#include "webengine_schemehandler.hh"

#include <QBuffer>
#include <QFile>
#include <QMimeDatabase>
#include <QWebEngineUrlRequestJob>
#include <cstring>

namespace {

QString qrcPathFromUrl( QUrl const & url )
{
  QString path = url.path();
  if ( path.startsWith( '/' ) )
    path.remove( 0, 1 );
  return ":/" + path;
}

}

ArticleUrlSchemeHandler::ArticleUrlSchemeHandler( ArticleNetworkAccessManager & netMgr_,
                                                  QObject * parent ):
  QWebEngineUrlSchemeHandler( parent ),
  netMgr( netMgr_ )
{
}

void ArticleUrlSchemeHandler::requestStarted( QWebEngineUrlRequestJob * job )
{
  if ( !job )
    return;

  const QUrl url = job->requestUrl();

  if ( url.scheme() == "qrcx" )
  {
    replyWithQrc( job, url );
    return;
  }

  QString contentType;
  sptr< Dictionary::DataRequest > req = netMgr.getResource( url, contentType );

  if ( !req.get() )
  {
    job->fail( QWebEngineUrlRequestJob::UrlNotFound );
    return;
  }

  if ( req->isFinished() )
  {
    replyWithData( job, req, contentType );
    return;
  }

  PendingRequest pendingReq;
  pendingReq.job = job;
  pendingReq.request = req;
  pendingReq.mimeType = contentType;

  pending.insert( req.get(), pendingReq );

  connect( req.get(), SIGNAL( finished() ), this, SLOT( handleRequestFinished() ) );
}

void ArticleUrlSchemeHandler::handleRequestFinished()
{
  Dictionary::DataRequest * request = qobject_cast< Dictionary::DataRequest * >( sender() );
  if ( !request )
    return;

  if ( !pending.contains( request ) )
    return;

  PendingRequest pendingReq = pending.take( request );
  if ( !pendingReq.job )
    return;

  replyWithData( pendingReq.job, pendingReq.request, pendingReq.mimeType );
}

void ArticleUrlSchemeHandler::replyWithData( QWebEngineUrlRequestJob * job,
                                             sptr< Dictionary::DataRequest > const & request,
                                             QString const & mimeType )
{
  if ( !job )
    return;

  if ( request->dataSize() < 0 )
  {
    job->fail( QWebEngineUrlRequestJob::UrlNotFound );
    return;
  }

  std::vector< char > const & raw = request->getFullData();

  QByteArray payload;
  payload.resize( raw.size() );
  if ( !raw.empty() )
    memcpy( payload.data(), raw.data(), raw.size() );

  QBuffer * buffer = new QBuffer( job );
  buffer->setData( payload );
  buffer->open( QIODevice::ReadOnly );

  const QString type = mimeType.isEmpty() ? guessMimeType( job->requestUrl().path() ) : mimeType;
  job->reply( type.toUtf8(), buffer );
}

void ArticleUrlSchemeHandler::replyWithQrc( QWebEngineUrlRequestJob * job, QUrl const & url )
{
  if ( !job )
    return;

  const QString path = qrcPathFromUrl( url );
  QFile * file = new QFile( path, job );
  if ( !file->open( QIODevice::ReadOnly ) )
  {
    job->fail( QWebEngineUrlRequestJob::UrlNotFound );
    return;
  }

  job->reply( guessMimeType( path ).toUtf8(), file );
}

QString ArticleUrlSchemeHandler::guessMimeType( QString const & path )
{
  QMimeDatabase db;
  QMimeType type = db.mimeTypeForFile( path, QMimeDatabase::MatchExtension );
  if ( type.isValid() )
    return type.name();

  return "application/octet-stream";
}
