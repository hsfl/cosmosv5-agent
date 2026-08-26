/*!
 * \file FileSender.h
 * \brief Send files using the COSMOS transfer protocol.
 * \ingroup cosmos_agent_messaging
 * \defgroup cosmos_filesender File Sender
 *
 * Provides the FileSender class that encapsulates the sender side of the
 * COSMOS file-transfer protocol. Given a destination node/agent and a list of
 * files, it segments, queues, and transmits chunks, handling flow control and
 * retransmission internally.
 */

#ifndef FILE_SENDER_H_
#define FILE_SENDER_H_

#include "support/transferclass.h"
#include <fstream>

namespace Cosmos {
class FileSender : public Cosmos::Support::Sender
{
public:
    /**
     * \brief Implementation of the Sender interface that writes packets to a file.
     */
    FileSender() = default;
    void init(const std::string& filename);
    ~FileSender();
    Cosmos::Support::SendRetVal send(PacketComm& packet) override;
private:
    ofstream outfile;
};
}

#endif