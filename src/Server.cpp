//
//  rtmp_relay
//

#include <queue>
#include <iostream>
#include <algorithm>
#include "Server.h"
#include "Application.h"
#include "Log.h"

using namespace cppsocket;

namespace relay
{
    Server::Server(Network& aNetwork,
                   const std::string& address,
                   float aPingInterval,
                   const std::vector<ApplicationDescriptor>& aApplicationDescriptors):
        network(aNetwork), socket(aNetwork), pingInterval(aPingInterval), applicationDescriptors(aApplicationDescriptors)
    {
        socket.setAcceptCallback(std::bind(&Server::handleAccept, this, std::placeholders::_1));

        socket.startAccept(address);
    }

    Server::~Server()
    {
        
    }

    void Server::update(float delta)
    {
        if (application) application->update(delta);
        
        for (auto receiverIterator = receivers.begin(); receiverIterator != receivers.end();)
        {
            const auto& receiver = *receiverIterator;

            if (receiver->isConnected())
            {
                receiver->update(delta);
                ++receiverIterator;
            }
            else
            {
                application.reset();
                receiverIterator = receivers.erase(receiverIterator);
            }
        }
    }

    void Server::handleAccept(Socket& clientSocket)
    {
        // accept only one input
        if (receivers.empty())
        {
            std::unique_ptr<Receiver> receiver(new Receiver(clientSocket, *this, pingInterval));
            receivers.push_back(std::move(receiver));

            application.reset();
        }
        else
        {
            clientSocket.close();
        }
    }

    bool Server::connect(const std::string& applicationName)
    {
        for (const ApplicationDescriptor& applicationDescriptor : applicationDescriptors)
        {
            if (applicationDescriptor.name.empty() ||
                applicationDescriptor.name == applicationName)
            {
                application.reset(new Application(network, applicationDescriptor, applicationName));

                return true;
            }
        }

        // failed to connect
        return false;
    }

    void Server::createStream(const std::string& streamName)
    {
        if (application) application->createStream(streamName);
    }

    void Server::deleteStream()
    {
        if (application) application->deleteStream();
    }

    void Server::unpublishStream()
    {
        if (application) application->unpublishStream();
    }

    void Server::sendAudio(uint64_t timestamp, const std::vector<uint8_t>& audioData)
    {
        if (application) application->sendAudio(timestamp, audioData);
    }

    void Server::sendVideo(uint64_t timestamp, const std::vector<uint8_t>& videoData)
    {
        if (application) application->sendVideo(timestamp, videoData);
    }

    void Server::sendMetaData(const amf0::Node& metaData)
    {
        if (application) application->sendMetaData(metaData);
    }

    void Server::sendTextData(const amf0::Node& textData)
    {
        if (application) application->sendTextData(textData);
    }

    void Server::printInfo() const
    {
        Log(Log::Level::INFO) << "Server listening on " << socket.getPort();

        Log(Log::Level::INFO) << "Receivers:";
        for (const auto& receiver : receivers)
        {
            receiver->printInfo();
        }

        if (application)
        {
            application->printInfo();
        }
    }

    void Server::getInfo(std::string& str) const
    {
        str += "<h1>Receivers</h1><table><tr><th>Name</th><th>Connected</th><th>Address</th><th>State</th></tr>";
        for (const auto& receiver : receivers)
        {
            receiver->getInfo(str);
        }

        str += "</table>";

        if (application)
        {
            application->getInfo(str);
        }
    }
}
