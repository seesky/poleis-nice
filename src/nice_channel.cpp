#ifdef USE_LIBNICE

#include "nice_channel.h"
#include <nice/agent.h>
#include <nice/address.h>
#include <cstring>
#ifndef WIN32
#include <arpa/inet.h>
#else
#include <winsock2.h>
#endif

CNiceChannel::CNiceChannel(bool controlling):
m_pAgent(NULL),
m_iStreamID(0),
m_iComponentID(0),
m_pContext(NULL),
m_pLoop(NULL),
m_pThread(NULL),
m_pRecvQueue(NULL),
m_iSndBufSize(65536),
m_iRcvBufSize(65536),
m_bConnected(false),
m_bFailed(false),
m_bGatheringDone(false),
m_bControlling(controlling)
{
   g_mutex_init(&m_StateLock);
   g_cond_init(&m_StateCond);
   memset(&m_SockAddr, 0, sizeof(m_SockAddr));
   memset(&m_PeerAddr, 0, sizeof(m_PeerAddr));
}

CNiceChannel::CNiceChannel(int version, bool controlling):
m_pAgent(NULL),
m_iStreamID(0),
m_iComponentID(0),
m_pContext(NULL),
m_pLoop(NULL),
m_pThread(NULL),
m_pRecvQueue(NULL),
m_iSndBufSize(65536),
m_iRcvBufSize(65536),
m_bConnected(false),
m_bFailed(false),
m_bGatheringDone(false),
m_bControlling(controlling)
{
   g_mutex_init(&m_StateLock);
   g_cond_init(&m_StateCond);
   memset(&m_SockAddr, 0, sizeof(m_SockAddr));
   memset(&m_PeerAddr, 0, sizeof(m_PeerAddr));
}

CNiceChannel::~CNiceChannel()
{
   close();
   g_cond_clear(&m_StateCond);
   g_mutex_clear(&m_StateLock);
}

void CNiceChannel::open(const sockaddr* addr)
{
   try
   {
      m_bConnected = false;
      m_bFailed = false;
      m_bGatheringDone = false;
      m_pContext = g_main_context_new();
      if (NULL == m_pContext)
         throw CUDTException(3, 2, 0);

      m_pLoop = g_main_loop_new(m_pContext, FALSE);
      if (NULL == m_pLoop)
         throw CUDTException(3, 2, 0);

      m_pAgent = nice_agent_new(m_pContext, NICE_COMPATIBILITY_RFC5245);
      if (NULL == m_pAgent)
         throw CUDTException(3, 2, 0);

      g_object_set(G_OBJECT(m_pAgent), "controlling-mode", m_bControlling ? TRUE : FALSE, NULL);

      m_iStreamID = nice_agent_add_stream(m_pAgent, 1);
      if (0 == m_iStreamID)
         throw CUDTException(3, 2, 0);
      m_iComponentID = 1;

      m_pRecvQueue = g_async_queue_new();
      if (NULL == m_pRecvQueue)
         throw CUDTException(3, 2, 0);

      if (!nice_agent_attach_recv(m_pAgent, m_iStreamID, m_iComponentID,
                                  m_pContext, &CNiceChannel::cb_recv, this))
         throw CUDTException(3, 1, 0);

      g_signal_connect(G_OBJECT(m_pAgent), "component-state-changed",
                       G_CALLBACK(CNiceChannel::cb_state_changed), this);
      g_signal_connect(G_OBJECT(m_pAgent), "candidate-gathering-done",
                       G_CALLBACK(CNiceChannel::cb_candidate_gathering_done), this);

      m_pThread = g_thread_new("nice-loop", &CNiceChannel::cb_loop, this);
      if (NULL == m_pThread)
         throw CUDTException(3, 1, 0);

      if (!nice_agent_gather_candidates(m_pAgent, m_iStreamID))
         throw CUDTException(3, 1, 0);
   }
   catch (CUDTException& e)
   {
      close();
      throw;
   }
}

void CNiceChannel::open(UDPSOCKET udpsock)
{
   open();
}

