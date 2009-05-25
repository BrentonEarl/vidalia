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
** \file messagelog.cpp
** \version $Id$
** \brief Displays log messages and message log settings
*/

#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QClipboard>
#include <vidalia.h>
#include <html.h>
#include <vmessagebox.h>

#include "messagelog.h"

/* Message log settings */
#define SETTING_MSG_FILTER          "MessageFilter"
#define SETTING_MAX_MSG_COUNT       "MaxMsgCount"
#define SETTING_ENABLE_LOGFILE      "EnableLogFile"
#define SETTING_LOGFILE             "LogFile"
#define DEFAULT_MSG_FILTER \
  (LogEvent::Error|LogEvent::Warn|LogEvent::Notice)
#define DEFAULT_MAX_MSG_COUNT       50
#define DEFAULT_ENABLE_LOGFILE      false
#if defined(Q_OS_WIN32)

/** Default location of the log file to which log messages will be written. */
#define DEFAULT_LOGFILE \
  (win32_program_files_folder()+"\\Tor\\tor-log.txt")
#else
#define DEFAULT_LOGFILE       (QDir::homePath() + "/.tor/tor-log.txt")
#endif

#define ADD_TO_FILTER(f,v,b)  (f = ((b) ? ((f) | (v)) : ((f) & ~(v))))

/** Constructor. The constructor will load the message log's settings from
 * VidaliSettings and register for log events according to the most recently
 * set severity filter. 
 * \param torControl A TorControl object used to register for log events.
 * \param parent The parent widget of this MessageLog object.
 * \param flags Any desired window creation flags. 
 */
MessageLog::MessageLog(QWidget *parent, Qt::WFlags flags)
: VidaliaWindow("MessageLog", parent, flags)
{
  /* Invoke Qt Designer generated QObject setup routine */
  ui.setupUi(this);

  /* Create necessary Message Log QObjects */
  _torControl = Vidalia::torControl();
 
  /* Bind events to actions */
  createActions();

  /* Set tooltips for necessary widgets */
  setToolTips();
  
  /* Load the message log's stored settings */
  loadSettings();

  /* Sort in ascending chronological order */ 
  ui.lstMessages->sortItems(LogTreeWidget::TimeColumn, 
                            Qt::AscendingOrder);
}

/** Default Destructor. Simply frees up any memory allocated for member
 * variables. */
MessageLog::~MessageLog()
{
  _torControl->setLogEvents(0, this);
  _logFile.close();
}

/** Binds events (signals) to actions (slots). */
void
MessageLog::createActions()
{
  connect(ui.actionSave_Selected, SIGNAL(triggered()), 
      this, SLOT(saveSelected()));
  
  connect(ui.actionSave_All, SIGNAL(triggered()), 
      this, SLOT(saveAll()));
  
  connect(ui.actionCopy, SIGNAL(triggered()),
      this, SLOT(copy()));

  connect(ui.actionFind, SIGNAL(triggered()),
      this, SLOT(find()));

  connect(ui.actionHelp, SIGNAL(triggered()),
      this, SLOT(help()));
  
  connect(ui.btnSaveSettings, SIGNAL(clicked()),
      this, SLOT(saveSettings()));

  connect(ui.btnCancelSettings, SIGNAL(clicked()),
      this, SLOT(cancelChanges()));

  connect(ui.btnBrowse, SIGNAL(clicked()),
      this, SLOT(browse()));

#if defined(Q_WS_MAC)
  ui.actionHelp->setShortcut(QString("Ctrl+?"));
#endif
  ui.actionClose->setShortcut(QString("Esc"));
  Vidalia::createShortcut("Ctrl+W", this, ui.actionClose, SLOT(trigger()));
}

/** Set tooltips for Message Filter checkboxes in code because they are long
 * and Designer wouldn't let us insert newlines into the text. */
void
MessageLog::setToolTips()
{
  ui.chkTorErr->setToolTip(tr("Messages that appear when something has \n"
                              "gone very wrong and Tor cannot proceed."));
  ui.chkTorWarn->setToolTip(tr("Messages that only appear when \n"
                               "something has gone wrong with Tor."));
  ui.chkTorNote->setToolTip(tr("Messages that appear infrequently \n"
                               "during normal Tor operation and are \n"
                               "not considered errors, but you may \n"
                               "care about."));
  ui.chkTorInfo->setToolTip(tr("Messages that appear frequently \n"
                               "during normal Tor operation."));
  ui.chkTorDebug->setToolTip(tr("Hyper-verbose messages primarily of \n"
                                "interest to Tor developers.")); 
}

