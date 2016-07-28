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

#include "RfbClient.h"
#include "thread/AutoLock.h"
#include "RfbCodeRegistrator.h"
#include "ft-server-lib/FileTransferRequestHandler.h"
#include "network/socket/SocketStream.h"
#include "RfbInitializer.h"
#include "ClientAuthListener.h"
#include "server-config-lib/Configurator.h"

RfbClient::RfbClient(NewConnectionEvents *newConnectionEvents,
                     SocketIPv4 *socket,
                     ClientTerminationListener *extTermListener,
                     ClientAuthListener *extAuthListener, bool viewOnly,
                     bool isOutgoing, unsigned int id,
                     const ViewPortState *constViewPort,
                     const ViewPortState *dynViewPort,
                     LogWriter *log)
: m_socket(socket), // now we own the socket
  m_newConnectionEvents(newConnectionEvents),
  m_viewOnly(viewOnly),
  m_isOutgoing(isOutgoing),
  m_shared(false),
  m_viewOnlyAuth(true),
  m_clientState(IN_NONAUTH),
  m_isMarkedOk(false),
  m_extTermListener(extTermListener),
  m_extAuthListener(extAuthListener),
  m_updateSender(0),
  m_clipboardExchange(0),
  m_clientInputHandler(0),
  m_id(id),
  m_desktop(0),
  m_constViewPort(constViewPort, log),
  m_dynamicViewPort(dynViewPort, log),
  m_log(log)
{
  resume();
}

RfbClient::~RfbClient()
{
  terminate();
  wait();
  delete m_socket;
}

void RfbClient::disconnect()
{
  // Shutdown and close socket.
  try { m_socket->shutdown(SD_BOTH); } catch (...) { }
  try { m_socket->close(); } catch (...) { }
  m_log->message(_T("Connection has been closed"));
}

unsigned int RfbClient::getId() const
{
  return m_id;
}

bool RfbClient::isOutgoing() const
{
  return m_isOutgoing;
}

void RfbClient::getPeerHost(StringStorage *host)
{
  SocketAddressIPv4 addr;

  if (m_socket->getPeerAddr(&addr)) {
    addr.toString(host);
  } else {
    // FIXME: This may occur if the close() function has been called.
    _ASSERT(FALSE);

    host->setString(_T("unknown"));
  }
}

void RfbClient::getLocalIpAddress(StringStorage *address)
{
  SocketAddressIPv4 addr;

  if (m_socket->getLocalAddr(&addr)) {
    addr.toString(address);
  } else {
    // FIXME: This may occur if the close() function has been called.
    _ASSERT(FALSE);

    address->setString(_T("unknown"));
  }
}

void RfbClient::getSocketAddr(SocketAddressIPv4 *addr) const
{
  m_socket->getPeerAddr(addr);
}

void RfbClient::setClientState(ClientState newState)
{
  AutoLock al(&m_clientStateMut);
  if (newState > m_clientState) {
    m_clientState = newState;
  }
}

ClientState RfbClient::getClientState()
{
  AutoLock al(&m_clientStateMut);
  return m_clientState;
}

void RfbClient::setViewOnlyFlag(bool value)
{
  if (getClientState() < IN_NORMAL_PHASE) {
    throw Exception(_T("Irrelevant call to RfbClient::setViewOnlyFlag()"));
  }
  m_viewOnly = value || m_viewOnlyAuth;
  m_clientInputHandler->setViewOnlyFlag(m_viewOnly);
}

void RfbClient::changeDynViewPort(const ViewPortState *dynViewPort)
{
  AutoLock al(&m_viewPortMutex);
  m_dynamicViewPort.changeState(dynViewPort);
}

void RfbClient::notifyAbStateChanging(ClientState state)
{
  setClientState(state);
  m_extTermListener->onClientTerminate();
}

void RfbClient::onTerminate()
{
  disconnect();
}

