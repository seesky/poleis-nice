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
#include <string>
#include <vector>

class CNiceChannel
{
public:
   CNiceChannel(bool controlling = false);
   CNiceChannel(int version, bool controlling = false);
   ~CNiceChannel();

   void open(const sockaddr* addr = NULL);
   void open(UDPSOCKET udpsock);
   void close();

   int getSndBufSize();
   int getRcvBufSize();
   void setSndBufSize(int size);
   void setRcvBufSize(int size);
   void getSockAddr(sockaddr* addr) const;
   void getPeerAddr(sockaddr* addr) const;

   int sendto(const sockaddr* addr, CPacket& packet) const;
   int recvfrom(sockaddr* addr, CPacket& packet) const;

   // Block until the underlying libnice component reports READY or CONNECTED.
   // Returns true on success, or false if FAILED or the timeout (ms) expires.
   bool waitUntilConnected(int timeout_ms = 30000);

   // Retrieve local ICE username fragment and password. Returns 0 on success.
   int getLocalCredentials(std::string& ufrag, std::string& pwd) const;

   // Obtain a list of local candidates in SDP attribute format.
   int getLocalCandidates(std::vector<std::string>& candidates) const;

   // Supply remote ICE username fragment and password.
   int setRemoteCredentials(const std::string& ufrag, const std::string& pwd);

   // Provide remote candidates (each in SDP attribute format) prior to checks.
   int setRemoteCandidates(const std::vector<std::string>& candidates);

   // Block until candidate gathering has completed.
   void waitForCandidates();

   // Specify whether this agent is in controlling mode.
   void setControllingMode(bool controlling);

private:
   struct SendRequest
   {
      CNiceChannel*       channel;
      guint8*              buffer;
      guint                size;
      int                  result;
      bool                 completed;
      bool                 tracked;
      bool                 pending;
      gint                 ref_count;
      GMutex               mutex;
      GCond                cond;

      SendRequest()
      : channel(NULL)
      , buffer(NULL)
      , size(0)
      , result(-1)
      , completed(false)
      , tracked(false)
      , pending(false)
      , ref_count(1)
      {
         g_mutex_init(&mutex);
         g_cond_init(&cond);
      }

      ~SendRequest()
      {
         g_cond_clear(&cond);
         g_mutex_clear(&mutex);
         if (buffer)
         {
            g_free(buffer);
            buffer = NULL;
         }
      }

      void ref()
      {
         g_atomic_int_inc(&ref_count);
      }

      void unref()
      {
         if (g_atomic_int_dec_and_test(&ref_count))
            delete this;
      }
   };

   static void cb_recv(NiceAgent* agent, guint stream_id, guint component_id,
                       guint len, gchar* buf, gpointer data);
   static gpointer cb_loop(gpointer data);
   static void cb_state_changed(NiceAgent* agent, guint stream_id,
                               guint component_id, guint state,
                               gpointer data);
   static void cb_candidate_gathering_done(NiceAgent* agent, guint stream_id,
                                           gpointer data);
   static gboolean cb_send_dispatch(gpointer data);
   static void destroy_send_request(gpointer data);

   typedef gint (*NiceAgentSendFunc)(NiceAgent*, guint, guint, guint, const gchar*);

public:
   static void SetAgentSendFuncForTesting(NiceAgentSendFunc func);

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

   GMutex         m_StateLock;
   GCond          m_StateCond;
   bool           m_bConnected;
   bool           m_bFailed;
   bool           m_bGatheringDone;
   bool           m_bControlling;
   mutable sockaddr_storage m_SockAddr;
   mutable sockaddr_storage m_PeerAddr;
   mutable GMutex m_CloseLock;
   mutable GCond  m_CloseCond;
   mutable bool   m_bClosing;
   mutable guint  m_ActiveSends;

   static NiceAgentSendFunc s_SendFunc;
   static gsize s_DebugInitToken;
   static gboolean s_DebugLoggingEnabled;
   static void EnsureDebugLoggingInitialized();
   static gboolean IsDebugLoggingEnabled();
   static void DebugLog(const gchar* format, ...) G_GNUC_PRINTF(1, 2);
};

#endif // USE_LIBNICE

#endif
