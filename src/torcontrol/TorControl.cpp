/*
**  This file is part of Vidalia, and is subject to the license terms in the
**  LICENSE file, found in the top level directory of this distribution. If
**  you did not receive the LICENSE file with this file, you may obtain it
**  from the Vidalia source package distributed by the Vidalia Project at
**  http://www.torproject.org/projects/vidalia.html. No part of Vidalia,
**  including this file, may be copied, modified, propagated, or distributed
**  except according to the terms described in the LICENSE file.
*/

/*
** \file TorControl.cpp
** \brief Object for interacting with the Tor process and control interface
*/

#include "TorControl.h"
#include "RouterDescriptor.h"
#include "ProtocolInfo.h"
#include "RouterStatus.h"
#include "file.h"
#include "stringutil.h"

#include <QHostAddress>
#include <QVariantMap>


/** Default constructor */
TorControl::TorControl(ControlMethod::Method method)
  : QObject(), _shouldContinue(true), _reason("") {
#define RELAY_SIGNAL(src, sig) \
  QObject::connect((src), (sig), this, (sig))

  /* Create a TorEvents object to receive and parse asynchronous events
   * from Tor's control port, and relay them as external signals from
   * this TorControl object. */
  _eventHandler = new TorEvents(this);
  RELAY_SIGNAL(_eventHandler, SIGNAL(circuitEstablished()));
  RELAY_SIGNAL(_eventHandler, SIGNAL(dangerousTorVersion(tc::TorVersionStatus,
                                                         QString, QStringList)));
  RELAY_SIGNAL(_eventHandler, SIGNAL(addressMapped(QString,QString,QDateTime)));
  RELAY_SIGNAL(_eventHandler, SIGNAL(bandwidthUpdate(quint64, quint64)));
  RELAY_SIGNAL(_eventHandler, SIGNAL(circuitStatusChanged(Circuit)));
  RELAY_SIGNAL(_eventHandler, SIGNAL(streamStatusChanged(Stream)));
  RELAY_SIGNAL(_eventHandler, SIGNAL(newDescriptors(QStringList)));
  RELAY_SIGNAL(_eventHandler, SIGNAL(logMessage(tc::Severity, QString)));
  RELAY_SIGNAL(_eventHandler, SIGNAL(dangerousPort(quint16, bool)));
  RELAY_SIGNAL(_eventHandler, SIGNAL(socksError(tc::SocksError, QString)));
  RELAY_SIGNAL(_eventHandler, SIGNAL(bootstrapStatusChanged(BootstrapStatus)));
  RELAY_SIGNAL(_eventHandler, SIGNAL(clockSkewed(int, QString)));
  RELAY_SIGNAL(_eventHandler, SIGNAL(bug(QString)));
  RELAY_SIGNAL(_eventHandler, SIGNAL(dnsHijacked()));
  RELAY_SIGNAL(_eventHandler, SIGNAL(dnsUseless()));
  RELAY_SIGNAL(_eventHandler,
               SIGNAL(externalAddressChanged(QHostAddress, QString)));
  RELAY_SIGNAL(_eventHandler,
               SIGNAL(checkingOrPortReachability(QHostAddress, quint16)));
  RELAY_SIGNAL(_eventHandler,
               SIGNAL(orPortReachabilityFinished(QHostAddress,quint16,bool)));
  RELAY_SIGNAL(_eventHandler,
               SIGNAL(checkingDirPortReachability(QHostAddress, quint16)));
  RELAY_SIGNAL(_eventHandler,
               SIGNAL(dirPortReachabilityFinished(QHostAddress,quint16,bool)));
  RELAY_SIGNAL(_eventHandler,
               SIGNAL(serverDescriptorRejected(QHostAddress, quint16, QString)));
  RELAY_SIGNAL(_eventHandler,
               SIGNAL(serverDescriptorAccepted(QHostAddress, quint16)));
  RELAY_SIGNAL(_eventHandler, SIGNAL(serverDescriptorAccepted()));

  /* Create an instance of a connection to Tor's control interface and give
   * it an object to use to handle asynchronous events. */
  _controlConn = new ControlConnection(method, _eventHandler);
  RELAY_SIGNAL(_controlConn, SIGNAL(connected()));
  RELAY_SIGNAL(_controlConn, SIGNAL(connectFailed(QString)));
  QObject::connect(_controlConn, SIGNAL(disconnected()),
                   this, SLOT(onDisconnected()));

  /* Create an object used to start and stop a Tor process. */
  _torProcess = new TorProcess(this);
  RELAY_SIGNAL(_torProcess, SIGNAL(started()));
  RELAY_SIGNAL(_torProcess, SIGNAL(startFailed(QString)));
  QObject::connect(_torProcess, SIGNAL(finished(int, QProcess::ExitStatus)),
                   this, SLOT(onStopped(int, QProcess::ExitStatus)));
  QObject::connect(_torProcess, SIGNAL(log(QString, QString)),
                   this, SLOT(onLogStdout(QString, QString)));

  QObject::connect(_torProcess, SIGNAL(startFailed(QString)),
                   this, SLOT(torStartFailed(QString)));
  QObject::connect(_controlConn, SIGNAL(connectFailed(QString)),
                   this, SLOT(torConnectFailed(QString)));


#if defined(Q_OS_WIN32)
  _torService = new TorService(this);
  RELAY_SIGNAL(_torService, SIGNAL(started()));
  RELAY_SIGNAL(_torService, SIGNAL(startFailed(QString)));
  QObject::connect(_torService, SIGNAL(finished(int, QProcess::ExitStatus)),
                   this, SLOT(onStopped(int, QProcess::ExitStatus)));
#endif
#undef RELAY_SIGNAL
  _method = method;
}

