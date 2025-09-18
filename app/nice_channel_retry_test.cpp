#ifdef USE_LIBNICE

#define private public
#include "nice_channel.h"
#undef private
#include "packet.h"

#include <glib.h>
#include <cerrno>
#include <cstring>
#include <iostream>

namespace
{
int g_send_attempts = 0;

gint FakeNiceAgentSend(NiceAgent*, guint, guint, gsize len, const gchar*)
{
   ++ g_send_attempts;
   if (g_send_attempts == 1)
   {
      errno = EAGAIN;
      return -1;
   }
   return static_cast<gint>(len);
}
}

int main()
{
   g_send_attempts = 0;
   CNiceChannel::SetAgentSendFuncForTesting(&FakeNiceAgentSend);

   CNiceChannel channel;
   GMainContext* context = g_main_context_new();
   g_main_context_push_thread_default(context);

   channel.m_pContext = context;
   channel.m_pAgent = reinterpret_cast<NiceAgent*>(0x1);
   channel.m_iStreamID = 1;
   channel.m_iComponentID = 1;

   CPacket packet;
   packet.setLength(8);
   for (int i = 0; i < packet.getLength(); ++ i)
      packet.m_pcData[i] = static_cast<char>(i);

   const int result = channel.sendto(NULL, packet);

   g_main_context_pop_thread_default(context);

   const int expected_size = CPacket::m_iPktHdrSize + packet.getLength();
   if (result != expected_size)
   {
      std::cerr << "Unexpected send result: " << result << " expected " << expected_size << std::endl;
      CNiceChannel::SetAgentSendFuncForTesting(NULL);
      g_main_context_unref(context);
      channel.m_pContext = NULL;
      channel.m_pAgent = NULL;
      return 1;
   }

   if (g_send_attempts != 2)
   {
      std::cerr << "Retry count mismatch: " << g_send_attempts << std::endl;
      CNiceChannel::SetAgentSendFuncForTesting(NULL);
      g_main_context_unref(context);
      channel.m_pContext = NULL;
      channel.m_pAgent = NULL;
      return 1;
   }

   if (channel.m_ActiveSends != 0)
   {
      std::cerr << "Active send count not cleared." << std::endl;
      CNiceChannel::SetAgentSendFuncForTesting(NULL);
      g_main_context_unref(context);
      channel.m_pContext = NULL;
      channel.m_pAgent = NULL;
      return 1;
   }

   if (channel.m_bFailed)
   {
      std::cerr << "Channel erroneously marked as failed." << std::endl;
      CNiceChannel::SetAgentSendFuncForTesting(NULL);
      g_main_context_unref(context);
      channel.m_pContext = NULL;
      channel.m_pAgent = NULL;
      return 1;
   }

   CNiceChannel::SetAgentSendFuncForTesting(NULL);

   g_main_context_unref(context);
   channel.m_pContext = NULL;
   channel.m_pAgent = NULL;

   return 0;
}

#else
int main()
{
   return 0;
}
#endif
