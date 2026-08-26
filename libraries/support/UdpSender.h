/*!
 * \file UdpSender.h
 * \brief Send data over UDP using the COSMOS transfer protocol.
 * \ingroup cosmos_agent_messaging
 * \defgroup cosmos_udpsender UDP Sender
 *
 * Provides the UdpSender class that delivers PacketComm packets over a UDP
 * socket. Used as the network transport backend for FileSender and other
 * agent-to-agent communication paths that run over IP networks.
 */

#ifndef UDP_SENDER_H_
#define UDP_SENDER_H_

#include "support/socketlib.h"
#include "support/transferclass.h"

namespace Cosmos {
class UdpSender : public Cosmos::Support::Sender
{
public:
    /**
     * \brief Implementation of the Sender interface that sends packets out over UDP.
     */
    UdpSender() = default;
    void init(const std::string& ip, uint16_t port);
    ~UdpSender();
    Cosmos::Support::SendRetVal send(PacketComm& packet) override;
private:
    //! Socket for sending data
    socket_channel sock;
};
}

#endif