/** Default destructor */
TorControl::~TorControl()
{
  /* If we started our own Tor, stop it now */
  if (isVidaliaRunningTor()) {
    stop();
  }
  disconnect();
  delete _controlConn;
}

/** Start the Tor process using the executable <b>tor</b> and the list of
 * arguments in <b>args</b>. */
void
TorControl::start(const QString &tor, const QStringList &args)
{
  if (isRunning()) {
    emit started();
  } else {
#if defined(Q_OS_WIN32)
    /* If the Tor service is installed, run that. Otherwise, start a new
     * Tor process. */
    if (TorService::isSupported() && _torService->isInstalled())
      _torService->start();
    else
      _torProcess->start(expand_filename(tor), args);
#else
    /* Start a new Tor process */
    _torProcess->start(expand_filename(tor), args);
#endif
  }
}

/** Returns true if the process is running */
bool
TorControl::torStarted()
{
  return _torProcess->state() == QProcess::Running;
}

/** Called when starting the tor process failed */
void
TorControl::torStartFailed(QString errmsg)
{
  _shouldContinue = false;
  _reason = tr("Start failed: %1").arg(errmsg);
}

/** Returns true if the bootstrap should continue. If it returns
 * false, it also sets the errmsg to the last error */
bool
TorControl::shouldContinue(QString *errmsg)
{
  if(not errmsg)
    errmsg = new QString();

  *errmsg = _reason;
  return _shouldContinue;
}

/** Stop the Tor process. */
bool
TorControl::stop(QString *errmsg)
{
  bool rc = false;

  if(!errmsg)
    errmsg = new QString();

  if (!isVidaliaRunningTor()) {
    *errmsg = tr("Vidalia has not started Tor. "
        "You need to stop Tor through the interface you started it.");
    return rc;
  } else {
    if (_controlConn->isConnected())
      rc = signal(TorSignal::Halt, errmsg);
    if (!rc)
      rc = _torProcess->stop(errmsg);
  }

  return rc;
}

/** Emits a signal that the Tor process stopped */
void
TorControl::onStopped(int exitCode, QProcess::ExitStatus exitStatus)
{
  if (_controlConn->status() == ControlConnection::Connecting)
    _controlConn->cancelConnect();

  if (exitStatus == QProcess::CrashExit || exitCode != 0) {
    _shouldContinue = false;
    _reason = tr("Process finished: ExitCode=%1").arg(exitCode);
  }

  emit stopped();
  disconnect();
  emit stopped(exitCode, exitStatus);
}

/** Returns true if the tor process has finished, along with its exit
 * code and status */
bool
TorControl::finished(int *exitCode, QProcess::ExitStatus *exitStatus)
{
  *exitCode = _torProcess->exitCode();
  *exitStatus = _torProcess->exitStatus();
  return _torProcess->state() == QProcess::NotRunning;
}

/** Detects if the Tor process is running under Vidalia. Returns true if
 * Vidalia owns the Tor process, or false if it was an independent Tor. */
bool
TorControl::isVidaliaRunningTor()
{
  return (_torProcess->state() != QProcess::NotRunning);
}

/** Detect if the Tor process is running. */
bool
TorControl::isRunning()
{
  return (_torProcess->state() != QProcess::NotRunning
            || _controlConn->isConnected());
}

/** Stops reading log messages from the Tor process's stdout. This has no
 * effect if isVidaliaRunningTor() is false. */
void
TorControl::closeTorStdout()
{
  if (_torProcess)
    _torProcess->closeStdout();
}

/** Called when Tor has printed a log message to stdout. */
void
TorControl::onLogStdout(const QString &severity, const QString &message)
{
  emit logMessage(tc::severityFromString(severity), message.trimmed());
}

/** Connect to Tor's control port. The control port to use is determined by
 * Vidalia's configuration file. */
void
TorControl::connect(const QHostAddress &address, quint16 port)
{
  _controlConn->connect(address, port);
}

/** Connect to Tor's control socket. The control socket to use is determined by
 * Vidalia's configuration file. */
void
TorControl::connect(const QString &path)
{
  _controlConn->connect(path);
}

/** Called when the connection to tor has failed */
void
TorControl::torConnectFailed(QString errmsg)
{
  _shouldContinue = false;
  _reason = tr("Connection failed: %1").arg(errmsg);
}

