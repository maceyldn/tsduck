#include "tsProcessorPlugin.h"
#include "tsPluginRepository.h"
#include "tsAsyncReport.h"
#include "tsTSPacket.h"
#include <thread>
#include <queue>
#include <mutex>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h> // Add this header
#include <cstring> // Add this header for memset and memcpy
#include <unistd.h> // Add this header for close function

#define MAX_UDP_PAYLOAD_SIZE 188 // the size of a TS packet

namespace ts {

    class UDPInjectPlugin : public ts::ProcessorPlugin {
        TS_NOBUILD_NOCOPY(UDPInjectPlugin);
    public:
        UDPInjectPlugin(TSP*);

        virtual bool getOptions() override;
        virtual bool start() override;
        virtual bool stop() override;
        virtual Status processPacket(ts::TSPacket& pkt, ts::TSPacketMetadata& pkt_data) override;

    private:
        int _socket;
        std::thread _receiverThread;
        std::queue<ts::TSPacket> _packetQueue;
        std::mutex _queueMutex;
        int _udpPort;
        ts::UString _interfaceIp;

        void receiverThreadMain();
    };

    TS_REGISTER_PROCESSOR_PLUGIN(u"udpinject", ts::UDPInjectPlugin);

    UDPInjectPlugin::UDPInjectPlugin(TSP* _tsp) :
    ts::ProcessorPlugin(_tsp, u"udpinject", u"Injects UDP packets into the TS stream"),
    _socket(-1),
    _receiverThread(),
    _packetQueue(),
    _queueMutex(),
    _udpPort(9999), // Default UDP port
    _interfaceIp(u"127.0.0.1") // Default interface IP address
    {
        // Add the UDP port option
        option(u"udp-port", 'p', ts::Args::UINT16, 0, ts::Args::UNLIMITED_VALUE, 1, 65535);
        help(u"udp-port",
            u"Specifies the UDP port for the plugin. "
            u"The valid range is from 1 to 65535. "
            u"Default is 9999.");
    }

    bool UDPInjectPlugin::getOptions()
    {
        // Get option values
         getIntValue(_udpPort, u"udp-port");
        return true;
    }

    bool UDPInjectPlugin::start() {

         tsp->info(u"UDPInject startup. port: %d", {_udpPort});

        // Open the UDP socket
        _socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (_socket < 0) {
            tsp->error(u"Failed to open UDP socket");
            return false;
        }

        // Bind the socket to the specified port and IP address
        struct sockaddr_in server_addr;
        std::memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = inet_addr(_interfaceIp.toUTF8().c_str());
        server_addr.sin_port = htons(static_cast<uint16_t>(_udpPort));

        if (bind(_socket, reinterpret_cast<const sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
            tsp->error(u"Failed to bind UDP socket to IP %s and port %d", {_interfaceIp, _udpPort});
            close(_socket);
            _socket = -1;
            return false;
        }

        // Start the receiver thread
        _receiverThread = std::thread(&UDPInjectPlugin::receiverThreadMain, this);

        return true;
    }

    bool UDPInjectPlugin::stop() {
        // Stop the receiver thread
        if (_receiverThread.joinable()) {
            _receiverThread.join();
        }

        // Close the UDP socket
        if (_socket >= 0) {
            close(_socket);
            _socket = -1;
        }

        return true;
    }

    void UDPInjectPlugin::receiverThreadMain() {
        uint8_t buffer[MAX_UDP_PAYLOAD_SIZE];
        while (true) {
            // Read data from the UDP socket into the buffer
            ssize_t recv_len = recv(_socket, buffer, MAX_UDP_PAYLOAD_SIZE, 0);
            if (recv_len < 0) {
                tsp->error(u"Error receiving UDP data");
                continue;
            } else if (recv_len != MAX_UDP_PAYLOAD_SIZE) {
                tsp->warning(u"Received incomplete UDP packet");
                continue;
            }

            ts::TSPacket pkt;
            std::memcpy(pkt.b, buffer, ts::PKT_SIZE);

            // Add the TSPacket to _packetQueue (with proper locking)
            std::lock_guard<std::mutex> lock(_queueMutex);
            _packetQueue.push(pkt);
        }
    }

    ts::ProcessorPlugin::Status UDPInjectPlugin::processPacket(ts::TSPacket& pkt, ts::TSPacketMetadata& pkt_data) {
        if (pkt.getPID() == ts::PID_NULL) {
            std::lock_guard<std::mutex> lock(_queueMutex);
            if (!_packetQueue.empty()) {
                pkt = _packetQueue.front();
                _packetQueue.pop();
                return ts::ProcessorPlugin::TSP_OK;
            }

            return ts::ProcessorPlugin::TSP_NULL;
        }
        return ts::ProcessorPlugin::TSP_OK;
    }
}
