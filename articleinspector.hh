#ifndef ARTICLEINSPECTOR_HH
#define ARTICLEINSPECTOR_HH

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

#endif // ARTICLEINSPECTOR_HH