/** Disconnect from Tor's control port */
void
TorControl::disconnect()
{
  if (isConnected())
    _controlConn->disconnect();
}

/** Emits the proper bootstrapStatusChanged */
void
TorControl::getBootstrapPhase()
{
  ControlCommand cmd("GETINFO", "status/bootstrap-phase");
  ControlReply reply;
  QString str;

  if (!send(cmd, reply, &str)) {
    return;
  }

  bool ok;
  QHash<QString, QString> args;
  args = string_parse_keyvals(reply.getMessage(), &ok);

  if(!ok)
    return;

  tc::Severity severity = tc::severityFromString(args.value("status/bootstrap-phase"));
  BootstrapStatus status
    = BootstrapStatus(severity,
                      BootstrapStatus::statusFromString(args.value("TAG")),
                      args.value("PROGRESS").toInt(),
                      args.value("SUMMARY"));
  emit bootstrapStatusChanged(status);
}

/** Emits a signal that the control socket disconnected from Tor */
void
TorControl::onDisconnected()
{
  if (_torProcess) {
    /* If we're running a Tor process, then start reading logs from stdout
     * again, in case our control connection just died but Tor is still
     * running. In this case, there may be relevant information in the logs. */
    _torProcess->openStdout();
  }
  /* Tor isn't running, so it has no version */
  _torVersion = QString();

  _shouldContinue = false;
  _reason = tr("Disconnected");

  /* Let interested parties know we lost our control connection */
  emit disconnected();
}

/** Check if the control socket is connected */
bool
TorControl::isConnected()
{
  return _controlConn->isConnected();
}

/** Send a message to Tor and reads the response. If Vidalia was unable to
 * send the command to Tor or read its response, false is returned. If the
 * response was read and the status code is not 250 OK, false is also
 * returned. */
bool
TorControl::send(ControlCommand cmd, ControlReply &reply, QString *errmsg)
{
  if (_controlConn->send(cmd, reply, errmsg)) {
    if (reply.getStatus() == "250") {
      return true;
    }
    if (errmsg) {
      *errmsg = reply.getMessage();
    }
  }
  return false;
}

/** Sends a message to Tor and discards the response. */
bool
TorControl::send(ControlCommand cmd, QString *errmsg)
{
  ControlReply reply;
  return send(cmd, reply, errmsg);
}

/** Sends an authentication cookie to Tor. The syntax is:
 *
 *   "AUTHENTICATE" SP 1*HEXDIG CRLF
 */
bool
TorControl::authenticate(const QByteArray cookie, QString *errmsg)
{
  ControlCommand cmd("AUTHENTICATE", base16_encode(cookie));
  ControlReply reply;
  QString str;

  if (!send(cmd, reply, &str)) {
    emit authenticationFailed(str);
    _shouldContinue = false;
    _reason = str;

    return err(errmsg, str);
  }
  onAuthenticated();
  return true;
}

/** Sends an authentication password to Tor. The syntax is:
 *
 *   "AUTHENTICATE" SP QuotedString CRLF
 */
bool
TorControl::authenticate(const QString &password, QString *errmsg)
{
  ControlCommand cmd("AUTHENTICATE", string_escape(password));
  ControlReply reply;
  QString str;

  if (!send(cmd, reply, &str)) {
    emit authenticationFailed(str);
    _shouldContinue = false;
    _reason = str;

    return err(errmsg, str);
  }
  onAuthenticated();
  return true;
}

/** Called when the controller has successfully authenticated to Tor. */
void
TorControl::onAuthenticated()
{
  /* The version of Tor isn't going to change while we're connected to it, so
   * save it for later. */
  getInfo("version", _torVersion);
  /* We want to use verbose names in events and GETINFO results. */
  useFeature("VERBOSE_NAMES");
  /* We want to use extended events in all async events */
  useFeature("EXTENDED_EVENTS");

  getBootstrapPhase();

  emit authenticated();
}

/** Sends a PROTOCOLINFO command to Tor and parses the response. */
ProtocolInfo
TorControl::protocolInfo(QString *errmsg)
{
  ControlCommand cmd("PROTOCOLINFO", "1");
  ControlReply reply;
  ProtocolInfo pi;
  QString msg, topic;
  QHash<QString,QString> keyvals;
  int idx;
  bool ok;

  if (!send(cmd, reply, errmsg))
    return ProtocolInfo();

  foreach (ReplyLine line, reply.getLines()) {
    if (line.getStatus() != "250")
      continue;

    msg = line.getMessage().trimmed();
    idx = msg.indexOf(" ");
    topic = msg.mid(0, idx).toUpper();

    if (idx > 0) {
      keyvals = string_parse_keyvals(msg.mid(idx+1), &ok);
      if (!ok)
        continue; /* Ignore malformed lines */
    } else {
      keyvals = QHash<QString,QString>();
    }

    if (topic == "AUTH") {
      if (keyvals.contains("METHODS"))
        pi.setAuthMethods(keyvals.value("METHODS"));
      if (keyvals.contains("COOKIEFILE"))
        pi.setCookieAuthFile(keyvals.value("COOKIEFILE"));
    } else if (topic == "VERSION") {
      if (keyvals.contains("Tor"))
        pi.setTorVersion(keyvals.value("Tor"));
    }
  }
  return pi;
}

