// Copyright (C) 2012 GlavSoft LLC.
// All rights reserved.
//
//-------------------------------------------------------------------------
// This file is part of the TightVNC software.  Please visit our Web site:
//
//                       http://www.tightvnc.com/
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//-------------------------------------------------------------------------
//

#ifndef _RESOURCE_STRINGS_H_
#define _RESOURCE_STRINGS_H_

#include "util/CommonHeader.h"
#include "resource.h"

class ResourceStrings
{
public:
  ResourceStrings();
  ResourceStrings(int min, int max);

  bool isValid(int res); 
  // this function returns string as it is stored
  StringStorage getStrRes(int strRes); 
  // this function returns the string without '\t' and '&'
  StringStorage getStrPureRes(int strRes);

protected:
  int m_min, m_max;
};


#endif
