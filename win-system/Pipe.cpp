// Copyright (C) 2009,2010,2011,2012 GlavSoft LLC.
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

#include "Pipe.h"
#include "util/Exception.h"
#include <crtdbg.h>
#include "win-system/Environment.h"
#include "thread/AutoLock.h"

#define MAX_PORTION_SIZE 512 * 1024

size_t Pipe::writeByHandle(const void *buffer, size_t len, HANDLE pipeHandle)
{
  DWORD result;
  OVERLAPPED overlapped;
  memset(&overlapped, 0, sizeof(OVERLAPPED));
  overlapped.hEvent = m_writeEvent.getHandle();

  bool success;
  {
    AutoLock al(&m_hPipeMutex);
    checkPipeHandle(pipeHandle);
    DWORD length = (DWORD)len;
    _ASSERT(length == len);
    success = WriteFile(pipeHandle, // pipe handle
                        buffer,    // message
                        length,  // message length
                        &result, // bytes written
                        &overlapped)    // overlapped
                        != 0;
  }

  if (!success) {
    int errCode = GetLastError();

    if (errCode == ERROR_IO_PENDING) {
      m_writeEvent.waitForEvent();
      DWORD cbRet;
      AutoLock al(&m_hPipeMutex);
      checkPipeHandle(pipeHandle);
      if (GetOverlappedResult(pipeHandle, &overlapped, &cbRet, FALSE) ||
          cbRet == 0) {
        result = cbRet;
      } else {
        StringStorage errMess;
        Environment::getErrStr(_T("The Pipe's write function failed")
                               _T(" after GetOverlappedResult calling"),
                               &errMess);
        throw IOException(errMess.getString());
      }
    } else {
      StringStorage errMess;
      Environment::getErrStr(_T("The Pipe's write function failed")
                             _T(" after WriteFile calling"), &errMess);
      throw IOException(errMess.getString());
    }
  } // else operation already successful has been completed

  if (result == 0) {
    throw IOException(_T("Unknown pipe error"));
  }
  return result;
}

size_t Pipe::readByHandle(void *buffer, size_t len, HANDLE pipeHandle)
{
  DWORD result = 0;
  OVERLAPPED overlapped;
  memset(&overlapped, 0, sizeof(OVERLAPPED));
  overlapped.hEvent = m_readEvent.getHandle();

  bool success;
  {
    AutoLock al(&m_hPipeMutex);
    DWORD length = (DWORD)len;
    _ASSERT(length == len);
    checkPipeHandle(pipeHandle);
    success = ReadFile(pipeHandle,         // pipe handle
                       buffer,            // message
                       length,          // message length
                       &result,         // bytes read
                       &overlapped)   // overlapped
                       != 0;
  }
  if (!success) {
    DWORD errCode = GetLastError();

    if (errCode == ERROR_IO_PENDING) {
      m_readEvent.waitForEvent();
      DWORD cbRet = 0;
      AutoLock al(&m_hPipeMutex);
      checkPipeHandle(pipeHandle);
      if (GetOverlappedResult(pipeHandle, &overlapped, &cbRet, FALSE) &&
          cbRet != 0) {
        result = cbRet;
      } else {
        StringStorage errMess;
        Environment::getErrStr(_T("The Pipe's read function failed")
                               _T(" after GetOverlappedResult calling"),
                               &errMess);
        throw IOException(errMess.getString());
      }
    } else {
      StringStorage errMess;
      Environment::getErrStr(_T("The Pipe's read function failed")
                             _T(" after ReadFile calling"), &errMess);
      throw IOException(errMess.getString());
    }
  } // else operation already successful has been completed

  if (result == 0) {
    throw IOException(_T("Unknown pipe error"));
  }
  return result;
}

void Pipe::checkPipeHandle(HANDLE pipeHandle)
{
  if (pipeHandle == INVALID_HANDLE_VALUE) {
    throw IOException(_T("Invalid pipe handle"));
  }
}