/** Tells Tor the controller wants to enable <b>feature</b> via the
 * USEFEATURE control command. Returns true if the given feature was
 * successfully enabled. */
bool
TorControl::useFeature(const QString &feature, QString *errmsg)
{
  ControlCommand cmd("USEFEATURE", feature);
  return send(cmd, errmsg);
}

/** Returns the current BootstrapStatus */
BootstrapStatus
TorControl::bootstrapStatus(QString *errmsg)
{
  QString str = getInfo("status/bootstrap-phase").toString();
  if (!str.isEmpty()) {
    tc::Severity severity = tc::severityFromString(str.section(' ', 0, 0));
    QHash<QString,QString> args = string_parse_keyvals(str);
    return BootstrapStatus(severity,
              BootstrapStatus::statusFromString(args.value("TAG")),
              args.value("PROGRESS").toInt(),
              args.value("SUMMARY"),
              args.value("WARNING"),
              tc::connectionStatusReasonFromString(args.value("REASON")),
              BootstrapStatus::actionFromString(
                args.value("RECOMMENDATION")));
  }
  return BootstrapStatus();
}

/** Returns true if Tor either has an open circuit or (on Tor >=
 * 0.2.0.1-alpha) has previously decided it's able to establish a circuit. */
bool
TorControl::isCircuitEstablished()
{
  /* If Tor is recent enough, we can 'getinfo status/circuit-established' to
   * see if Tor has an open circuit */
  if (getTorVersion() >= 0x020001) {
    QString tmp;
    if (getInfo("status/circuit-established", tmp))
      return (tmp == "1");
  }

  /* Either Tor was too old or our getinfo failed, so try to get a list of all
   * circuits and check their statuses. */
  CircuitList circs = getCircuits();
  foreach (Circuit circ, circs) {
    if (circ.status() == Circuit::Built)
      return true;
  }
  return false;
}

/** Sends a GETINFO message to Tor based on the given map of keyvals. The
 * syntax is:
 *
 *    "GETINFO" 1*(SP keyword) CRLF
 */
bool
TorControl::getInfo(QHash<QString,QString> &map, QString *errmsg)
{
  ControlCommand cmd("GETINFO");
  ControlReply reply;

  /* Add the keys as arguments to the GETINFO message */
  foreach (QString key, map.keys()) {
    cmd.addArgument(key);
  }

  /* Ask Tor for the specified info values */
  if (send(cmd, reply, errmsg)) {
    /* Parse the response for the returned values */
    foreach (ReplyLine line, reply.getLines()) {
      /* Split the "key=val" line and map them */
      QStringList keyval = line.getMessage().split("=");
      if (keyval.size() == 2) {
        QString key = keyval.at(0);
        QString val = keyval.at(1);
        if (val.startsWith(QLatin1Char('\"')) &&
            val.endsWith(QLatin1Char('\"'))) {
          bool ok;
          val = string_unescape(val, &ok);
          if (! ok)
            continue;
        }
        map.insert(key, val);
      }
    }
    return true;
  }
  return false;
}

/** Sends a GETINFO message to Tor using the given list of <b>keys</b> and
 * returns a QVariantMap containing the specified keys and their values as
 * returned  by Tor. Returns a default constructed QVariantMap on failure. */
QVariantMap
TorControl::getInfo(const QStringList &keys, QString *errmsg)
{
  ControlCommand cmd("GETINFO");
  ControlReply reply;
  QVariantMap infoMap;

  cmd.addArguments(keys);
  if (!send(cmd, reply, errmsg))
    return QVariantMap();

  foreach (ReplyLine line, reply.getLines()) {
    QString msg = line.getMessage();
    int index   = msg.indexOf("=");
    QString key = msg.mid(0, index);
    QStringList val;

    if (index > 0 && index < msg.length()-1) {
      QString str = msg.mid(index+1);
      if (str.startsWith(QLatin1Char('\"')) &&
          str.endsWith(QLatin1Char('\"'))) {
        bool ok;
        str = string_unescape(str, &ok);
        if (! ok)
          continue;
      }
      val << str;
    }
    if (line.hasData())
      val << line.getData();

    if (infoMap.contains(key)) {
      QStringList values = infoMap.value(key).toStringList();
      values << val;
      infoMap.insert(key, values);
    } else {
      infoMap.insert(key, val);
    }
  }
  return infoMap;
}

/** Sends a GETINFO message to Tor with a single <b>key</b> and returns a
 * QVariant containing the value returned by Tor. Returns a default
 * constructed QVariant on failure. */
QVariant
TorControl::getInfo(const QString &key, QString *errmsg)
{
  QVariantMap map = getInfo(QStringList() << key, errmsg);
  return map.value(key);
}

