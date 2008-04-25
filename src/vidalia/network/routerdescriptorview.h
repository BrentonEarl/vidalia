/*
**  This file is part of Vidalia, and is subject to the license terms in the
**  LICENSE file, found in the top level directory of this distribution. If you
**  did not receive the LICENSE file with this file, you may obtain it from the
**  Vidalia source package distributed by the Vidalia Project at
**  http://www.vidalia-project.net/. No part of Vidalia, including this file,
**  may be copied, modified, propagated, or distributed except according to the
**  terms described in the LICENSE file.
*/

/*
** \file routerdescriptorview.h
** \version $Id$
** \brief Formats and displays a router descriptor as HTML
*/

#ifndef _ROUTERDESCRIPTORVIEW_H
#define _ROUTERDESCRIPTORVIEW_H

#include <QObject>
#include <QTextEdit>
#include <QList>

#include <routerdescriptor.h>


class RouterDescriptorView : public QTextEdit
{
  Q_OBJECT

public:
  /** Default constructor. */
  RouterDescriptorView(QWidget *parent = 0);

public slots:
  /** Shows the given router descriptor. */
  void display(RouterDescriptor rd);
  /** Shows all router descriptors in the given list. */
  void display(QList<RouterDescriptor> rdlist);
  
private:
  /** Adjusts the displayed uptime to include time since the
   * router's descriptor was last published. */
  quint64 adjustUptime(quint64 uptime, QDateTime published);
  /** Formats the descriptor's published date. */
  QString formatPublished(QDateTime date);
  /** Formats the router's uptime. */
  QString formatUptime(quint64 seconds);
  /** Formats the observed bandwidth into KB/s. */
  QString formatBandwidth(quint64 bandwidth);
};

#endif
