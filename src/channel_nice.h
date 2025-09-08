#ifndef __UDT_CHANNEL_NICE_H__
#define __UDT_CHANNEL_NICE_H__

#include "channel.h"
#include <string>

// A simple channel that stores NICE configuration options.
// Actual integration with libnice can be added later.
class CChannelNice : public CChannel
{
public:
    CChannelNice(int version,
                 const std::string& stun,
                 const std::string& turn,
                 const std::string& username,
                 const std::string& password);

    void open(const sockaddr* addr = NULL);

private:
    std::string m_sStunServer;
    std::string m_sTurnServer;
    std::string m_sTurnUsername;
    std::string m_sTurnPassword;
};

#endif
