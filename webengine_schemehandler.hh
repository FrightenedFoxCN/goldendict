/* This file is (c) 2026 GoldenDict contributors
 * Part of GoldenDict. Licensed under GPLv3 or later, see the LICENSE file */

#ifndef WEBENGINE_SCHEMEHANDLER_HH
#define WEBENGINE_SCHEMEHANDLER_HH

#include <QHash>
#include <QPointer>
#include <QString>
#include <QWebEngineUrlSchemeHandler>

#include "article_netmgr.hh"
#include "dictionary.hh"

class QWebEngineUrlRequestJob;

class ArticleUrlSchemeHandler : public QWebEngineUrlSchemeHandler
{
  Q_OBJECT

public:
  explicit ArticleUrlSchemeHandler( ArticleNetworkAccessManager & netMgr,
                                    QObject * parent = 0 );

  void requestStarted( QWebEngineUrlRequestJob * job ) override;

private slots:
  void handleRequestFinished();

private:
  struct PendingRequest
  {
    QPointer< QWebEngineUrlRequestJob > job;
    sptr< Dictionary::DataRequest > request;
    QString mimeType;
  };

  ArticleNetworkAccessManager & netMgr;
  QHash< Dictionary::DataRequest *, PendingRequest > pending;

  void replyWithData( QWebEngineUrlRequestJob * job,
                      sptr< Dictionary::DataRequest > const & request,
                      QString const & mimeType );
  void replyWithQrc( QWebEngineUrlRequestJob * job, QUrl const & url );
  static QString guessMimeType( QString const & path );
};

#endif