void CNiceChannel::close()
{
   if (m_pAgent)
   {
      // Detach any receive callback and stop the stream so that libnice
      // stops all internal processing before we tear down its context.
      nice_agent_attach_recv(m_pAgent, m_iStreamID, m_iComponentID, NULL, NULL, NULL);
      if (m_iStreamID > 0)
         nice_agent_remove_stream(m_pAgent, m_iStreamID);
   }

   if (m_pLoop)
      g_main_loop_quit(m_pLoop);

   if (m_pRecvQueue)
      g_async_queue_push(m_pRecvQueue, NULL);

   if (m_pThread)
   {
      g_thread_join(m_pThread);
      m_pThread = NULL;
   }

   if (m_pAgent)
   {
      g_object_unref(m_pAgent);
      m_pAgent = NULL;
   }
   if (m_pLoop)
   {
      g_main_loop_unref(m_pLoop);
      m_pLoop = NULL;
   }
   if (m_pContext)
   {
      g_main_context_unref(m_pContext);
      m_pContext = NULL;
   }
   if (m_pRecvQueue)
   {
      g_async_queue_unref(m_pRecvQueue);
      m_pRecvQueue = NULL;
   }
}

int CNiceChannel::getSndBufSize()
{
   return m_iSndBufSize;
}

int CNiceChannel::getRcvBufSize()
{
   return m_iRcvBufSize;
}

void CNiceChannel::setSndBufSize(int size)
{
   m_iSndBufSize = size;
}

void CNiceChannel::setRcvBufSize(int size)
{
   m_iRcvBufSize = size;
}

void CNiceChannel::getSockAddr(sockaddr* addr) const
{
   if (addr)
   {
      if (!m_SockAddr.ss_family)
      {
         NiceCandidate* lc = NULL;
         NiceCandidate* rc = NULL;
         if (nice_agent_get_selected_pair(m_pAgent, m_iStreamID, m_iComponentID, &lc, &rc))
         {
            if (lc)
            {
               nice_address_copy_to_sockaddr(&lc->addr, (struct sockaddr*)&m_SockAddr);
               nice_candidate_free(lc);
            }
            if (rc)
            {
               nice_address_copy_to_sockaddr(&rc->addr, (struct sockaddr*)&m_PeerAddr);
               nice_candidate_free(rc);
            }
         }
      }

      if (m_SockAddr.ss_family)
      {
         if (AF_INET == m_SockAddr.ss_family)
            memcpy(addr, &m_SockAddr, sizeof(sockaddr_in));
         else
            memcpy(addr, &m_SockAddr, sizeof(sockaddr_in6));
      }
      else
         memset(addr, 0, sizeof(sockaddr));
   }
}

void CNiceChannel::getPeerAddr(sockaddr* addr) const
{
   if (addr)
   {
      if (!m_PeerAddr.ss_family)
      {
         NiceCandidate* lc = NULL;
         NiceCandidate* rc = NULL;
         if (nice_agent_get_selected_pair(m_pAgent, m_iStreamID, m_iComponentID, &lc, &rc))
         {
            if (lc)
            {
               nice_address_copy_to_sockaddr(&lc->addr, (struct sockaddr*)&m_SockAddr);
               nice_candidate_free(lc);
            }
            if (rc)
            {
               nice_address_copy_to_sockaddr(&rc->addr, (struct sockaddr*)&m_PeerAddr);
               nice_candidate_free(rc);
            }
         }
      }

      if (m_PeerAddr.ss_family)
      {
         if (AF_INET == m_PeerAddr.ss_family)
            memcpy(addr, &m_PeerAddr, sizeof(sockaddr_in));
         else
            memcpy(addr, &m_PeerAddr, sizeof(sockaddr_in6));
      }
      else
         memset(addr, 0, sizeof(sockaddr));
   }
}

int CNiceChannel::sendto(const sockaddr* addr, CPacket& packet) const
{
   if (packet.getFlag())
      for (int i = 0, n = packet.getLength() / 4; i < n; ++ i)
         *((uint32_t *)packet.m_pcData + i) = htonl(*((uint32_t *)packet.m_pcData + i));

   uint32_t* p = packet.header();
   for (int j = 0; j < 4; ++ j)
   {
      *p = htonl(*p);
      ++ p;
   }

   const int size = CPacket::m_iPktHdrSize + packet.getLength();
   guint8* buf = (guint8*)g_malloc(size);
   memcpy(buf, packet.header(), CPacket::m_iPktHdrSize);
   memcpy(buf + CPacket::m_iPktHdrSize, packet.m_pcData, packet.getLength());

   int result = -1;

   if (m_pContext && m_pAgent)
   {
      SendRequest request;
      request.channel = this;
      request.buffer = buf;
      request.size = size;

      guint source_id = g_main_context_invoke_full(m_pContext,
                                                   G_PRIORITY_DEFAULT,
                                                   &CNiceChannel::cb_send_dispatch,
                                                   &request,
                                                   NULL);

      if (0 == source_id)
      {
         if (request.buffer)
         {
            g_free(request.buffer);
            request.buffer = NULL;
         }
      }
      else
      {
         g_mutex_lock(&request.mutex);
         while (!request.completed)
            g_cond_wait(&request.cond, &request.mutex);
         result = request.result;
         g_mutex_unlock(&request.mutex);
      }

      if (request.buffer)
         g_free(request.buffer);
   }
   else
   {
      g_free(buf);
   }

   p = packet.header();
   for (int k = 0; k < 4; ++ k)
   {
      *p = ntohl(*p);
      ++ p;
   }

   if (packet.getFlag())
   {
      for (int l = 0, n = packet.getLength() / 4; l < n; ++ l)
         *((uint32_t *)packet.m_pcData + l) = ntohl(*((uint32_t *)packet.m_pcData + l));
   }

   return result;
}

