#include "channel_nice.h"

CChannelNice::CChannelNice(int version,
                           const std::string& stun,
                           const std::string& turn,
                           const std::string& username,
                           const std::string& password)
    : CChannel(version),
      m_sStunServer(stun),
      m_sTurnServer(turn),
      m_sTurnUsername(username),
      m_sTurnPassword(password)
{
}

void CChannelNice::open(const sockaddr* addr)
{
    // Apply NICE related options before opening the channel.
    // Actual libnice calls can be added here in the future.
    CChannel::open(addr);
}
