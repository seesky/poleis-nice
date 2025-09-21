#ifdef USE_LIBNICE

#include "nice_channel.h"
#include <nice/agent.h>
#include <nice/address.h>
#include <cstring>
#include <cerrno>
#include <cstdarg>
#include <type_traits>
#ifndef WIN32
#include <arpa/inet.h>
#include <sys/socket.h>
#else
#include <winsock2.h>
#endif

#ifdef NICE_CHECK_VERSION
#if NICE_CHECK_VERSION(0, 1, 18)
#define POLEIS_NICE_HAS_STUN_SERVER_HELPER 0
#else
#define POLEIS_NICE_HAS_STUN_SERVER_HELPER 1
#endif
#if NICE_CHECK_VERSION(0, 1, 19)
#define POLEIS_NICE_PORT_RANGE_RETURNS_GBOOLEAN 0
#else
#define POLEIS_NICE_PORT_RANGE_RETURNS_GBOOLEAN 1
#endif
#else
#define POLEIS_NICE_HAS_STUN_SERVER_HELPER 1
#define POLEIS_NICE_PORT_RANGE_RETURNS_GBOOLEAN 1
#endif

CNiceChannel::NiceAgentSendFunc CNiceChannel::s_SendFunc = nice_agent_send;
gsize CNiceChannel::s_DebugInitToken = 0;
gboolean CNiceChannel::s_DebugLoggingEnabled = FALSE;

namespace
{
using NiceAgentSetPortRangeReturnType =
   decltype(nice_agent_set_port_range(static_cast<NiceAgent*>(NULL),
                                      static_cast<guint>(0),
                                      static_cast<guint>(0),
                                      static_cast<guint>(0),
                                      static_cast<guint>(0)));

template <typename ReturnType>
struct NiceAgentSetPortRangeInvoker
{
   static void Invoke(NiceAgent*, guint, guint, guint, guint)
   {
      static_assert(std::is_same<ReturnType, gboolean>::value ||
                    std::is_same<ReturnType, void>::value,
                    "Unsupported nice_agent_set_port_range signature");
   }
};

#if POLEIS_NICE_PORT_RANGE_RETURNS_GBOOLEAN
template <>
struct NiceAgentSetPortRangeInvoker<gboolean>
{
   static void Invoke(NiceAgent* agent,
                      guint stream_id,
                      guint component_id,
                      guint port_min,
                      guint port_max)
   {
      nice_agent_set_port_range(agent, stream_id, component_id, port_min, port_max);
   }
};
#else
template <>
struct NiceAgentSetPortRangeInvoker<void>
{
   static void Invoke(NiceAgent* agent,
                      guint stream_id,
                      guint component_id,
                      guint port_min,
                      guint port_max)
   {
      nice_agent_set_port_range(agent, stream_id, component_id,
                                port_min, port_max);
      // Newer libnice releases return void, indicating the call cannot fail.
   }
};
#endif

gboolean EnvValueEnablesDebug(const gchar* value)
{
   if (!value)
      return FALSE;

   gchar* copy = g_strdup(value);
   gchar* stripped = g_strstrip(copy);
   gboolean enabled = FALSE;

   if (*stripped)
   {
      if ((g_ascii_strcasecmp(stripped, "0") != 0) &&
          (g_ascii_strcasecmp(stripped, "false") != 0) &&
          (g_ascii_strcasecmp(stripped, "off") != 0) &&
          (g_ascii_strcasecmp(stripped, "no") != 0))
      {
         enabled = TRUE;
      }
   }

   g_free(copy);
   return enabled;
}

const gchar* NiceComponentStateToString(guint state)
{
   switch (state)
   {
   case NICE_COMPONENT_STATE_DISCONNECTED:
      return "DISCONNECTED";
   case NICE_COMPONENT_STATE_GATHERING:
      return "GATHERING";
   case NICE_COMPONENT_STATE_CONNECTING:
      return "CONNECTING";
   case NICE_COMPONENT_STATE_CONNECTED:
      return "CONNECTED";
   case NICE_COMPONENT_STATE_READY:
      return "READY";
   case NICE_COMPONENT_STATE_FAILED:
      return "FAILED";
   case NICE_COMPONENT_STATE_LAST:
      return "LAST";
   default:
      break;
   }
   return "UNKNOWN";
}
}

