#ifdef USE_LIBNICE

#include "nice_channel.h"
#include <nice/agent.h>
#include <cstring>
#ifndef WIN32
#include <arpa/inet.h>
#else
#include <winsock2.h>
#endif

CNiceChannel::CNiceChannel():
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
m_bFailed(false)
{
   g_mutex_init(&m_StateLock);
   g_cond_init(&m_StateCond);
}

CNiceChannel::CNiceChannel(int version):
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
m_bFailed(false)
{
   g_mutex_init(&m_StateLock);
   g_cond_init(&m_StateCond);
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
      m_pContext = g_main_context_new();
      if (NULL == m_pContext)
         throw CUDTException(3, 2, 0);

      m_pLoop = g_main_loop_new(m_pContext, FALSE);
      if (NULL == m_pLoop)
         throw CUDTException(3, 2, 0);

      m_pAgent = nice_agent_new(m_pContext, NICE_COMPATIBILITY_RFC5245);
      if (NULL == m_pAgent)
         throw CUDTException(3, 2, 0);

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

      m_pThread = g_thread_new("nice-loop", &CNiceChannel::cb_loop, this);
      if (NULL == m_pThread)
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
      nice_agent_attach_recv(m_pAgent, m_iStreamID, m_iComponentID, NULL, NULL, NULL);

   if (m_pLoop)
      g_main_loop_quit(m_pLoop);

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
      memset(addr, 0, sizeof(sockaddr));
}

void CNiceChannel::getPeerAddr(sockaddr* addr) const
{
   if (addr)
      memset(addr, 0, sizeof(sockaddr));
}

int CNiceChannel::sendto(const sockaddr* addr, CPacket& packet) const
{
   if (packet.getFlag())
      for (int i = 0, n = packet.getLength() / 4; i < n; ++ i)
         *((uint32_t *)packet.m_pcData + i) = htonl(*((uint32_t *)packet.m_pcData + i));

   uint32_t* p = packet.m_nHeader;
   for (int j = 0; j < 4; ++ j)
   {
      *p = htonl(*p);
      ++ p;
   }

   int size = CPacket::m_iPktHdrSize + packet.getLength();
   guint8* buf = (guint8*)g_malloc(size);
   memcpy(buf, packet.m_nHeader, CPacket::m_iPktHdrSize);
   memcpy(buf + CPacket::m_iPktHdrSize, packet.m_pcData, packet.getLength());

   int res = nice_agent_send(m_pAgent, m_iStreamID, m_iComponentID, size, (char*)buf);
   g_free(buf);

   p = packet.m_nHeader;
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

   return res;
}

int CNiceChannel::recvfrom(sockaddr* addr, CPacket& packet) const
{
   if (addr)
      memset(addr, 0, sizeof(sockaddr));

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

   memcpy(packet.m_nHeader, arr->data, CPacket::m_iPktHdrSize);
   memcpy(packet.m_pcData, arr->data + CPacket::m_iPktHdrSize, size - CPacket::m_iPktHdrSize);
   g_byte_array_unref(arr);

   packet.setLength(size - CPacket::m_iPktHdrSize);

   uint32_t* p = packet.m_nHeader;
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
      NiceCandidate* c = NULL;
      if (nice_agent_parse_remote_candidate_sdp(m_pAgent, m_iStreamID, m_iComponentID, it->c_str(), &c))
         list = g_slist_append(list, c);
   }
   int r = nice_agent_set_remote_candidates(m_pAgent, m_iStreamID, m_iComponentID, list);
   g_slist_free_full(list, (GDestroyNotify)nice_candidate_free);
   return r;
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