/** Overloaded method to send a GETINFO command for a single info value */
bool
TorControl::getInfo(QString key, QString &val, QString *errmsg)
{
  QHash<QString,QString> map;
  map.insert(key, "");

  if (getInfo(map, errmsg)) {
    val = map.value(key);
    return true;
  }
  return false;
}

/** Sends a signal to Tor */
bool
TorControl::signal(TorSignal::Signal sig, QString *errmsg)
{
  ControlCommand cmd("SIGNAL");
  cmd.addArgument(TorSignal::toString(sig));

  if (sig == TorSignal::Shutdown || sig == TorSignal::Halt) {
    /* Tor closes the connection before giving us a response to any commands
     * asking it to stop running, so don't try to get a response. */
    return _controlConn->send(cmd, errmsg);
  }
  return send(cmd, errmsg);
}

/** Returns an address on which Tor is listening for application
 * requests. If none are available, a null QHostAddress is returned. */
QHostAddress
TorControl::getSocksAddress(QString *errmsg)
{
  QHostAddress socksAddr;

  /* If SocksPort is 0, then Tor is not accepting any application requests. */
  if (getSocksPort() == 0) {
    return QHostAddress::Null;
  }

  /* Get a list of SocksListenAddress lines and return the first valid IP
   * address parsed from the list. */
  QStringList addrList = getSocksAddressList(errmsg);
  foreach (QString addr, addrList) {
    addr = addr.mid(0, addr.indexOf(":"));
    if (socksAddr.setAddress(addr)) {
      return socksAddr;
    }
  }
  /* Otherwise Tor is listening on its default 127.0.0.1 */
  return QHostAddress::LocalHost;
}

/** Returns a (possibly empty) list of all currently configured
 * SocksListenAddress entries. */
QStringList
TorControl::getSocksAddressList(QString *errmsg)
{
  QStringList addrList;
  if (getConf("SocksListenAddress", addrList, errmsg)) {
    return addrList;
  }
  return QStringList();
}

/** Returns a valid SOCKS port for Tor, or 0 if Tor is not accepting
 * application requests. */
quint16
TorControl::getSocksPort(QString *errmsg)
{
  QList<quint16> portList = getSocksPortList(errmsg);
  if (portList.size() > 0) {
    return portList.at(0);
  }
  return 0;
}

/** Returns a list of all currently configured SOCKS ports. If Tor is not
 * accepting any application connections, an empty list will be returned. */
QList<quint16>
TorControl::getSocksPortList(QString *errmsg)
{
  bool valid;
  quint16 port, socksPort;
  QString portString;
  QList<quint16> portList;

  /* Get the value of the SocksPort configuration variable */
  if (getConf("SocksPort", portString, errmsg)) {
    socksPort = (quint16)portString.toUInt(&valid);
    if (valid) {
      if (socksPort == 0) {
        /* A SocksPort of 0 means Tor is not accepting any application
         * connections. */
        return QList<quint16>();
      }
    }
  }
  /* Get a list of SOCKS ports from SocksListenAddress entries */
  QStringList addrList = getSocksAddressList(errmsg);
  foreach (QString addr, addrList) {
    if (addr.contains(":")) {
      portString = addr.mid(addr.indexOf(":")+1);
      port = (quint16)portString.toUInt(&valid);
      if (valid) {
        portList << port;
      }
    }
  }
  /* If there were no SocksListenAddress entries, or one or more of them did
   * not specify a port, then add the value of SocksPort, too */
  if (!portList.size() || (portList.size() != addrList.size())) {
    portList << socksPort;
  }
  return portList;
}

/** Reeturns Tor's version as a string. */
QString
TorControl::getTorVersionString()
{
  return _torVersion;
}

/** Returns Tor's version as a numeric value. Note that this discards any
 * version status flag, such as "-alpha" or "-rc". */
quint32
TorControl::getTorVersion()
{
  static QString versionString;
  static quint32 version = 0;
  quint8 major, minor, micro, patch;

  /* Only recompute the version number if the version string changed */
  if (versionString == _torVersion)
    return version;
  versionString = _torVersion;

  /* Split the version string at either "." or "-" characters */
  QStringList parts = versionString.split(QRegExp("\\.|-|\\ "));
  if (parts.size() >= 4) {
    major = (quint8)parts.at(0).toUInt();
    minor = (quint8)parts.at(1).toUInt();
    micro = (quint8)parts.at(2).toUInt();
    patch = (quint8)parts.at(3).toUInt();
    version = ((major << 24) | (minor << 16) | (micro << 8) | patch);
  } else {
    /* Couldn't parse the version string */
    version = 0;
  }
  return version;
}

/** Sets an event and its handler. If add is true, then the event is added,
 * otherwise it is removed. If set is true, then the given event will be
 * registered with Tor. */
