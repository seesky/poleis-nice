/*****************************************************************************
Copyright (c) 2001 - 2011, The Board of Trustees of the University of Illinois.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

* Redistributions of source code must retain the above
  copyright notice, this list of conditions and the
  following disclaimer.

* Redistributions in binary form must reproduce the
  above copyright notice, this list of conditions
  and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the University of Illinois
  nor the names of its contributors may be used to
  endorse or promote products derived from this
  software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS"
IS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*****************************************************************************/

/*****************************************************************************
written by
   ChatGPT
*****************************************************************************/

#ifndef __UDT_NICE_CHANNEL_H__
#define __UDT_NICE_CHANNEL_H__

#ifdef USE_LIBNICE

#include "udt.h"
#include "packet.h"

#include <nice/agent.h>
#include <glib.h>
#include <queue>

class CNiceChannel
{
public:
   CNiceChannel();
   CNiceChannel(int version);
   ~CNiceChannel();

   void open(const sockaddr* addr = NULL);
   void open(UDPSOCKET udpsock);
   void close() const;

   int getSndBufSize();
   int getRcvBufSize();
   void setSndBufSize(int size);
   void setRcvBufSize(int size);
   void getSockAddr(sockaddr* addr) const;
   void getPeerAddr(sockaddr* addr) const;

   int sendto(const sockaddr* addr, CPacket& packet) const;
   int recvfrom(sockaddr* addr, CPacket& packet) const;

private:
   static void cb_recv(NiceAgent* agent, guint stream_id, guint component_id,
                       guint len, gchar* buf, gpointer data);
   static gpointer cb_loop(gpointer data);

private:
   NiceAgent*     m_pAgent;
   guint          m_iStreamID;
   guint          m_iComponentID;
   GMainContext*  m_pContext;
   GMainLoop*     m_pLoop;
   GThread*       m_pThread;
   GAsyncQueue*   m_pRecvQueue;

   int            m_iSndBufSize;
   int            m_iRcvBufSize;
};

#endif // USE_LIBNICE

#endif
