#include "channel_nice.h"

#include <arpa/inet.h>
#include <cstring>
#include <vector>

CChannelNice::CChannelNice():
   m_pAgent(NULL),
   m_pContext(NULL),
   m_iStreamID(0),
   m_bConnected(false)
{
   // Create a dedicated GLib context and libnice agent
   m_pContext = g_main_context_new();
   m_pAgent = nice_agent_new(m_pContext, NICE_COMPATIBILITY_RFC5245);
   if (!m_pAgent)
      throw CUDTException(1, 0, 0);

   // Configure STUN and TURN servers (placeholder addresses)
   nice_agent_set_stun_server(m_pAgent, "stun.l.google.com", 19302);
   nice_agent_set_turn_server(m_pAgent, "turn.example.com", 3478,
                              "user", "pass");

   // Add a default local address candidate
   NiceAddress addr;
   nice_address_init(&addr);
   nice_address_set_from_string(&addr, "0.0.0.0");
   nice_agent_add_local_address(m_pAgent, &addr);

   m_iStreamID = nice_agent_add_stream(m_pAgent, 1);
   nice_agent_set_stream_name(m_pAgent, m_iStreamID, "udt");

   g_signal_connect(m_pAgent, "new-selected-pair",
                    G_CALLBACK(CChannelNice::cb_candidate_pair), this);
   g_signal_connect(m_pAgent, "component-state-changed",
                    G_CALLBACK(CChannelNice::cb_component_state), this);
}

CChannelNice::~CChannelNice()
{
   close();
}

void CChannelNice::open()
{
   nice_agent_gather_candidates(m_pAgent, m_iStreamID);
   // Wait for negotiation to complete and connection established
   while (!m_bConnected)
      g_main_context_iteration(m_pContext, TRUE);
}

void CChannelNice::close()
{
   if (m_pAgent)
   {
      if (m_iStreamID > 0)
         nice_agent_remove_stream(m_pAgent, m_iStreamID);
      g_object_unref(m_pAgent);
      m_pAgent = NULL;
   }
   if (m_pContext)
   {
      g_main_context_unref(m_pContext);
      m_pContext = NULL;
   }
}

void CChannelNice::cb_candidate_pair(NiceAgent* agent, guint stream_id,
                                     guint component_id, guint state,
                                     gpointer user_data)
{
   CChannelNice* self = reinterpret_cast<CChannelNice*>(user_data);
   NiceCandidate* local = NULL;
   NiceCandidate* remote = NULL;
   if (nice_agent_get_selected_pair(agent, stream_id, component_id,
                                    &local, &remote))
   {
      if (remote)
      {
         memset(&self->m_PeerAddr, 0, sizeof(sockaddr_storage));
         if (remote->addr.s.addr.sa_family == AF_INET)
            memcpy(&self->m_PeerAddr, &remote->addr.s,
                   sizeof(sockaddr_in));
         else
            memcpy(&self->m_PeerAddr, &remote->addr.s6,
                   sizeof(sockaddr_in6));
      }
      if (local)
         nice_candidate_free(local);
      if (remote)
         nice_candidate_free(remote);
   }
}

void CChannelNice::cb_component_state(NiceAgent* agent, guint stream_id,
                                      guint component_id, guint state,
                                      gpointer user_data)
{
   if (state == NICE_COMPONENT_STATE_CONNECTED)
   {
      CChannelNice* self = reinterpret_cast<CChannelNice*>(user_data);
      self->m_bConnected = true;
   }
}

int CChannelNice::sendto(const sockaddr* addr, CPacket& packet) const
{
   int payload = packet.getLength();
   std::vector<char> buf(CPacket::m_iPktHdrSize + payload);

   // Header conversion to network order
   uint32_t* hdr = reinterpret_cast<uint32_t*>(buf.data());
   for (int i = 0; i < 4; ++i)
      hdr[i] = htonl(packet.m_nHeader[i]);

   // Payload conversion if control packet
   if (packet.getFlag())
   {
      for (int i = 0, n = payload / 4; i < n; ++i)
         reinterpret_cast<uint32_t*>(buf.data() + CPacket::m_iPktHdrSize)[i] =
            htonl(reinterpret_cast<uint32_t*>(packet.m_pcData)[i]);
   }
   else
      memcpy(buf.data() + CPacket::m_iPktHdrSize, packet.m_pcData, payload);

   gssize sent = nice_agent_send(m_pAgent, m_iStreamID, 1,
                                 buf.size(), buf.data());
   return (sent >= 0) ? payload : -1;
}

int CChannelNice::recvfrom(sockaddr* addr, CPacket& packet) const
{
   int maxlen = CPacket::m_iPktHdrSize + packet.getLength();
   std::vector<char> buf(maxlen);
   GError* err = NULL;
   gssize r = nice_agent_recv(m_pAgent, m_iStreamID, 1,
                              buf.size(), buf.data(), &err);
   if (r <= 0)
   {
      if (err)
         g_error_free(err);
      packet.setLength(-1);
      return -1;
   }

   if (addr)
      memcpy(addr, &m_PeerAddr, sizeof(sockaddr_storage));

   memcpy(packet.m_nHeader, buf.data(), CPacket::m_iPktHdrSize);
   uint32_t* hdr = packet.m_nHeader;
   for (int i = 0; i < 4; ++i)
      hdr[i] = ntohl(hdr[i]);

   packet.setLength(r - CPacket::m_iPktHdrSize);
   memcpy(packet.m_pcData, buf.data() + CPacket::m_iPktHdrSize,
          packet.getLength());

   if (packet.getFlag())
   {
      for (int i = 0, n = packet.getLength() / 4; i < n; ++i)
         reinterpret_cast<uint32_t*>(packet.m_pcData)[i] =
            ntohl(reinterpret_cast<uint32_t*>(packet.m_pcData)[i]);
   }

   return packet.getLength();
}