bool
TorControl::setEvent(TorEvents::Event e, bool add, bool set, QString *errmsg)
{
  _events = (add ? (_events | e) : (_events & ~e));
  if (set && isConnected())
    return setEvents(errmsg);
  return true;
}

/** Register for the events currently in the event list */
bool
TorControl::setEvents(QString *errmsg)
{
  ControlCommand cmd("SETEVENTS");

  for (TorEvents::Event e = TorEvents::EVENT_MIN; e <= TorEvents::EVENT_MAX;) {
    if (_events & e)
      cmd.addArgument(TorEvents::toString(e));
    e = static_cast<TorEvents::Event>(e << 1);
  }
  return send(cmd, errmsg);
}

/** Sets each configuration key in <b>map</b> to the value associated
 * with its key. */
bool
TorControl::setConf(QHash<QString,QString> map, QString *errmsg, ControlReply *reply)
{
  ControlCommand cmd("SETCONF");

  /* Add each keyvalue to the argument list */
  foreach (QString key, map.uniqueKeys()) {
    /* If a key has multiple values (e.g., a QMultiHash), they are stored
     * in order from most recently inserted to least recently inserted.
     * So, walk the list in reverse so that we append the configuration
     * values to the SETCONF command in the same order they were inserted
     * into the QHash. */
    QList<QString> values = map.values(key);
    for (int i = values.size()-1; i >= 0; i--) {
      QString value = values.at(i);
      if (value.length() > 0)
        cmd.addArgument(key + "=" + string_escape(value));
      else
        cmd.addArgument(key);
    }
  }

  if(not reply)
    reply = new ControlReply();
  return send(cmd, *reply, errmsg);
}

/** Sets a single configuration key to the given value. */
bool
TorControl::setConf(QString key, QString value, QString *errmsg, ControlReply *reply)
{
  QHash<QString,QString> map;
  map.insert(key, value);
  return setConf(map, errmsg, reply);
}

/** Sets a single configuration string that is formatted <key=escaped value>.*/
bool
TorControl::setConf(QString keyAndValue, QString *errmsg, ControlReply *reply)
{
  QHash<QString,QString> map;
  map.insert(keyAndValue, "");
  return setConf(map, errmsg, reply);
}

/** Gets values for a set of configuration keys, each of which has a single
 * value. */
bool
TorControl::getConf(QHash<QString,QString> &map, QString *errmsg)
{
  QHash<QString,QStringList> multiMap;
  foreach (QString key, map.keys()) {
    multiMap.insert(key, QStringList());
  }
  if (getConf(multiMap, errmsg)) {
    foreach (QString key, multiMap.keys()) {
      if (map.contains(key)) {
        map.insert(key, multiMap.value(key).join("\n"));
      }
    }
  }
  return false;
}

/** Gets a set of configuration keyvalues and stores them in <b>map</b>. */
bool
TorControl::getConf(QHash<QString,QStringList> &map, QString *errmsg)
{
  ControlCommand cmd("GETCONF");
  ControlReply reply;
  QStringList confValue;
  QString confKey;

  /* Add the keys as arguments to the GETINFO message */
  foreach (QString key, map.keys()) {
    cmd.addArgument(key);
  }

  /* Ask Tor for the specified info values */
  if (send(cmd, reply, errmsg)) {
    /* Parse the response for the returned values */
    foreach (ReplyLine line, reply.getLines()) {
      /* Split the "key=val" line and map them */
      QStringList keyval = line.getMessage().split("=");
      if (keyval.size() == 2) {
        confKey = keyval.at(0);

        if (map.contains(confKey)) {
          /* This configuration key has multiple values, so add this one to
           * the list. */
          confValue = map.value(confKey);
        }
        confValue << keyval.at(1);
        map.insert(keyval.at(0), confValue);
      }
    }
    return true;
  }
  return false;
}

/** Gets a single configuration value for <b>key</b>. */
bool
TorControl::getConf(QString key, QString &value, QString *errmsg)
{
  QStringList confValues;
  if (getConf(key, confValues, errmsg)) {
    value = confValues.join("\n");
    return true;
  }
  return false;
}

/** Gets a list of configuration values for <b>key</b>. */
bool
TorControl::getConf(QString key, QStringList &value, QString *errmsg)
{
  QHash<QString,QStringList> map;
  map.insert(key, QStringList());

  if (getConf(map, errmsg)) {
    value = map.value(key);
    return true;
  }
  return false;
}

/** Sends a GETICONF message to Tor using the given list of <b>keys</b> and
 * returns a QVariantMap containing the specified keys and their values as
 * returned  by Tor. Returns a default constructed QVariantMap on failure. */
