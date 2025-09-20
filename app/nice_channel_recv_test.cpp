#ifdef USE_LIBNICE

#define private public
#include "nice_channel.h"
#undef private
#include "packet.h"

#include <glib.h>
#include <cerrno>
#include <iostream>
#ifdef WIN32
#include <winsock2.h>
#endif

int main()
{
   CNiceChannel channel;
   channel.m_pRecvQueue = g_async_queue_new();
   if (!channel.m_pRecvQueue)
   {
      std::cerr << "Failed to allocate receive queue" << std::endl;
      return 1;
   }

   CPacket packet;

   errno = 0;
   int result = channel.recvfrom(NULL, packet);
   if (result != -1 || packet.getLength() != -1)
   {
      std::cerr << "Unexpected recv result for timeout: " << result << std::endl;
      g_async_queue_unref(channel.m_pRecvQueue);
      channel.m_pRecvQueue = NULL;
      return 1;
   }

#ifndef WIN32
   if (errno != EAGAIN)
   {
      std::cerr << "Expected EAGAIN for timeout but found " << errno << std::endl;
      g_async_queue_unref(channel.m_pRecvQueue);
      channel.m_pRecvQueue = NULL;
      return 1;
   }
#else
   if (WSAGetLastError() != WSAEWOULDBLOCK)
   {
      std::cerr << "Expected WSAEWOULDBLOCK for timeout" << std::endl;
      g_async_queue_unref(channel.m_pRecvQueue);
      channel.m_pRecvQueue = NULL;
      return 1;
   }
#endif

   g_mutex_lock(&channel.m_CloseLock);
   channel.m_bClosing = true;
   g_mutex_unlock(&channel.m_CloseLock);

   g_async_queue_push(channel.m_pRecvQueue, NULL);

   errno = 0;
   result = channel.recvfrom(NULL, packet);
   if (result != -1)
   {
      std::cerr << "Unexpected recv result for shutdown: " << result << std::endl;
      g_async_queue_unref(channel.m_pRecvQueue);
      channel.m_pRecvQueue = NULL;
      return 1;
   }

#ifndef WIN32
   if (errno != EBADF)
   {
      std::cerr << "Expected EBADF for shutdown but found " << errno << std::endl;
      g_async_queue_unref(channel.m_pRecvQueue);
      channel.m_pRecvQueue = NULL;
      return 1;
   }
#else
   if (WSAGetLastError() != WSAECONNRESET)
   {
      std::cerr << "Expected WSAECONNRESET for shutdown" << std::endl;
      g_async_queue_unref(channel.m_pRecvQueue);
      channel.m_pRecvQueue = NULL;
      return 1;
   }
#endif

   g_async_queue_unref(channel.m_pRecvQueue);
   channel.m_pRecvQueue = NULL;

   return 0;
}

#else
int main()
{
   return 0;
}
#endif
