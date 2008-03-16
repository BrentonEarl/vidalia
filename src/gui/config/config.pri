#################################################################
#  $Id$
#
#  Vidalia is distributed under the following license:
#
#  Copyright (C) 2006,  Matt Edman, Justin Hipple
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  
#  02110-1301, USA.
#################################################################

FORMS += $$PWD/configdialog.ui \
         $$PWD/generalpage.ui \
         $$PWD/serverpage.ui \
         $$PWD/advancedpage.ui \
         $$PWD/appearancepage.ui

HEADERS += $$PWD/configdialog.h \
           $$PWD/domainvalidator.h \
           $$PWD/ipvalidator.h \
           $$PWD/portvalidator.h \
           $$PWD/nicknamevalidator.h \
           $$PWD/configpage.h \
           $$PWD/configpagestack.h \
           $$PWD/generalpage.h \
           $$PWD/serverpage.h \
           $$PWD/advancedpage.h \
           $$PWD/appearancepage.h

SOURCES += $$PWD/configdialog.cpp \
           $$PWD/domainvalidator.cpp \
           $$PWD/ipvalidator.cpp \
           $$PWD/portvalidator.cpp \
           $$PWD/nicknamevalidator.cpp \
           $$PWD/configpagestack.cpp \
           $$PWD/generalpage.cpp \
           $$PWD/serverpage.cpp \
           $$PWD/advancedpage.cpp \
           $$PWD/appearancepage.cpp