QVariantMap
TorControl::getConf(const QStringList &keys, QString *errmsg)
{
  ControlCommand cmd("GETCONF");
  ControlReply reply;
  QVariantMap confMap;

  cmd.addArguments(keys);
  if (!send(cmd, reply, errmsg))
    return QVariantMap();

  foreach (ReplyLine line, reply.getLines()) {
    QString msg = line.getMessage();
    int index   = msg.indexOf("=");
    QString key = msg.mid(0, index);
    QString val;

    if (index > 0 && index < msg.length()-1)
      val = msg.mid(index+1);
    if (val.startsWith(QLatin1Char('\"')) &&
        val.endsWith(QLatin1Char('\"'))) {
      bool ok;
      val = string_unescape(val, &ok);
      if (! ok)
        continue;
    }

    if (confMap.contains(key)) {
      QStringList values = confMap.value(key).toStringList();
      values << val;
      confMap.insert(key, values);
    } else {
      confMap.insert(key, val);
    }
  }
  return confMap;
}

/** Sends a GETCONF message to Tor with a single <b>key</b> and returns a
 * QVariant containing the value returned by Tor. Returns a default
 * constructed QVariant on failure. */
QVariant
TorControl::getConf(const QString &key, QString *errmsg)
{
  QVariantMap map = getConf(QStringList() << key, errmsg);
  return map.value(key);
}

/** Loads the contents as if they were read from disk as the torrc */
bool
TorControl::loadConf(const QString &contents, QString *errmsg)
{
  ControlCommand cmd("+LOADCONF");
  ControlReply reply;

  cmd.addArgument(contents + ".");
  return send(cmd, reply, errmsg);
}

/** Sends a GETCONF message to Tor with the single key and returns a QString
 * containing the value returned by Tor */
QString
TorControl::getHiddenServiceConf(const QString &key, QString *errmsg)
{
  ControlCommand cmd("GETCONF");
  ControlReply reply;
  QVariantMap confMap;

  cmd.addArgument(key);
  if (!send(cmd, reply, errmsg))
    return "";

  return reply.toString();
}

/** Asks Tor to save the current configuration to its torrc. */
bool
TorControl::saveConf(QString *errmsg)
{
  ControlCommand cmd("SAVECONF");
  bool ret = send(cmd, errmsg);

  if(!ret) {
    QString err;
    setConf("__ReloadTorrcOnSIGHUP", "0", &err);
  }

  return ret;
}

/** Tells Tor to reset the given configuration keys back to defaults. */
bool
TorControl::resetConf(QStringList keys, QString *errmsg)
{
  ControlCommand cmd("RESETCONF");

  /* Add each key to the argument list */
  foreach (QString key, keys) {
    cmd.addArgument(key);
  }
  return send(cmd, errmsg);
}

/** Tells Tor to reset a single given configuration key back to its default
 * value. */
bool
TorControl::resetConf(QString key, QString *errmsg)
{
  return resetConf(QStringList() << key, errmsg);
}

/** Returns true if microdescriptors are used */
bool
TorControl::useMicrodescriptors(QString *errmsg)
{
  if(!errmsg)
    errmsg = new QString();

  QString mdres, fetchres;
  if(!getConf("UseMicrodescriptors", mdres, errmsg))
    return false;
  if(!getConf("FetchUselessDescriptors", fetchres, errmsg))
    return false;
  return (mdres == "1") or (mdres == "auto" and fetchres == "0");
}

/** Returns an unparsed router descriptor for the router whose fingerprint
 * matches <b>id</b>. The returned text can later be parsed by the
 * RouterDescriptor class. If <b>id</b> is invalid, then an empty
 * QStringList is returned. */
QStringList
TorControl::getRouterDescriptorText(const QString &id, QString *errmsg)
{
  if(useMicrodescriptors(errmsg))
    return getInfo("md/id/" + id, errmsg).toStringList();

  return getInfo("desc/id/" + id, errmsg).toStringList();
}

/** Returns the descriptor for the router whose fingerprint matches
 * <b>id</b>. If <b>id</b> is invalid or the router's descriptor cannot
 * be parsed, then an invalid RouterDescriptor is returned. */
RouterDescriptor
TorControl::getRouterDescriptor(const QString &id, QString *errmsg)
{
  return RouterDescriptor(getRouterDescriptorText(id, errmsg), useMicrodescriptors());
}

/** Returns the status of the router whose fingerprint matches <b>id</b>. If
 * <b>id</b> is invalid or the router's status cannot be parsed, then an
 * invalid RouterStatus is returned. */
RouterStatus
TorControl::getRouterStatus(const QString &id, QString *errmsg)
{
  QStringList status = getInfo("ns/id/" + id, errmsg).toStringList();
  return RouterStatus(status);
}

/** Returns a RouterStatus object for every known router in the network. If
 * the network status document cannot be parsed, then an empty NetworkStatus
 * is returned. */
NetworkStatus
TorControl::getNetworkStatus(QString *errmsg)
{
  QStringList networkStatusLines = getInfo("ns/all", errmsg).toStringList();
  QList<RouterStatus> networkStatus;
  int len = networkStatusLines.size();
  int i = 0;

  while (i < len) {
    /* Extract the "r", "s", and whatever other status lines */
    QStringList routerStatusLines;
    do {
      routerStatusLines << networkStatusLines.at(i);
    } while (++i < len && ! networkStatusLines.at(i).startsWith("r "));

    /* Create a new RouterStatus object and add it to the network status, if
     * it's valid. */
    RouterStatus routerStatus(routerStatusLines);
    if (routerStatus.isValid())
      networkStatus << routerStatus;
  }
  return networkStatus;
}

