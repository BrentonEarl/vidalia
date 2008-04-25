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
** \file languagesupport.cpp
** \version $Id$
** \brief Contains languages supported by Vidalia
*/

#include <QTranslator>
#include <QLocale>
#include <vidalia.h>

#include "languagesupport.h"


/** Initializes the list of available languages. */
QMap<QString, QString>
LanguageSupport::languages()
{
  static QMap<QString, QString> languages;
  if (languages.isEmpty()) {
    languages.insert("en",    "English");
    languages.insert("ar",
      QString::fromUtf8("\330\247\331\204\330\271\330\261\330"
                        "\250\331\212\330\251"));
    languages.insert("bg",
      QString::fromUtf8("\320\221\321\212\320\273\320\263\320"
                        "\260\321\200\321\201\320\272\320\270"));
    languages.insert("cs",    
      QString::fromUtf8("\304\215esky"));
    languages.insert("de",    "Deutsch");
    languages.insert("es",    
      QString::fromUtf8("espa\303\261ol"));
    languages.insert("fa",
      QString::fromUtf8("\331\201\330\247\330\261\330\263\333\214"));
    languages.insert("fi",    "suomi");
    languages.insert("fr",    
      QString::fromUtf8("fran\303\247ais"));
    languages.insert("he",
      QString::fromUtf8("\327\242\327\221\327\250\327\231\327\252"));
    languages.insert("hu",    "magyar nyelv");
    languages.insert("it",    "Italiano");
    languages.insert("ja",
      QString::fromUtf8("\346\227\245\346\234\254\350\252\236"));
    languages.insert("nb",    
      QString::fromUtf8("Bokm\303\245l"));
    languages.insert("nl",    "Nederlands");
    languages.insert("pl",    "Polski");
    languages.insert("pt",    
      QString::fromUtf8("Portugu\303\252s"));
    languages.insert("ru",
      QString::fromUtf8("\320\240\321\203\321\201\321\201\320\272\320\270\320\271"));
    languages.insert("sq",    "Shqip");
    languages.insert("sv",    "svenska");
    languages.insert("zh-cn", 
      QString::fromUtf8("\347\256\200\344\275\223\345\255\227"));
    languages.insert("zh-tw", 
      QString::fromUtf8("\347\260\241\351\253\224\345\255\227"));
  }
  return languages;
}

/** Returns the default language code for the system locale. */
QString
LanguageSupport::defaultLanguageCode()
{
  QString localeName = QLocale::system().name();
  QString language   = localeName.mid(0, localeName.indexOf("_"));
  if (language == "zh") {
    language += "-" + localeName.mid(localeName.indexOf("_")+1).toLower();
  }
  if (!isValidLanguageCode(language)) {
    language = "en";
  }
  return language;
}

/** Returns the language code for a given language name. */
QString
LanguageSupport::languageCode(const QString &languageName)
{
  return languages().key(languageName);
}

/** Returns a list of all supported language codes. (e.g., "en"). */
QStringList
LanguageSupport::languageCodes()
{
  return languages().keys();
}

/** Returns the language name for a given language code. */
QString
LanguageSupport::languageName(const QString &languageCode)
{
  return languages().value(languageCode);
}

/** Returns a list of all supported language names (e.g., "English"). */
QStringList
LanguageSupport::languageNames()
{
  return languages().values();
}

/** Returns true if we understand the given language code. */
bool
LanguageSupport::isValidLanguageCode(const QString &languageCode)
{
  return languageCodes().contains(languageCode);
}

/** Returns true if <b>languageCode</b> requires a right-to-left layout. */
bool
LanguageSupport::isRightToLeft(const QString &languageCode)
{
  return (!languageCode.compare("ar", Qt::CaseInsensitive) 
            || !languageCode.compare("fa", Qt::CaseInsensitive)
            || !languageCode.compare("he", Qt::CaseInsensitive));
}

/** Sets the application's translator to the specified language. */
bool
LanguageSupport::translate(const QString &languageCode)
{
  QString code = languageCode.toLower();
  if (isValidLanguageCode(code)) {
    QTranslator *translator = new QTranslator(vApp);
    if (translator->load(QString(":/lang/vidalia_%1.qm").arg(code))) {
      QApplication::installTranslator(translator);
      if (isRightToLeft(code))
        vApp->setLayoutDirection(Qt::RightToLeft);
      return true;
    }
    delete translator;
  }
  return false;
}