int CNiceChannel::recvfrom(sockaddr* addr, CPacket& packet) const
{
   if (addr)
      memset(addr, 0, sizeof(sockaddr));

   if (addr && !m_PeerAddr.ss_family)
   {
      NiceCandidate* lc = NULL;
      NiceCandidate* rc = NULL;
      if (nice_agent_get_selected_pair(m_pAgent, m_iStreamID, m_iComponentID, &lc, &rc))
      {
         if (lc)
         {
            nice_address_copy_to_sockaddr(&lc->addr, (struct sockaddr*)&m_SockAddr);
            nice_candidate_free(lc);
         }
         if (rc)
         {
            nice_address_copy_to_sockaddr(&rc->addr, (struct sockaddr*)&m_PeerAddr);
            if (addr)
            {
               if (AF_INET == m_PeerAddr.ss_family)
                  memcpy(addr, &m_PeerAddr, sizeof(sockaddr_in));
               else
                  memcpy(addr, &m_PeerAddr, sizeof(sockaddr_in6));
            }
            nice_candidate_free(rc);
         }
      }
   }
   else if (addr && m_PeerAddr.ss_family)
   {
      if (AF_INET == m_PeerAddr.ss_family)
         memcpy(addr, &m_PeerAddr, sizeof(sockaddr_in));
      else
         memcpy(addr, &m_PeerAddr, sizeof(sockaddr_in6));
   }

   GByteArray* arr = (GByteArray*)g_async_queue_pop(m_pRecvQueue);
   if (NULL == arr)
      return -1;

   int size = arr->len;
   if (size < CPacket::m_iPktHdrSize)
   {
      g_byte_array_unref(arr);
      packet.setLength(-1);
      return -1;
   }

   packet.setHeader(reinterpret_cast<const uint32_t*>(arr->data));
   memcpy(packet.m_pcData, arr->data + CPacket::m_iPktHdrSize, size - CPacket::m_iPktHdrSize);
   g_byte_array_unref(arr);

   packet.setLength(size - CPacket::m_iPktHdrSize);

   uint32_t* p = packet.header();
   for (int i = 0; i < 4; ++ i)
   {
      *p = ntohl(*p);
      ++ p;
   }

   if (packet.getFlag())
   {
      for (int j = 0, n = packet.getLength() / 4; j < n; ++ j)
         *((uint32_t *)packet.m_pcData + j) = ntohl(*((uint32_t *)packet.m_pcData + j));
   }

   return packet.getLength();
}

gboolean CNiceChannel::cb_send_dispatch(gpointer data)
{
   SendRequest* request = static_cast<SendRequest*>(data);

   g_mutex_lock(&request->mutex);

   if (request->buffer && request->channel->m_pAgent)
      request->result = nice_agent_send(request->channel->m_pAgent,
                                        request->channel->m_iStreamID,
                                        request->channel->m_iComponentID,
                                        request->size,
                                        (char*)request->buffer);
   else
      request->result = -1;

   if (request->buffer)
   {
      g_free(request->buffer);
      request->buffer = NULL;
   }

   request->completed = true;
   g_cond_signal(&request->cond);
   g_mutex_unlock(&request->mutex);

   return G_SOURCE_REMOVE;
}

int CNiceChannel::getLocalCredentials(std::string& ufrag, std::string& pwd) const
{
   gchar* lu = NULL;
   gchar* lp = NULL;
   if (!nice_agent_get_local_credentials(m_pAgent, m_iStreamID, &lu, &lp))
      return -1;
   if (lu)
   {
      ufrag = lu;
      g_free(lu);
   }
   if (lp)
   {
      pwd = lp;
      g_free(lp);
   }
   return 0;
}