/** Loads the saved Message Log settings */
void
MessageLog::loadSettings()
{
  /* Set Max Count widget */
  uint maxMsgCount = getSetting(SETTING_MAX_MSG_COUNT,
                                DEFAULT_MAX_MSG_COUNT).toUInt();
  ui.spnbxMaxCount->setValue(maxMsgCount);
  ui.lstMessages->setMaximumMessageCount(maxMsgCount);

  /* Set whether or not logging to file is enabled */
  _enableLogging = getSetting(SETTING_ENABLE_LOGFILE,
                              DEFAULT_ENABLE_LOGFILE).toBool();
  QString logfile = getSetting(SETTING_LOGFILE,
                               DEFAULT_LOGFILE).toString();
  ui.lineFile->setText(QDir::convertSeparators(logfile));
  rotateLogFile(logfile);
  ui.chkEnableLogFile->setChecked(_logFile.isOpen());

  /* Set the checkboxes accordingly */
  _filter = getSetting(SETTING_MSG_FILTER, DEFAULT_MSG_FILTER).toUInt();
  ui.chkTorErr->setChecked(_filter & LogEvent::Error);
  ui.chkTorWarn->setChecked(_filter & LogEvent::Warn);
  ui.chkTorNote->setChecked(_filter & LogEvent::Notice);
  ui.chkTorInfo->setChecked(_filter & LogEvent::Info);
  ui.chkTorDebug->setChecked(_filter & LogEvent::Debug);
  registerLogEvents();
 
  /* Filter the message log */
  QApplication::setOverrideCursor(Qt::WaitCursor);
  ui.lstMessages->filter(_filter);
  QApplication::restoreOverrideCursor();
}

/** Attempts to register the selected message filter with Tor and displays an
 * error if setting the events fails. */
void
MessageLog::registerLogEvents()
{
  QString errmsg;
  _filter = getSetting(SETTING_MSG_FILTER, DEFAULT_MSG_FILTER).toUInt();
  if (!_torControl->setLogEvents(_filter, this, &errmsg)) {
    VMessageBox::warning(this, tr("Error Setting Filter"),
      p(tr("Vidalia was unable to register for Tor's log events.")) + p(errmsg),
      VMessageBox::Ok);
  }
}

/** Opens a log file if necessary, or closes it if logging is disabled. If a
 * log file is already opened and a new filename is specified, then the log
 * file will be rotated to the new filename. In the case that the new filename
 * can not be openend, the old file will remain open and writable. */
bool
MessageLog::rotateLogFile(QString filename)
{
  QString errmsg;
  if (_enableLogging) {
    if (!_logFile.open(filename, &errmsg)) {
      VMessageBox::warning(this, tr("Error Opening Log File"),
        p(tr("Vidalia was unable to open the specified log file."))+p(errmsg),
        VMessageBox::Ok);
      return false;
    }
  } else {
    /* Close the log file. */
    _logFile.close();
  }
  return true;
}

/** Saves the Message Log settings, adjusts the message list if required, and
 * then hides the settings frame. */
void
MessageLog::saveSettings()
{
  /* Update the logging status */
  _enableLogging = ui.chkEnableLogFile->isChecked();
  if (_enableLogging && ui.lineFile->text().isEmpty()) {
    /* The user chose to enable logging messages to a file, but didn't specify
     * a log filename. */
    VMessageBox::warning(this, tr("Log Filename Required"),
      p(tr("You must enter a filename to be able to save log "
           "messages to a file.")), VMessageBox::Ok);
    return;
  }
  if (rotateLogFile(ui.lineFile->text())) {
    saveSetting(SETTING_LOGFILE, ui.lineFile->text());
    saveSetting(SETTING_ENABLE_LOGFILE, _logFile.isOpen());
  }
  ui.lineFile->setText(QDir::convertSeparators(ui.lineFile->text()));
  ui.chkEnableLogFile->setChecked(_logFile.isOpen());

  /* Update the maximum displayed item count */
  saveSetting(SETTING_MAX_MSG_COUNT, ui.spnbxMaxCount->value());
  ui.lstMessages->setMaximumMessageCount(ui.spnbxMaxCount->value());
  
  /* Save message filter and refilter the list */
  uint filter = 0;
  ADD_TO_FILTER(filter, LogEvent::Error, ui.chkTorErr->isChecked());
  ADD_TO_FILTER(filter, LogEvent::Warn, ui.chkTorWarn->isChecked());
  ADD_TO_FILTER(filter, LogEvent::Notice, ui.chkTorNote->isChecked());
  ADD_TO_FILTER(filter, LogEvent::Info, ui.chkTorInfo->isChecked());
  ADD_TO_FILTER(filter, LogEvent::Debug, ui.chkTorDebug->isChecked());
  saveSetting(SETTING_MSG_FILTER, filter);
  registerLogEvents();
  
  /* Filter the message log */
  QApplication::setOverrideCursor(Qt::WaitCursor);
  ui.lstMessages->filter(_filter);
  QApplication::restoreOverrideCursor();
   
  /* Hide the settings frame and reset toggle button*/
  ui.actionSettings->toggle(); 
}