void RfbClient::execute()
{
  // Initialized by default message that will be logged on normal way
  // of disconnection.
  StringStorage peerStr;
  getPeerHost(&peerStr);
  StringStorage sysLogMessage;
  sysLogMessage.format(_T("The client %s has disconnected"),
                       peerStr.getString());

  ServerConfig *config = Configurator::getInstance()->getServerConfig();

  WindowsEvent connClosingEvent;

  SocketStream sockStream(m_socket);

  RfbOutputGate output(&sockStream);
  RfbInputGate input(&sockStream);

  FileTransferRequestHandler *fileTransfer = 0;

  RfbInitializer rfbInitializer(&sockStream, m_extAuthListener, this,
                                !m_isOutgoing);

  try {
    // First initialization phase
    try {
      m_log->info(_T("Entering RFB initialization phase 1"));
      rfbInitializer.authPhase();
      setClientState(IN_AUTH);
      m_log->debug(_T("RFB initialization phase 1 completed"));

      m_shared = rfbInitializer.getSharedFlag();
      m_log->debug(_T("Shared flag = %d"), (int)m_shared);
      m_viewOnlyAuth = rfbInitializer.getViewOnlyAuth();
      m_log->debug(_T("Initial view-only state = %d"), (int)m_viewOnly);
      m_log->debug(_T("Authenticated with view-only password = %d"), (int)m_viewOnlyAuth);
      m_viewOnly = m_viewOnly || m_viewOnlyAuth;

      // Let RfbClientManager handle new authenticated connection.
      m_desktop = m_extAuthListener->onClientAuth(this);

      m_log->info(_T("View only = %d"), (int)m_viewOnly);
    } catch (Exception &e) {
      m_log->error(_T("Error during RFB initialization: %s"), e.getMessage());
      throw;
    }
    _ASSERT(m_desktop != 0);

    m_constViewPort.initDesktopInterface(m_desktop);
    m_dynamicViewPort.initDesktopInterface(m_desktop);

    RfbDispatcher dispatcher(&input, &connClosingEvent);
    m_log->debug(_T("Dispatcher has been created"));
    CapContainer srvToClCaps, clToSrvCaps, encCaps;
    RfbCodeRegistrator codeRegtor(&dispatcher, &srvToClCaps, &clToSrvCaps,
                                  &encCaps);
    // Init modules
    // UpdateSender initialization
    m_updateSender = new UpdateSender(&codeRegtor, m_desktop, this,
                                      &output, m_id, m_desktop, m_log);
    m_log->debug(_T("UpdateSender has been created"));
    PixelFormat pf;
    Dimension fbDim;
    m_desktop->getFrameBufferProperties(&fbDim, &pf);
    Rect viewPort = getViewPortRect(&fbDim);
    m_updateSender->init(&Dimension(&viewPort), &pf);
    m_log->debug(_T("UpdateSender has been initialized"));
    // ClientInputHandler initialization
    m_clientInputHandler = new ClientInputHandler(&codeRegtor, this,
                                                  m_viewOnly);
    m_log->debug(_T("ClientInputHandler has been created"));
    // ClipboardExchange initialization
    m_clipboardExchange = new ClipboardExchange(&codeRegtor, m_desktop, &output,
                                                m_viewOnly, m_log);
    m_log->debug(_T("ClipboardExchange has been created"));

    // FileTransfers initialization
    if (config->isFileTransfersEnabled() &&
        rfbInitializer.getTightEnabledFlag()) {
      fileTransfer = new FileTransferRequestHandler(&codeRegtor, &output, m_desktop, m_log, !m_viewOnly);
      m_log->debug(_T("File transfer has been created"));
    } else {
      m_log->info(_T("File transfer is not allowed"));
    }

    // Second initialization phase
    // Send and receive initialization information between server and viewer
    m_log->debug(_T("View port: (%d,%d) (%dx%d)"), viewPort.left,
                                                 viewPort.top,
                                                 viewPort.getWidth(),
                                                 viewPort.getHeight());
    m_log->info(_T("Entering RFB initialization phase 2"));
    rfbInitializer.afterAuthPhase(&srvToClCaps, &clToSrvCaps,
                                  &encCaps, &Dimension(&viewPort), &pf);
    m_log->debug(_T("RFB initialization phase 2 completed"));

    // Start normal phase
    setClientState(IN_NORMAL_PHASE);

    m_log->info(_T("Entering normal phase of the RFB protocol"));
    dispatcher.resume();

    connClosingEvent.waitForEvent();
  } catch (Exception &e) {
    m_log->error(_T("Connection will be closed: %s"), e.getMessage());
    sysLogMessage.format(_T("The client %s has been")
                         _T(" disconnected for the reason: %s"),
                         peerStr.getString(), e.getMessage());
  }

  disconnect();
  m_newConnectionEvents->onDisconnect(&sysLogMessage);

  // After this call, we are guaranteed not to be used by other threads.
  notifyAbStateChanging(IN_PENDING_TO_REMOVE);

  if (fileTransfer)         delete fileTransfer;
  if (m_clipboardExchange)  delete m_clipboardExchange;
  if (m_clientInputHandler) delete m_clientInputHandler;
  if (m_updateSender)       delete m_updateSender;

  // Let the client manager remove us from the client lists.
  notifyAbStateChanging(IN_READY_TO_REMOVE);
}