int CNiceChannel::getLocalCandidates(std::vector<std::string>& candidates) const
{
   GSList* list = nice_agent_get_local_candidates(m_pAgent, m_iStreamID, m_iComponentID);
   for (GSList* item = list; item; item = item->next)
   {
      NiceCandidate* c = (NiceCandidate*)item->data;
      gchar* cand = nice_agent_generate_local_candidate_sdp(m_pAgent, c);
      if (cand)
      {
         candidates.push_back(cand);
         g_free(cand);
      }
   }
   g_slist_free_full(list, (GDestroyNotify)nice_candidate_free);
   return candidates.size();
}

int CNiceChannel::setRemoteCredentials(const std::string& ufrag, const std::string& pwd)
{
   return nice_agent_set_remote_credentials(m_pAgent, m_iStreamID, ufrag.c_str(), pwd.c_str());
}

int CNiceChannel::setRemoteCandidates(const std::vector<std::string>& candidates)
{
   GSList* list = NULL;
   for (std::vector<std::string>::const_iterator it = candidates.begin(); it != candidates.end(); ++ it)
   {
      NiceCandidate* c = nice_agent_parse_remote_candidate_sdp(m_pAgent, m_iStreamID, it->c_str());
      if (c)
      {
         if (c->component_id == m_iComponentID)
            list = g_slist_append(list, c);
         else
            nice_candidate_free(c);
      }
   }
   int r = nice_agent_set_remote_candidates(m_pAgent, m_iStreamID, m_iComponentID, list);
   g_slist_free_full(list, (GDestroyNotify)nice_candidate_free);
   return r;
}

void CNiceChannel::setControllingMode(bool controlling)
{
   m_bControlling = controlling;
   if (m_pAgent)
      g_object_set(G_OBJECT(m_pAgent), "controlling-mode", m_bControlling ? TRUE : FALSE, NULL);
}

void CNiceChannel::waitForCandidates()
{
   g_mutex_lock(&m_StateLock);
   while (!m_bGatheringDone && !m_bFailed)
   {
      g_cond_wait(&m_StateCond, &m_StateLock);
   }
   g_mutex_unlock(&m_StateLock);
}

bool CNiceChannel::waitUntilConnected(int timeout_ms)
{
   gint64 end_time = 0;
   if (timeout_ms > 0)
      end_time = g_get_monotonic_time() + timeout_ms * G_TIME_SPAN_MILLISECOND;

   g_mutex_lock(&m_StateLock);
   while (!m_bConnected && !m_bFailed)
   {
      if (timeout_ms > 0)
      {
         if (!g_cond_wait_until(&m_StateCond, &m_StateLock, end_time))
            break;
      }
      else
      {
         g_cond_wait(&m_StateCond, &m_StateLock);
      }
   }
   bool res = m_bConnected;
   g_mutex_unlock(&m_StateLock);
   return res;
}

void CNiceChannel::cb_recv(NiceAgent* agent, guint stream_id, guint component_id,
                           guint len, gchar* buf, gpointer data)
{
   CNiceChannel* self = (CNiceChannel*)data;
   if (!self->m_pRecvQueue)
      return;
   GByteArray* arr = g_byte_array_sized_new(len);
   g_byte_array_append(arr, (guint8*)buf, len);
   g_async_queue_push(self->m_pRecvQueue, arr);
}

void CNiceChannel::cb_candidate_gathering_done(NiceAgent* agent, guint stream_id,
                                               gpointer data)
{
   CNiceChannel* self = (CNiceChannel*)data;
   g_mutex_lock(&self->m_StateLock);
   self->m_bGatheringDone = true;
   g_cond_broadcast(&self->m_StateCond);
   g_mutex_unlock(&self->m_StateLock);
}

void CNiceChannel::cb_state_changed(NiceAgent* agent, guint stream_id,
                                    guint component_id, guint state,
                                    gpointer data)
{
   CNiceChannel* self = (CNiceChannel*)data;
   g_mutex_lock(&self->m_StateLock);
   if (state == NICE_COMPONENT_STATE_READY || state == NICE_COMPONENT_STATE_CONNECTED)
   {
      self->m_bConnected = true;
      g_cond_broadcast(&self->m_StateCond);
   }
   else if (state == NICE_COMPONENT_STATE_FAILED)
   {
      self->m_bFailed = true;
      g_cond_broadcast(&self->m_StateCond);
   }
   g_mutex_unlock(&self->m_StateLock);
}

gpointer CNiceChannel::cb_loop(gpointer data)
{
   CNiceChannel* self = (CNiceChannel*)data;
   g_main_loop_run(self->m_pLoop);
   return NULL;
}

#endif
