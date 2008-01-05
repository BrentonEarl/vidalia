/****************************************************************
 *  Vidalia is distributed under the following license:
 *
 *  Copyright (C) 2006,  Matt Edman, Justin Hipple
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 * 
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, 
 *  Boston, MA  02110-1301, USA.
 ****************************************************************/

/** 
 * \file dock.h
 * \version $Id$
 */

#ifndef _DOCK_H
#define _DOCK_H

#include <Carbon/Carbon.h>

/** Sets dock icon to the specified .icns file. Note that callers of this
 * function should NOT specify the .icns extension in the parameter. */
void setDockIcon(const char *dockIcon);

/** Restores the dock icon to the application's icon. */
void restoreDockIcon();

#endif