void CNiceChannel::EnsureDebugLoggingInitialized()
{
   if (g_once_init_enter(&s_DebugInitToken))
   {
      const gchar* value = g_getenv("LIBNICE_DEBUG");
      if (!value)
         value = g_getenv("POLEIS_LIBNICE_DEBUG");
      if (!value)
         value = g_getenv("POLEIS_DEBUG");

      s_DebugLoggingEnabled = EnvValueEnablesDebug(value);
      if (s_DebugLoggingEnabled)
      {
         const gchar* logged = value ? value : "(set)";
         g_log("poleis-nice", G_LOG_LEVEL_MESSAGE,
               "libnice debug logging enabled via environment value '%s'", logged);
      }

      g_once_init_leave(&s_DebugInitToken, 1);
   }
}

gboolean CNiceChannel::IsDebugLoggingEnabled()
{
   EnsureDebugLoggingInitialized();
   return s_DebugLoggingEnabled;
}

void CNiceChannel::DebugLog(const gchar* format, ...)
{
   if (!IsDebugLoggingEnabled())
      return;

   va_list args;
   va_start(args, format);
   gchar* message = g_strdup_vprintf(format, args);
   va_end(args);

   g_log("poleis-nice", G_LOG_LEVEL_MESSAGE, "%s", message);
   g_free(message);
}

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
m_bControlling(controlling),
m_bClosing(false),
m_ActiveSends(0),
m_bHasStunServer(false),
m_StunPort(0),
m_bHasTurnRelay(false),
m_TurnPort(0),
m_TurnType(NICE_RELAY_TYPE_TURN_UDP),
m_bHasPortRange(false),
m_PortRangeMin(0),
m_PortRangeMax(0)
{
   g_mutex_init(&m_StateLock);
   g_cond_init(&m_StateCond);
   g_mutex_init(&m_CloseLock);
   g_cond_init(&m_CloseCond);
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
m_bControlling(controlling),
m_bClosing(false),
m_ActiveSends(0),
m_bHasStunServer(false),
m_StunPort(0),
m_bHasTurnRelay(false),
m_TurnPort(0),
m_TurnType(NICE_RELAY_TYPE_TURN_UDP),
m_bHasPortRange(false),
m_PortRangeMin(0),
m_PortRangeMax(0)
{
   g_mutex_init(&m_StateLock);
   g_cond_init(&m_StateCond);
   g_mutex_init(&m_CloseLock);
   g_cond_init(&m_CloseCond);
   memset(&m_SockAddr, 0, sizeof(m_SockAddr));
   memset(&m_PeerAddr, 0, sizeof(m_PeerAddr));
}

CNiceChannel::~CNiceChannel()
{
   close();
   g_cond_clear(&m_StateCond);
   g_mutex_clear(&m_StateLock);
   g_cond_clear(&m_CloseCond);
   g_mutex_clear(&m_CloseLock);
}

void CNiceChannel::SetAgentSendFuncForTesting(NiceAgentSendFunc func)
{
   s_SendFunc = func ? func : nice_agent_send;
}

void CNiceChannel::open(const sockaddr* addr)
{
   try
   {
      EnsureDebugLoggingInitialized();
      DebugLog("Opening libnice channel (controlling=%s)",
               m_bControlling ? "true" : "false");

      g_mutex_lock(&m_CloseLock);
      m_bClosing = false;
      m_ActiveSends = 0;
      g_mutex_unlock(&m_CloseLock);

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

      DebugLog("Created NiceAgent %p", static_cast<void*>(m_pAgent));

      g_object_set(G_OBJECT(m_pAgent), "controlling-mode", m_bControlling ? TRUE : FALSE, NULL);

      m_iStreamID = nice_agent_add_stream(m_pAgent, 1);
      if (0 == m_iStreamID)
         throw CUDTException(3, 2, 0);
      m_iComponentID = 1;

      DebugLog("Added stream %u component %u", m_iStreamID, m_iComponentID);

      m_pRecvQueue = g_async_queue_new();
      if (NULL == m_pRecvQueue)
         throw CUDTException(3, 2, 0);

      if (m_bHasStunServer)
      {
         guint port = m_StunPort ? m_StunPort : 3478;
         DebugLog("Configuring STUN server %s:%u", m_StunServer.c_str(), port);

         g_object_set(G_OBJECT(m_pAgent),
                      "stun-server", m_StunServer.c_str(),
                      "stun-server-port", port,
                      NULL);

      }

      if (m_bHasTurnRelay)
      {
         guint port = m_TurnPort ? m_TurnPort : 3478;
         DebugLog("Configuring TURN relay %s:%u (username=%s)",
                  m_TurnServer.c_str(), port, m_TurnUsername.c_str());
         if (!nice_agent_set_relay_info(m_pAgent, m_iStreamID, m_iComponentID,
                                        m_TurnServer.c_str(), port,
                                        m_TurnUsername.c_str(), m_TurnPassword.c_str(),
                                        m_TurnType))
            throw CUDTException(3, 1, 0);
      }

      if (m_bHasPortRange)
      {
         DebugLog("Restricting component %u to port range %u-%u",
                  m_iComponentID, m_PortRangeMin, m_PortRangeMax);
         NiceAgentSetPortRangeInvoker<NiceAgentSetPortRangeReturnType>::Invoke(
            m_pAgent,
            m_iStreamID,
            m_iComponentID,
            m_PortRangeMin,
            m_PortRangeMax);
      }

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

      DebugLog("Started candidate gathering for stream %u", m_iStreamID);
   }
   catch (CUDTException& e)
   {
      DebugLog("Exception while opening libnice channel: %s",
               e.getErrorMessage());
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
   if (IsDebugLoggingEnabled())
      DebugLog("Closing libnice channel (agent=%p)", static_cast<void*>(m_pAgent));

   g_mutex_lock(&m_CloseLock);
   if (!m_bClosing)
      m_bClosing = true;
   while (m_ActiveSends > 0)
      g_cond_wait(&m_CloseCond, &m_CloseLock);
   g_mutex_unlock(&m_CloseLock);

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

   g_mutex_lock(&m_CloseLock);
   m_bClosing = false;
   m_ActiveSends = 0;
   g_mutex_unlock(&m_CloseLock);

   if (IsDebugLoggingEnabled())
      DebugLog("Libnice channel closed");
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
   const gsize alloc_size = static_cast<gsize>(size);
   const guint send_size = static_cast<guint>(size);
   guint8* buf = static_cast<guint8*>(g_malloc(alloc_size));
   memcpy(buf, packet.header(), CPacket::m_iPktHdrSize);
   memcpy(buf + CPacket::m_iPktHdrSize, packet.m_pcData, packet.getLength());

   int result = -1;

   if (m_pContext && m_pAgent)
   {
      CNiceChannel* self = const_cast<CNiceChannel*>(this);

      DebugLog("Scheduling send of %u bytes (stream=%u component=%u)",
               send_size, m_iStreamID, m_iComponentID);

      bool failed = false;
      g_mutex_lock(&self->m_StateLock);
      failed = self->m_bFailed;
      g_mutex_unlock(&self->m_StateLock);

      bool can_send = false;

      g_mutex_lock(&self->m_CloseLock);
      if (!failed && !self->m_bClosing && self->m_pAgent)
      {
         ++ self->m_ActiveSends;
         can_send = true;
      }
      g_mutex_unlock(&self->m_CloseLock);

      if (can_send)
      {
         SendRequest* request = new SendRequest();
         request->channel = self;
         request->buffer = buf;
         request->size = send_size;
         request->completed = false;
         request->tracked = true;
         request->pending = true;

         request->ref();
         g_main_context_invoke_full(m_pContext,
                                    G_PRIORITY_DEFAULT,
                                    &CNiceChannel::cb_send_dispatch,
                                    request,
                                    &CNiceChannel::destroy_send_request);

         g_mutex_lock(&request->mutex);
         while (!request->completed)
            g_cond_wait(&request->cond, &request->mutex);
         result = request->result;
         g_mutex_unlock(&request->mutex);

         request->unref();
      }
      else
      {
         g_free(buf);
      }
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

   CNiceChannel* self = const_cast<CNiceChannel*>(this);

   const guint64 timeout_usec = G_USEC_PER_SEC / 100;
   GByteArray* arr = NULL;
   GAsyncQueue* queue = m_pRecvQueue;
   if (queue)
      arr = static_cast<GByteArray*>(g_async_queue_timeout_pop(queue, timeout_usec));
   if (NULL == arr)
   {
      bool closing = false;
      g_mutex_lock(&self->m_CloseLock);
      closing = self->m_bClosing;
      g_mutex_unlock(&self->m_CloseLock);

      if (closing || !self->m_pRecvQueue)
      {
#ifdef WIN32
         WSASetLastError(WSAECONNRESET);
#else
         errno = EBADF;
#endif
         packet.setLength(-1);
         return -1;
      }

#ifdef WIN32
      WSASetLastError(WSAEWOULDBLOCK);
#else
      errno = EAGAIN;
#endif
      packet.setLength(-1);
      return -1;
   }

   int size = arr->len;
   if (size < CPacket::m_iPktHdrSize)
   {
      g_byte_array_unref(arr);
      packet.setLength(-1);
      return -1;
   }

   DebugLog("Received %d byte payload from libnice", size);

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

void CNiceChannel::destroy_send_request(gpointer data)
{
   SendRequest* request = static_cast<SendRequest*>(data);
   if (request)
      request->unref();
}

gboolean CNiceChannel::cb_send_dispatch(gpointer data)
{
   SendRequest* request = static_cast<SendRequest*>(data);

   g_mutex_lock(&request->mutex);

   CNiceChannel* channel = request->channel;
   bool reschedule = false;
   bool completed = false;
   bool fatal_error = false;
   const char* skip_reason = NULL;
   GMainContext* context = NULL;

   if (channel && request->tracked)
   {
      g_mutex_lock(&channel->m_CloseLock);

      context = channel->m_pContext;

      const bool closing = channel->m_bClosing;
      const bool failed = channel->m_bFailed;

      if (!closing && !failed && channel->m_pAgent && request->buffer)
      {
         request->result = s_SendFunc(channel->m_pAgent,
                                      channel->m_iStreamID,
                                      channel->m_iComponentID,
                                      request->size,
                                      (char*)request->buffer);

         if (request->result >= 0)
         {
            completed = true;
         }
         else
         {
            int err = errno;
            if ((EAGAIN == err || EWOULDBLOCK == err || ENOBUFS == err || ENOMEM == err || EINTR == err) && context)
            {
               // Temporary congestion signals from the kernel; retry the send later.
               reschedule = true;
            }
            else
            {
               completed = true;
               fatal_error = true;
               request->result = -1;
            }
         }
      }
      else
      {
         completed = true;
         request->result = -1;
         if (closing)
         {
            skip_reason = "channel closing";
         }
         else if (failed)
         {
            skip_reason = "channel already failed/disconnected";
         }
         else if (!channel->m_pAgent)
         {
            skip_reason = "channel agent missing";
            fatal_error = true;
         }
         else if (!request->buffer)
         {
            skip_reason = "send buffer missing";
            fatal_error = true;
         }
         else
         {
            fatal_error = true;
         }
      }

      if (completed && request->tracked)
      {
         if (channel->m_ActiveSends > 0)
         {
            -- channel->m_ActiveSends;
            if (0 == channel->m_ActiveSends)
               g_cond_signal(&channel->m_CloseCond);
         }
         request->tracked = false;
      }

      g_mutex_unlock(&channel->m_CloseLock);
   }
   else
   {
      request->result = -1;
      completed = true;
      fatal_error = true;
      skip_reason = "send request lost channel reference";
   }

      if (reschedule && context)
      {
         DebugLog("Send request for %u bytes deferred to main context",
                  request->size);
         request->ref();
      g_main_context_invoke_full(context,
                                 G_PRIORITY_DEFAULT,
                                 &CNiceChannel::cb_send_dispatch,
                                 request,
                                 &CNiceChannel::destroy_send_request);
   }
   else
   {
      if (request->buffer)
      {
         g_free(request->buffer);
         request->buffer = NULL;
      }

      request->completed = true;
      request->pending = false;
      g_cond_signal(&request->cond);
   }

   g_mutex_unlock(&request->mutex);

   if (skip_reason)
   {
      if (channel)
      {
         DebugLog("Send request for stream %u component %u aborted: %s",
                  channel->m_iStreamID, channel->m_iComponentID, skip_reason);
      }
      else
      {
         DebugLog("Send request aborted: %s", skip_reason);
      }
   }

   if (fatal_error && channel)
   {
      g_mutex_lock(&channel->m_StateLock);
      channel->m_bFailed = true;
      g_cond_broadcast(&channel->m_StateCond);
      g_mutex_unlock(&channel->m_StateLock);

      g_warning("Send failed for stream %u component %u; channel marked unusable",
                channel->m_iStreamID, channel->m_iComponentID);
      DebugLog("Send failed with fatal error; channel marked failed");
   }

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

   DebugLog("Retrieved local ICE credentials (ufrag length=%zu, pwd length=%zu)",
            ufrag.size(), pwd.size());
   return 0;
}

int CNiceChannel::getLocalCandidates(std::vector<std::string>& candidates) const
{
   GSList* list = nice_agent_get_local_candidates(m_pAgent, m_iStreamID, m_iComponentID);
   for (GSList* item = list; item; item = item->next)
   {
      NiceCandidate* c = static_cast<NiceCandidate*>(item->data);
      sockaddr_storage storage;
      memset(&storage, 0, sizeof(storage));

      nice_address_copy_to_sockaddr(&c->addr, reinterpret_cast<sockaddr*>(&storage));

      if (storage.ss_family != AF_INET)
         continue;

      gchar* cand = nice_agent_generate_local_candidate_sdp(m_pAgent, c);
      if (cand)
      {
         candidates.push_back(cand);
         g_free(cand);
      }
   }
   g_slist_free_full(list, (GDestroyNotify)nice_candidate_free);

   DebugLog("Collected %zu local ICE candidates", candidates.size());
   return candidates.size();
}

int CNiceChannel::setRemoteCredentials(const std::string& ufrag, const std::string& pwd)
{
   DebugLog("Applying remote ICE credentials (ufrag length=%zu, pwd length=%zu)",
            ufrag.size(), pwd.size());
   return nice_agent_set_remote_credentials(m_pAgent, m_iStreamID, ufrag.c_str(), pwd.c_str());
}

int CNiceChannel::setRemoteCandidates(const std::vector<std::string>& candidates)
{
   GSList* list = NULL;
   size_t filtered = 0;
   for (std::vector<std::string>::const_iterator it = candidates.begin(); it != candidates.end(); ++ it)
   {
      NiceCandidate* c = nice_agent_parse_remote_candidate_sdp(m_pAgent, m_iStreamID, it->c_str());
      if (c)
      {
         if (c->component_id == m_iComponentID)
         {
            sockaddr_storage storage;
            memset(&storage, 0, sizeof(storage));

            nice_address_copy_to_sockaddr(&c->addr, reinterpret_cast<sockaddr*>(&storage));
            if (storage.ss_family != AF_INET)
            {
               ++filtered;
               nice_candidate_free(c);
               continue;
            }

            list = g_slist_append(list, c);
         }
         else
         {
            nice_candidate_free(c);
         }
      }
   }
   int r = nice_agent_set_remote_candidates(m_pAgent, m_iStreamID, m_iComponentID, list);
   g_slist_free_full(list, (GDestroyNotify)nice_candidate_free);
   DebugLog("Applied %zu remote ICE candidates (%zu filtered, result=%d)",
            candidates.size(), filtered, r);
   return r;
}

void CNiceChannel::setControllingMode(bool controlling)
{
   m_bControlling = controlling;
   if (m_pAgent)
      g_object_set(G_OBJECT(m_pAgent), "controlling-mode", m_bControlling ? TRUE : FALSE, NULL);
   DebugLog("Set controlling mode to %s", m_bControlling ? "true" : "false");
}

void CNiceChannel::setStunServer(const std::string& server, guint port)
{
   if (server.empty())
   {
      clearStunServer();
      return;
   }

   m_bHasStunServer = true;
   m_StunServer = server;
   m_StunPort = port ? port : 3478;

   if (m_pAgent)
   {
      g_object_set(G_OBJECT(m_pAgent),
                   "stun-server", m_StunServer.c_str(),
                   "stun-server-port", m_StunPort,
                   NULL);

   }
}

void CNiceChannel::clearStunServer()
{
   m_bHasStunServer = false;
   m_StunServer.clear();
   m_StunPort = 0;

   if (m_pAgent)
   {
      g_object_set(G_OBJECT(m_pAgent),
                   "stun-server", NULL,
                   "stun-server-port", 0,
                   NULL);
   }
}

void CNiceChannel::setTurnRelay(const std::string& server, guint port,
                                const std::string& username, const std::string& password,
                                NiceRelayType type)
{
   if (server.empty())
   {
      clearTurnRelay();
      return;
   }

   m_bHasTurnRelay = true;
   m_TurnServer = server;
   m_TurnPort = port ? port : 3478;
   m_TurnUsername = username;
   m_TurnPassword = password;
   m_TurnType = type;

   if (m_pAgent && m_iStreamID != 0)
   {
      nice_agent_set_relay_info(m_pAgent, m_iStreamID, m_iComponentID,
                                m_TurnServer.c_str(), m_TurnPort,
                                m_TurnUsername.c_str(), m_TurnPassword.c_str(),
                                m_TurnType);
   }
}

void CNiceChannel::clearTurnRelay()
{
   m_bHasTurnRelay = false;
   m_TurnServer.clear();
   m_TurnPort = 0;
   m_TurnUsername.clear();
   m_TurnPassword.clear();
   m_TurnType = NICE_RELAY_TYPE_TURN_UDP;
}

bool CNiceChannel::restartCandidateGathering()
{
   if (!m_pAgent || (m_iStreamID == 0))
   {
      DebugLog("Skipping candidate gathering restart (agent=%p stream=%u)",
               static_cast<void*>(m_pAgent), m_iStreamID);
      return true;
   }

   DebugLog("Restarting candidate gathering for stream %u", m_iStreamID);

   g_mutex_lock(&m_StateLock);
   m_bGatheringDone = false;
   g_mutex_unlock(&m_StateLock);

   if (!nice_agent_gather_candidates(m_pAgent, m_iStreamID))
   {
      DebugLog("Failed to restart candidate gathering for stream %u", m_iStreamID);
      return false;
   }

   return true;
}

void CNiceChannel::setPortRange(guint min_port, guint max_port)
{
   if (min_port > 0 && max_port > 0 && min_port <= max_port)
   {
      m_bHasPortRange = true;
      m_PortRangeMin = min_port;
      m_PortRangeMax = max_port;
   }
   else
   {
      m_bHasPortRange = false;
      m_PortRangeMin = 0;
      m_PortRangeMax = 0;
   }
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
   DebugLog("Candidate gathering complete for stream %u", stream_id);
}

void CNiceChannel::cb_state_changed(NiceAgent* agent, guint stream_id,
                                    guint component_id, guint state,
                                    gpointer data)
{
   CNiceChannel* self = (CNiceChannel*)data;
   g_mutex_lock(&self->m_StateLock);
   const bool was_connected = self->m_bConnected;
   const bool now_connected = (state == NICE_COMPONENT_STATE_READY ||
                               state == NICE_COMPONENT_STATE_CONNECTED);

   bool entered_failed_state = false;
   bool left_ready_state = false;
   bool broadcast = false;

   if (now_connected)
   {
      if (!self->m_bConnected)
      {
         self->m_bConnected = true;
         broadcast = true;
      }
   }
   else
   {
      if (self->m_bConnected)
      {
         self->m_bConnected = false;
         left_ready_state = true;
      }

      if (state == NICE_COMPONENT_STATE_FAILED)
      {
         broadcast = true;
         self->m_bFailed = true;
         entered_failed_state = true;
      }
      else if (was_connected)
      {
         broadcast = true;
         self->m_bFailed = true;
      }
   }

   if (broadcast)
      g_cond_broadcast(&self->m_StateCond);

   g_mutex_unlock(&self->m_StateLock);

   if (entered_failed_state)
   {
      g_warning("Component %u state changed to %s; channel marked unusable",
                component_id, NiceComponentStateToString(state));
   }
   else if (left_ready_state)
   {
      g_warning("Component %u state changed to %s after being connected; "
                "channel marked unusable",
                component_id, NiceComponentStateToString(state));
   }

   DebugLog("Component %u state changed to %s%s", component_id,
            NiceComponentStateToString(state),
            (entered_failed_state || left_ready_state) ?
               "; channel marked unusable" : "");
}

gpointer CNiceChannel::cb_loop(gpointer data)
{
   CNiceChannel* self = (CNiceChannel*)data;
   g_main_loop_run(self->m_pLoop);
   return NULL;
}

#endif
