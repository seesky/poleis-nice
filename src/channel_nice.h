#ifndef __UDT_CHANNEL_NICE_H__
#define __UDT_CHANNEL_NICE_H__

#include "udt.h"
#include "packet.h"

extern "C" {
#include <nice/agent.h>
}
#include <glib.h>
#include <vector>

// A channel implementation based on libnice for NAT traversal.
// It mirrors the packet packing/unpacking behaviour of CChannel.
class CChannelNice
{
public:
   CChannelNice();
   ~CChannelNice();

   // Start ICE negotiation and wait for connection establishment.
   void open();
   void close();

   // Send and receive UDT packets via libnice.
   int sendto(const sockaddr* addr, CPacket& packet) const;
   int recvfrom(sockaddr* addr, CPacket& packet) const;

private:
   static void cb_candidate_pair(NiceAgent* agent, guint stream_id,
                                 guint component_id, guint state,
                                 gpointer user_data);
   static void cb_component_state(NiceAgent* agent, guint stream_id,
                                  guint component_id, guint state,
                                  gpointer user_data);

private:
   NiceAgent*     m_pAgent;
   GMainContext*  m_pContext;
   guint          m_iStreamID;
   bool           m_bConnected;
   sockaddr_storage m_PeerAddr; // remote address from selected pair
};

#endif