/** Returns the annotations for the router whose fingerprint matches
 * <b>id</b>. If <b>id</b> is invalid or the router's annotations cannot be
 * parsed, then an empty DescriptorAnnotations is returned and <b>errmsg</b>
 * is set if it's not NULL. (Tor >= 0.2.0.13-alpha only) */
DescriptorAnnotations
TorControl::getDescriptorAnnotations(const QString &id, QString *errmsg)
{
  QStringList lines = getInfo("desc-annotations/id/"+id, errmsg).toStringList();
  DescriptorAnnotations annotations;
  QString key, value;

  foreach (QString line, lines) {
    int idx = line.indexOf(" ");

    /* Extract the annotation key */
    key = line.mid(0, idx);
    if (key.startsWith("@"))
      key = key.remove(0, 1);

    /* Extract the annotation value (if present) */
    if (idx > 0 && idx < line.length()-1)
      annotations.insert(key, line.mid(idx + 1).trimmed());
    else
      annotations.insert(key, QString());
  }
  return annotations;
}

/** Gets a list of current circuits. */
QList<Circuit>
TorControl::getCircuits(QString *errmsg)
{
  ControlCommand cmd("GETINFO", "circuit-status");
  ControlReply reply;
  CircuitList circuits;

  if (!send(cmd, reply, errmsg))
    return CircuitList();

  /* The rest of the circuits just come as data, one per line */
  foreach(QString line, reply.getData()) {
    Circuit circ(line);
    if (circ.isValid())
      circuits << circ;
  }
  return circuits;
}

/** Closes the circuit specified by <b>circId</b>. If <b>ifUnused</b> is
 * true, then the circuit will not be closed unless it is unused. */
bool
TorControl::closeCircuit(const CircuitId &circId, bool ifUnused, QString *errmsg)
{
  ControlCommand cmd("CLOSECIRCUIT", circId);
  if (ifUnused) {
    cmd.addArgument("IfUnused");
  }
  return send(cmd, errmsg);
}

/** Gets a list of current streams. */
QList<Stream>
TorControl::getStreams(QString *errmsg)
{
  ControlCommand cmd("GETINFO", "stream-status");
  ControlReply reply;
  QList<Stream> streams;
  Stream s;

  if (send(cmd, reply, errmsg)) {
    /* Sometimes there is a stream on the first message line */
    QString msg = reply.getMessage();
    s = Stream::fromString(msg.mid(msg.indexOf("=")+1));
    if (s.isValid())
      streams << s;

    /* The rest of the streams just come as data, one per line */
    foreach (QString line, reply.getData()) {
      s = Stream::fromString(line);
      if (s.isValid())
        streams << s;
    }
  }
  return streams;
}

/** Closes the stream specified by <b>streamId</b>. */
bool
TorControl::closeStream(const StreamId &streamId, QString *errmsg)
{
  ControlCommand cmd("CLOSESTREAM", streamId);
  cmd.addArgument("1"); /* 1 == REASON_MISC (tor-spec.txt) */
  return send(cmd, errmsg);
}

/** Gets a list of address mappings of the type specified by <b>type</b>
 * (defaults to <i>AddressMapAll</i>. */
AddressMap
TorControl::getAddressMap(AddressMap::AddressMapType type, QString *errmsg)
{
  AddressMap addressMap;
  QStringList entries;

  switch (type) {
    case AddressMap::AddressMapConfig:
      entries = getInfo("address-mappings/config").toStringList();
      break;
    case AddressMap::AddressMapCache:
      entries = getInfo("address-mappings/cache").toStringList();
      break;
    case AddressMap::AddressMapControl:
      entries = getInfo("address-mappings/control").toStringList();
      break;
    default:
      entries = getInfo("address-mappings/all").toStringList();
  }

  foreach (QString entry, entries) {
    addressMap.add(entry);
  }
  return addressMap;
}

/** Gets the ISO-3166 two-letter country code for <b>ip</b> from Tor.
 * Returns a default-constructed QString on failure or if a country code
 * is not known for <b>ip</b>. On failure, <b>errmsg</b> will be set if
 * it's not NULL. */
QString
TorControl::ipToCountry(const QHostAddress &ip, QString *errmsg)
{
  QString request   = QString("ip-to-country/%1").arg(ip.toString());
  QVariant response = getInfo(request, errmsg);
  if (! response.isNull())
    return response.toString();
  return QString();
}

/** Takes ownership of the tor process it's communicating to */
bool
TorControl::takeOwnership(QString *errmsg)
{
  ControlCommand cmd("TAKEOWNERSHIP");
  return send(cmd, errmsg);
}

/** Clear the error state for shouldContinue(errmsg) */
void
TorControl::clearErrState()
{
  _shouldContinue = true;
  _reason = "";
}