/** Simply restores the previously saved settings and hides the settings
 * frame. */
void 
MessageLog::cancelChanges()
{
  /* Hide the settings frame and reset toggle button */
  ui.actionSettings->toggle();
  /* Reload the settings */
  loadSettings();
}

/** Called when the user clicks "Browse" to select a new log file. */
void
MessageLog::browse()
{
  /* Strangely, QFileDialog returns a non seperator converted path. */
  QString filename = QDir::convertSeparators(
                          QFileDialog::getSaveFileName(this,
                              tr("Select Log File"), "tor-log.txt"));
  if (!filename.isEmpty()) {
    ui.lineFile->setText(filename);
  }
}

/** Saves the given list of items to a file.
 * \param items A list of log message items to save. 
 */
void
MessageLog::save(QStringList messages)
{
  if (!messages.size()) {
    return;
  }

  QString fileName = QFileDialog::getSaveFileName(this,
                          tr("Save Log Messages"),
                          "VidaliaLog-" + 
                          QDateTime::currentDateTime().toString("MM.dd.yyyy")
                          + ".txt", tr("Text Files (*.txt)"));
  
  /* If the choose to save */
  if (!fileName.isEmpty()) {
    LogFile logFile;
    QString errmsg;
    
    /* If can't write to file, show error message */
    if (!logFile.open(fileName, &errmsg)) {
      VMessageBox::warning(this, tr("Vidalia"),
                           p(tr("Cannot write file %1\n\n%2."))
                                                .arg(fileName)
                                                .arg(errmsg),
                           VMessageBox::Ok);
      return;
    }
   
    /* Write out the message log to the file */
    QApplication::setOverrideCursor(Qt::WaitCursor);
    foreach (QString msg, messages) {
      logFile << msg;
    }
    QApplication::restoreOverrideCursor();
  }
}

/** Saves currently selected messages to a file. */
void
MessageLog::saveSelected()
{
  save(ui.lstMessages->selectedMessages());
}

/** Saves all shown messages to a file. */
void
MessageLog::saveAll()
{
  save(ui.lstMessages->allMessages());
}

/** Copies contents of currently selected messages to the 'clipboard'. */
void
MessageLog::copy()
{
  QString contents = ui.lstMessages->selectedMessages().join("");
  if (!contents.isEmpty()) {
    /* Copy the selected messages to the clipboard */
    QApplication::clipboard()->setText(contents);
  }
}

/** Prompts the user for a search string. If the search string is not found in
 * any of the currently displayed log entires, then a message will be
 * displayed for the user informing them that no matches were found. 
 * \sa search()
 */
void
MessageLog::find()
{
  bool ok;
  QString text = QInputDialog::getText(this, tr("Find in Message Log"),
                  tr("Find:"), QLineEdit::Normal, QString(), &ok);
  
  if (ok && !text.isEmpty()) {
    /* Search for the user-specified text */
    QList<LogTreeItem *> results = ui.lstMessages->find(text);
    if (!results.size()) {
      VMessageBox::information(this, tr("Not Found"), 
                               p(tr("Search found 0 matches.")), 
                               VMessageBox::Ok);
    } else {
      /* Set the focus to the first match */
      ui.lstMessages->scrollToItem(results.at(0));
    }
  }
}

/** Writes a message to the Message History and tags it with
 * the proper date, time and type.
 * \param type The message's severity type.
 * \param message The log message to be added.
 */
void 
MessageLog::log(LogEvent::Severity type, QString message)
{
  /* Only add the message if it's not being filtered out */
  if (_filter & (uint)type) {
    /* Add the message to the list and scroll to it if necessary. */
    LogTreeItem *item = ui.lstMessages->log(type, message); 
   
    /* This is a workaround to force Qt to update the statusbar text (if any
     * is currently displayed) to reflect the new message added. */
    QString currStatusTip = ui.statusbar->currentMessage();
    if (!currStatusTip.isEmpty()) {
      currStatusTip = ui.lstMessages->statusTip();
      ui.statusbar->showMessage(currStatusTip);
    }
    
    /* If we're saving log messages to a file, go ahead and do that now */
    if (_enableLogging) {
      _logFile << item->toString();
    }
  }
}

/** Custom event handler. Checks if the event is a log event. If it is, then
 * it will write the message to the message log. 
 * \param event The custom log event. 
 */
void
MessageLog::customEvent(QEvent *event)
{
  if (event->type() == CustomEventType::LogEvent) {
    LogEvent *e = (LogEvent *)event;
    log(e->severity(), e->message());
    e->accept();
  }
}

/** Displays help information about the message log. */
void
MessageLog::help()
{
  emit helpRequested("log");
}

