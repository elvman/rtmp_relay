//
//  rtmp_relay
//

#include <iostream>
#include <cstring>
#include "Input.h"
#include "Constants.h"
#include "Amf0.h"
#include "Utils.h"

Input::Input(Network& network, Socket socket):
    _network(network), _socket(std::move(socket)), _generator(_rd())
{
    _socket.setReadCallback(std::bind(&Input::handleRead, this, std::placeholders::_1));
    _socket.setCloseCallback(std::bind(&Input::handleClose, this));
    _socket.startRead();
}

Input::~Input()
{
    
}

Input::Input(Input&& other):
    _network(other._network),
    _socket(std::move(other._socket)),
    _data(std::move(other._data)),
    _state(other._state),
    _chunkSize(other._chunkSize),
    _generator(std::move(other._generator)),
    _messageStreamId(other._messageStreamId),
    _timestamp(other._timestamp)
{
    other._state = rtmp::State::UNINITIALIZED;
    other._chunkSize = 128;
    other._messageStreamId = 0;
    other._timestamp = 0;
    
    _socket.setReadCallback(std::bind(&Input::handleRead, this, std::placeholders::_1));
    _socket.setCloseCallback(std::bind(&Input::handleClose, this));
}

Input& Input::operator=(Input&& other)
{
    _socket = std::move(other._socket);
    _data = std::move(other._data);
    _state = other._state;
    _chunkSize = other._chunkSize;
    _generator = std::move(other._generator);
    _messageStreamId = other._messageStreamId;
    _timestamp = other._timestamp;
    
    other._state = rtmp::State::UNINITIALIZED;
    other._chunkSize = 128;
    other._messageStreamId = 0;
    other._timestamp = 0;
    
    _socket.setReadCallback(std::bind(&Input::handleRead, this, std::placeholders::_1));
    _socket.setCloseCallback(std::bind(&Input::handleClose, this));
    
    return *this;
}

void Input::update()
{
    
}

bool Input::getPacket(std::vector<uint8_t>& packet)
{
    if (_data.size())
    {
        packet = _data;
        return true;
    }
    
    return false;
}

void Input::handleRead(const std::vector<uint8_t>& data)
{
    _data.insert(_data.end(), data.begin(), data.end());
    
    std::cout << "Input got " << std::to_string(data.size()) << " bytes" << std::endl;
    
    uint32_t offset = 0;
    
    while (offset < _data.size())
    {
        if (_state == rtmp::State::UNINITIALIZED)
        {
            if (_data.size() - offset >= sizeof(uint8_t))
            {
                // C0
                uint8_t version = static_cast<uint8_t>(*(_data.data() + offset));
                offset += sizeof(version);
                std::cout << "Got version " << version << std::endl;
                
                if (version != 0x03)
                {
                    std::cerr << "Unsuported version" << std::endl;
                    _socket.close();
                    break;
                }
                
                // S0
                std::vector<uint8_t> reply;
                reply.push_back(RTMP_VERSION);
                _socket.send(reply);
                
                _state = rtmp::State::VERSION_SENT;
            }
            else
            {
                break;
            }
        }
        else if (_state == rtmp::State::VERSION_SENT)
        {
            if (_data.size() - offset >= sizeof(rtmp::Challange))
            {
                // C1
                rtmp::Challange* challange = reinterpret_cast<rtmp::Challange*>(_data.data() + offset);
                offset += sizeof(*challange);
                
                std::cout << "Got Challange message, time: " << challange->time <<
                    ", version: " << static_cast<uint32_t>(challange->version[0]) << "." <<
                    static_cast<uint32_t>(challange->version[1]) << "." <<
                    static_cast<uint32_t>(challange->version[2]) << "." <<
                    static_cast<uint32_t>(challange->version[3]) << std::endl;
                
                // S1
                rtmp::Challange replyChallange;
                replyChallange.time = 0;
                memcpy(replyChallange.version, RTMP_SERVER_VERSION, sizeof(RTMP_SERVER_VERSION));
                
                for (size_t i = 0; i < sizeof(replyChallange.randomBytes); ++i)
                {
                    replyChallange.randomBytes[i] = std::uniform_int_distribution<uint8_t>{0, 255}(_generator);
                }
                
                std::vector<uint8_t> reply;
                reply.insert(reply.begin(),
                             reinterpret_cast<uint8_t*>(&replyChallange),
                             reinterpret_cast<uint8_t*>(&replyChallange) + sizeof(replyChallange));
                _socket.send(reply);
                
                // S2
                rtmp::Ack ack;
                ack.time = challange->time;
                ack.time2 = static_cast<uint32_t>(time(nullptr));
                memcpy(ack.randomBytes, challange->randomBytes, sizeof(ack.randomBytes));
                
                std::vector<uint8_t> ackData;
                ackData.insert(ackData.begin(),
                               reinterpret_cast<uint8_t*>(&ack),
                               reinterpret_cast<uint8_t*>(&ack) + sizeof(ack));
                _socket.send(ackData);
                
                _state = rtmp::State::ACK_SENT;
            }
            else
            {
                break;
            }
        }
        else  if (_state == rtmp::State::ACK_SENT)
        {
            if (_data.size() - offset >= sizeof(rtmp::Ack))
            {
                // C2
                rtmp::Ack* ack = reinterpret_cast<rtmp::Ack*>(_data.data() + offset);
                offset += sizeof(*ack);
                
                std::cout << "Got Ack message, time: " << ack->time << ", time2: " << ack->time2 << std::endl;
                
                std::cout << "Handshake done" << std::endl;
                
                _state = rtmp::State::HANDSHAKE_DONE;
            }
            else
            {
                break;
            }
        }
        else if (_state == rtmp::State::HANDSHAKE_DONE)
        {
            // TODO: send subscribe
            rtmp::Packet packet;
            
            uint32_t ret = rtmp::decodePacket(data, offset, _chunkSize, packet);
            
            if (ret > 0)
            {
                offset += ret;

                handlePacket(packet);
            }
            else
            {
                break;
            }
        }
    }
    
    _data.erase(_data.begin(), _data.begin() + offset);
}