void RfbClient::sendUpdate(const UpdateContainer *updateContainer,
                           const CursorShape *cursorShape)
{
  m_updateSender->newUpdates(updateContainer, cursorShape);
}

void RfbClient::sendClipboard(const StringStorage *newClipboard)
{
  m_clipboardExchange->sendClipboard(newClipboard);
}

void RfbClient::onKeyboardEvent(UINT32 keySym, bool down)
{
  m_desktop->setKeyboardEvent(keySym, down);
}

void RfbClient::onMouseEvent(UINT16 x, UINT16 y, UINT8 buttonMask)
{
  PixelFormat pfStub;
  Dimension fbDim;
  m_desktop->getFrameBufferProperties(&fbDim, &pfStub);

  Rect vp;
  bool shareApp;
  Region sharedRegion;
  getViewPortInfo(&fbDim, &vp, &shareApp, &sharedRegion);

  if (!shareApp) {
    sharedRegion.clear();
    sharedRegion.addRect(&vp);
  }
  bool pointInside = sharedRegion.isPointInside(x + vp.left, y + vp.top);

  if (pointInside) {
    m_updateSender->blockCursorPosSending();
    m_desktop->setMouseEvent(x + vp.left, y + vp.top, buttonMask);
  }
}

Rect RfbClient::getViewPortRect(const Dimension *fbDimension)
{
  AutoLock al(&m_viewPortMutex);
  m_constViewPort.update(fbDimension);
  m_dynamicViewPort.update(fbDimension);

  return m_constViewPort.getViewPortRect().intersection(
    &m_dynamicViewPort.getViewPortRect());
}

void RfbClient::getViewPortInfo(const Dimension *fbDimension, Rect *resultRect,
                                bool *shareApp, Region *shareAppRegion)
{
  AutoLock al(&m_viewPortMutex);

  *resultRect = getViewPortRect(fbDimension);
  *shareApp = m_dynamicViewPort.getOnlyApplication();
  if (*shareApp) {
    m_dynamicViewPort.getApplicationRegion(shareAppRegion);
  }
}

void RfbClient::onGetViewPort(Rect *viewRect, bool *shareApp, Region *shareAppRegion)
{
  PixelFormat pfStub;
  Dimension fbDim;
  m_desktop->getFrameBufferProperties(&fbDim, &pfStub);
  getViewPortInfo(&fbDim, viewRect, shareApp, shareAppRegion);
}
