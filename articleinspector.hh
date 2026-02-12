#ifndef ARTICLEINSPECTOR_HH
#define ARTICLEINSPECTOR_HH

#include <QtGlobal>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)

#include <QPointer>
#include <QWidget>
#include <QWebEnginePage>
#include <QWebEngineView>
#include <list>
#include "config.hh"
#include "ex.hh"

class ArticleInspector : public QWidget
{
Q_OBJECT

public:
  DEF_EX( exInit, "Article inspector failed to init", std::exception )

  explicit ArticleInspector( Config::Class * cfg, QWidget * parent = 0 );
  ~ArticleInspector();

  void setInspectedPage( QWebEnginePage * page );

public slots:
  void beforeClosed();

protected:
  void showEvent( QShowEvent *event );

private:
  Config::Class * cfg;
  QWebEngineView * view;
  QWebEnginePage * devToolsPage;
  QPointer< QWebEnginePage > inspectedPage;

  static std::list< ArticleInspector * > openedInspectors;
};

#elif QT_VERSION >= 0x040600

#include <QWebInspector>
#include <list>
#include "config.hh"
#include "ex.hh"

class ArticleInspector : public QWebInspector
{
Q_OBJECT

public:
  DEF_EX( exInit, "Article inspector failed to init", std::exception )

  explicit ArticleInspector( Config::Class * cfg,  QWidget* parent = 0 );
  ~ArticleInspector();

public slots:
  void beforeClosed();

protected:
  void showEvent( QShowEvent *event );

private:
  Config::Class * cfg;

  static std::list< ArticleInspector * > openedInspectors;
};

#endif // QT_VERSION

#endif // ARTICLEINSPECTOR_HH