void Input::handleClose()
{
    std::cout << "Input disconnected" << std::endl;
}

bool Input::handlePacket(const rtmp::Packet& packet)
{
    std::cout << "Message Stream ID: " << packet.header.messageStreamId << std::endl;
    //std::cout << "Message Stream ID: " << packet.header. << std::endl;

    _messageStreamId = packet.header.messageStreamId;
    _timestamp = packet.header.timestamp;

    switch (packet.header.messageType)
    {
        case rtmp::MessageType::SET_CHUNK_SIZE:
        {
            uint32_t ret = decodeInt(packet.data, 0, 4, _chunkSize);

            if (ret == 0)
            {
                return false;
            }

            break;
        }

        case rtmp::MessageType::PING:
        {
            uint32_t offset = 0;

            uint16_t pingType;
            uint32_t ret = decodeInt(packet.data, offset, 2, pingType);

            if (ret == 0)
            {
                return false;
            }

            offset += 2;

            uint16_t param1;
            ret = decodeInt(packet.data, offset, 2, param1);

            if (ret == 0)
            {
                return false;
            }

            offset += 2;

            uint16_t param2;
            ret = decodeInt(packet.data, offset, 2, param2);

            if (ret == 0)
            {
                return false;
            }

            offset += 2;

            break;
        }

        case rtmp::MessageType::SERVER_BANDWIDTH:
        {
            break;
        }

        case rtmp::MessageType::CLIENT_BANDWIDTH:
        {
            break;
        }

        case rtmp::MessageType::AUDIO_PACKET:
        {
            // TODO: forward audio packet
            break;
        }

        case rtmp::MessageType::VIDEO_PACKET:
        {
            // TODO: forward video packet
            break;
        }

        case rtmp::MessageType::AMF3_COMMAND:
        {
            std::cerr << "AMF3 commands are not supported" << std::endl;
            break;
        }

        case rtmp::MessageType::INVOKE:
        case rtmp::MessageType::AMF0_COMMAND:
        {
            uint32_t offset = 0;

            amf0::Node command;

            uint32_t ret = command.decode(packet.data, offset);

            if (ret == 0)
            {
                return false;
            }

            offset += ret;

            std::cout << "Command: " << command.asString() << std::endl;
            
            amf0::Node streamId;

            ret = streamId.decode(packet.data, offset);

            if (ret == 0)
            {
                return false;
            }

            offset += ret;

            std::cout << "Stream ID: " << streamId.asDouble() << std::endl;

            amf0::Node argument;

            ret = argument.decode(packet.data, offset);

            if (ret == 0)
            {
                return false;
            }

            offset += ret;

            std::cout << "Argument: ";
            argument.dump();

            if (command.asString() == "connect")
            {
                std::vector<uint8_t> resultData;

                amf0::Node resultName = std::string("_result");
                resultName.encode(resultData);

                amf0::Node resultStreamId = static_cast<double>(0);
                resultStreamId.encode(resultData);

                amf0::Node replyFmsVer;
                replyFmsVer["fmsVer"] = std::string("FMS/3,0,1,123");
                replyFmsVer["capabilities"] = static_cast<double>(31);
                replyFmsVer.encode(resultData);

                amf0::Node replyStatus;
                replyStatus["level"] = std::string("status");
                replyStatus["code"] = std::string("NetConnection.Connect.Success");
                replyStatus["description"] = std::string("Connection succeeded.");
                replyStatus["objectEncoding"] = static_cast<double>(0);
                replyStatus.encode(resultData);

                rtmp::Header resultHeader;
                resultHeader.type = rtmp::Header::Type::TWELVE_BYTE;
                resultHeader.chunkStreamId = 3;
                resultHeader.messageStreamId = rtmp::MESSAGE_STREAM_ID; //packet.header.messageStreamId;
                resultHeader.timestamp = packet.header.timestamp;
                resultHeader.messageType = rtmp::MessageType::AMF0_COMMAND;
                resultHeader.length = static_cast<uint32_t>(resultData.size());

                rtmp::Packet resultPacket;
                resultPacket.header = resultHeader;
                resultPacket.data = resultData;

                std::vector<uint8_t> result;
                encodePacket(result, _chunkSize, resultPacket);

                _socket.send(result);

                /*
                std::vector<uint8_t> onBWDoneData;

                amf0::Node onBWDoneName = std::string("onBWDone");
                onBWDoneName.encode(onBWDoneData);

                amf0::Node arg1 = static_cast<double>(0);
                arg1.encode(onBWDoneData);

                amf0::Node arg2(amf0::Marker::Null);
                arg2.encode(onBWDoneData);

                amf0::Node arg3 = static_cast<double>(8192);
                arg3.encode(onBWDoneData);

                rtmp::Header onBWDoneHeader;
                onBWDoneHeader.type = rtmp::Header::Type::TWELVE_BYTE;
                onBWDoneHeader.messageStreamId = packet.header.messageStreamId;
                onBWDoneHeader.timestamp = packet.header.timestamp + 1;
                onBWDoneHeader.messageType = rtmp::MessageType::AMF0_COMMAND;
                onBWDoneHeader.length = static_cast<uint32_t>(onBWDoneData.size());

                rtmp::Packet onBWDonePacket;
                onBWDonePacket.header = onBWDoneHeader;
                onBWDonePacket.data = onBWDoneData;

                std::vector<uint8_t> onBWDone;
                encodePacket(onBWDone, _chunkSize, onBWDonePacket);

                _socket.send(onBWDone);
                */
            }
            else if (command.asString() == "publish")
            {
                //startPlaying("casino/blackjack/wallclock_test_med");
                startPlaying("wallclock_test_med");
            }
            break;
        }

        default:
        {
            std::cerr << "Unhandled message" << std::endl;
            break;
        }
    }

    return true;
}

void Input::startPlaying(const std::string filename)
{
    std::vector<uint8_t> statusData;

    amf0::Node commandName = std::string("onStatus");
    commandName.encode(statusData);

    amf0::Node zeroNode = static_cast<double>(0);
    zeroNode.encode(statusData);

    amf0::Node nullNode(amf0::Marker::Null);
    nullNode.encode(statusData);

    amf0::Node replyFmsVer;
    replyFmsVer["fmsVer"] = std::string("FMS/3,0,1,123");
    replyFmsVer["capabilities"] = static_cast<double>(31);
    replyFmsVer.encode(statusData);

    amf0::Node replyStatus;
    replyStatus["level"] = std::string("status");
    replyStatus["code"] = std::string("NetStream.Play.Start");
    replyStatus["description"] = std::string("Start live");
    //replyStatus["details"] = filename;
    //replyStatus["clientid"] = std::string("Lavf");
    replyStatus.encode(statusData);

    rtmp::Header statusHeader;
    statusHeader.type = rtmp::Header::Type::TWELVE_BYTE;
    statusHeader.chunkStreamId = 3;
    statusHeader.messageStreamId = rtmp::MESSAGE_STREAM_ID;
    statusHeader.timestamp = _timestamp;
    statusHeader.messageType = rtmp::MessageType::AMF0_COMMAND;
    statusHeader.length = static_cast<uint32_t>(statusData.size());

    rtmp::Packet statusPacket;
    statusPacket.header = statusHeader;
    statusPacket.data = statusData;

    std::vector<uint8_t> result;
    encodePacket(result, _chunkSize, statusPacket);

    _socket.send(result);
}
