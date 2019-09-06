/*
 * BsmReceiver.cpp
 *
 *  Created on: May 10, 2016
 *      Author: ivp
 */

#include "MessageReceiverPlugin.h"
#include <Clock.h>
#include <FrequencyThrottle.h>
#include <mutex>
#include <stdexcept>
#include <thread>

#include <tmx/apimessages/TmxEventLog.hpp>
#include <tmx/j2735_messages/J2735MessageFactory.hpp>
#include <BsmConverter.h>
#include <LocationMessage.h>

#define ABBR_BSM 20 // should be 20 
#define ABBR_SRM 2000

using namespace std;
using namespace boost::asio;
using namespace tmx;
using namespace tmx::messages;
using namespace tmx::utils;

// BSMs may be 10 times a second, so only send errors at most every 2 minutes
#define ERROR_WAIT_MS 120000
#define STATUS_WAIT_MS 2000

namespace MessageReceiver {

mutex syncLock;
FrequencyThrottle<int> errThrottle;
FrequencyThrottle<int> statThrottle;

static std::atomic<uint64_t> totalBytes {0};
static std::map<std::string, std::atomic<uint32_t> > totalCount;

MessageReceiverPlugin::MessageReceiverPlugin(std::string name): TmxMessageManager(name)
{
	//Don't need to subscribe to messages
	//SubscribeToMessages();
	errThrottle.set_Frequency(std::chrono::milliseconds(ERROR_WAIT_MS));
	statThrottle.set_Frequency(std::chrono::milliseconds(STATUS_WAIT_MS));
}

MessageReceiverPlugin::~MessageReceiverPlugin() { }

template <typename T>
TmxJ2735EncodedMessage<T> *encode(TmxJ2735EncodedMessage<T> &encMsg, T *msg) {
	encMsg.clear();

	if (msg)
		encMsg.initialize(*msg);

	// Clean up the TMX message pointer and thus the J2735 structure pointer
	delete msg;
	return &encMsg;
}

BsmMessage *DecodeBsm(uint32_t vehicleId, uint32_t heading, uint32_t speed, uint32_t latitude,
			   uint32_t longitude, uint32_t elevation, DecodedBsmMessage &decodedBsm)
{
	FILE_LOG(logDEBUG1) << "BSM vehicleId: " << vehicleId
			<< ", heading: " << heading
			<< ", speed: " << speed
			<< ", latitude: " << latitude
			<< ", longitude: " << longitude
			<< ", elevation: " << elevation;

	//send BSM

	// Set the temp ID
	decodedBsm.set_TemporaryId(vehicleId);

	// Heading
	decodedBsm.set_Heading((float)(heading / 1000000.0));
	decodedBsm.set_IsHeadingValid(true);

	// Speed
	decodedBsm.set_Speed_mps((double)(speed / 1000.0));
	decodedBsm.set_IsSpeedValid(true);

	// Latitude and Longitude
	decodedBsm.set_Latitude((double)(latitude / 1000000.0 - 180));
	decodedBsm.set_Longitude((double)(longitude / 1000000.0 - 180));
	decodedBsm.set_IsLocationValid(true);

	// Altitude
	decodedBsm.set_Elevation_m((float)(elevation / 1000.0 - 500));
	decodedBsm.set_IsElevationValid(true);

	FILE_LOG(logDEBUG1) << "Decoded BSM: " << decodedBsm;

	BasicSafetyMessage *bsm = (BasicSafetyMessage *)calloc(1, sizeof(BasicSafetyMessage));
	if (bsm)
		BsmConverter::ToBasicSafetyMessage(decodedBsm, *bsm);

	// Note that this constructor assumes control of cleaning up the J2735 structure pointer
	return new BsmMessage(bsm);
}

SrmMessage *DecodeSrm(uint32_t vehicleId, uint32_t heading, uint32_t speed, uint32_t latitude,
		uint32_t longitude, uint32_t role)
{
	FILE_LOG(logDEBUG1) << "SRM vehicleId: " << vehicleId
			<< ", heading: " << heading
			<< ", speed: " << speed
			<< ", latitude: " << latitude
			<< ", longitude: " << longitude
			<< ", role: " << role;

	// send SRM
	size_t s = sizeof(vehicleId);
	SignalRequestMessage *srm = (SignalRequestMessage *)calloc(1, sizeof(SignalRequestMessage));
	if (srm) {
		srm->requestor.id.present = VehicleID_PR_entityID;
		srm->requestor.id.choice.entityID.size = s;
		srm->requestor.id.choice.entityID.buf = (uint8_t *)calloc(s, sizeof(uint8_t));
		if (srm->requestor.id.choice.entityID.buf)
			memcpy(srm->requestor.id.choice.entityID.buf, &vehicleId, s);

		srm->requestor.type =
				(struct RequestorType *)calloc(1, sizeof(struct RequestorType));
		if (srm->requestor.type)
		{
			srm->requestor.type->role = (BasicVehicleRole_t)role;
			srm->requestor.position =
				(struct RequestorPositionVector *)calloc(1, sizeof(struct RequestorPositionVector));
			if (srm->requestor.position)
			{
				srm->requestor.position->position.lat = (Latitude_t)(10.0 * latitude - 1800000000);
				srm->requestor.position->position.Long = (Longitude_t)(10.0 * longitude - 1800000000);
				srm->requestor.position->heading =
						(DSRC_Angle_t *)calloc(1, sizeof(DSRC_Angle_t));
				if (srm->requestor.position->heading)
					*(srm->requestor.position->heading) = (DSRC_Angle_t)(heading / 12500.0);
				srm->requestor.position->speed =
						(TransmissionAndSpeed *)calloc(1, sizeof(TransmissionAndSpeed));
				if (srm->requestor.position->speed)
				{
					srm->requestor.position->speed->transmisson = TransmissionState_unavailable;
					srm->requestor.position->speed->speed = (Velocity_t)(speed / 20.0);
				}
			}
		}
	}

	// Note that this constructor assumes control of cleaning up the J2735 structure pointer
	return new SrmMessage(srm);
}

void MessageReceiverPlugin::OnMessageReceived(routeable_message &msg)
{
	routeable_message *sendMsg = &msg;

	DecodedBsmMessage decodedBsm;
	BsmEncodedMessage encodedBsm;
	SrmEncodedMessage encodedSrm;

	PLOG(logDEBUG) << "Received " << msg;

	if (msg.get_type() == "Unknown" && msg.get_subtype() == "Unknown")
	{
		if (msg.get_encoding() == api::ENCODING_JSON_STRING)
		{
			// Check to see if the payload is a routable message
			message payloadMsg = msg.get_payload<message>();
			if (payloadMsg.get_untyped("header.type", "Unknown") != "Unknown")
			{
				msg.clear();
				msg.set_contents(payloadMsg.get_container());
				msg.reinit();
			}
		}
		else if (msg.get_encoding() == api::ENCODING_BYTEARRAY_STRING)
		{
			try
			{
				// Check for an abbreviated message
			    byte_stream bytesFull = msg.get_payload_bytes();
				byte_stream bytes; 
				if (bytesfull.size() > 8)
				{
					PLOG(logDEBUG1) << "Looking for abbreviated message in bytes " << bytesFull;
					uint16_t msgType;
					uint8_t msgVersion;
					uint16_t id;
					uint16_t dataLength;
					uint32_t vehID, heaDing, spEed, laTi, loNg, eleVate; // stores for data 

					std::vector<unsigned char>::iterator cnt = bytesFull.begin();

					while(cnt != bytesFull.end())
					{
						if(*cnt == 0x00 && (*(cnt+1) == 0x14 || *(cnt+1) == 0x12 || *(cnt+1) == 0x13))
						{
							break;
						}
						cnt++;
					}

					while(cnt != bytesFull.end())
					{	
						bytes.push_back(*cnt);
						cnt++;

					}

					// if(bytes.size() == bytesFull.size()) // original short version of payload 
					// {						
					// 	msgType = ntohs(*((uint16_t*)bytes.data()));
					// 	msgVersion = ntohs(*((uint16_tuint8_t*)&(bytes.data()[2])));
					// 	id = ntohs(*((uint16_t*)&(bytes.data()[4])));
					// 	dataLength = ntohs(*((uint16_t*)&(bytes.data()[6])));

					// 	vehID = ntohl(*((uint32_t*)&(bytes.data()[8]))); 
					// 	heaDing  = ntohl(*((uint32_t*)&(bytes.data()[12])));
					// 	spEed = ntohl(*((uint32_t*)&(bytes.data()[16])));
					// 	laTi = ntohl(*((uint32_t*)&(bytes.data()[20])));
					// 	loNg = ntohl(*((uint32_t*)&(bytes.data()[24]))); 
					// 	eleVate = ntohl(*((uint32_t*)&(bytes.data()[28]))); 
					// }
					// else{
					// 	msgType = ntohs(*((uint16_t*)bytes.data()));
					// 	msgVersion = ntohs(*((uint8_t*)bytesFull.data()));
					// 	id = ntohs(*((uint16_t*)&(bytes.data()[4])));
					// 	dataLength = ntohs(*((uint16_t*)&(bytes.data()[6])));

					// 	vehID = ntohl(*((uint32_t*)&(bytes.data()[8]))); 
					// 	heaDing  = ntohl(*((uint32_t*)&(bytes.data()[12])));
					// 	spEed = ntohl(*((uint32_t*)&(bytes.data()[16])));
					// 	laTi = ntohl(*((uint32_t*)&(bytes.data()[20])));
					// 	loNg = ntohl(*((uint32_t*)&(bytes.data()[24]))); 
					// 	eleVate = ntohl(*((uint32_t*)&(bytes.data()[28]))); 



					// }

						

					// bytes is  unint8_t vector 

					//if(bytes.data()[0] == 0x00)		
					//{


					msgType = ntohs(*((uint16_t*)bytes.data()));
					msgVersion = ntohs(*((uint16_t*)&(bytes.data()[2])));
					id = ntohs(*((uint16_t*)&(bytes.data()[4])));
					dataLength = ntohs(*((uint16_t*)&(bytes.data()[6])));

					// msgType = ntohs((uint16_t) bytes[4]);
					// msgVersion = ntohs(bytes[4+2]);
					// id = ntohs(bytes[4+4]);
					// dataLength = ntohs(bytes[6+4]);

					//}
					// if(bytes.data()[1]==128) 
					// {

					// 	//uint8_t bt1 = ntohs(*((uint8_t*)bytes.data()[1]));
					// 	//uint8_t bt2 = ntohs(*((uint8_t*)bytes.data()[2]));
					// 	uint8_t bt1 = (uint8_t) bytes.data()[1]; 
					// 	uint8_t bt2 = (uint8_t) bytes.data()[2];
					// 	if(bytes.data()[1] < 128)
					// 	{
					// 		msgType = ntohs(*((uint16_t*)bytes.data()[2]));
					// 		msgVersion = ntohs(*((uint16_t*)&(bytes.data()[2+2])));
					// 		id = ntohs(*((uint16_t*)&(bytes.data()[4+2])));
					// 		dataLength = ntohs(*((uint16_t*)&(bytes.data()[6+2])));
					// 	}
					// 	if(bytes.data()[1] ==128 && bytes.data()[2] <128)
					// 	{
					// 		msgType = ntohs(*((uint16_t*)bytes.data()[3]));
					// 		msgVersion = ntohs(*((uint16_t*)&(bytes.data()[2+3])));
					// 		id = ntohs(*((uint16_t*)&(bytes.data()[4+3])));
					// 		dataLength = ntohs(*((uint16_t*)&(bytes.data()[6+3])));
					// 	}
					// 	if(bytes.data()[1] ==128 && bytes.data()[2] ==129)
					// 	{
							
					// 		msgType = ntohs(*((uint16_t*)bytes.data()[4]));
					// 		msgVersion = ntohs(*((uint16_t*)&(bytes.data()[2+4])));
					// 		id = ntohs(*((uint16_t*)&(bytes.data()[4+4])));
					// 		dataLength = ntohs(*((uint16_t*)&(bytes.data()[6+4])));
					// 	}
					// 	if(bytes.data()[1] ==128 && bytes.data()[2] ==130)
					// 	{
					// 		msgType = ntohs(*((uint16_t*)bytes.data()[5]));
					// 		msgVersion = ntohs(*((uint16_t*)&(bytes.data()[2+5])));
					// 		id = ntohs(*((uint16_t*)&(bytes.data()[4+5])));
					// 		dataLength = ntohs(*((uint16_t*)&(bytes.data()[6+5])));
					// 	}
					// }
					


				
					PLOG(logDEBUG4) << "Got message,  msgType: " << msgType
							<< ", msgVersion: " << msgVersion
							<< ", id: " << id
							<< ", dataLength: " << dataLength;

					if (dataLength > 0)
					{
						switch (msgType)
						{
							case ABBR_BSM:
							if (bytes.size() >= 32 && dataLength >= 24)
							{
								if (!simBSM && !simLoc) return;

								//extract data
								//vehicleId(4), heading*M(4), speed*K(4), (latitude+180)*M(4), (longitude+180)*M(4), elevation (4)
								BsmMessage *bsm = DecodeBsm(ntohl(*((uint32_t*)&(bytes.data()[8]))),
										ntohl(*((uint32_t*)&(bytes.data()[12]))),
										ntohl(*((uint32_t*)&(bytes.data()[16]))),
										ntohl(*((uint32_t*)&(bytes.data()[20]))),
										ntohl(*((uint32_t*)&(bytes.data()[24]))),
										ntohl(*((uint32_t*)&(bytes.data()[28]))),
										decodedBsm);

								if (simLoc) {
									LocationMessage loc(::to_string(decodedBsm.get_TemporaryId()),
														location::SignalQualityTypes::SimulationMode,
														"", ::to_string(msg.get_timestamp()),
														decodedBsm.get_Latitude(), decodedBsm.get_Longitude(),
														location::FixTypes::ThreeD, 0, 0.0,
														decodedBsm.get_Speed_mps(), decodedBsm.get_Heading());
									loc.set_Altitude(decodedBsm.get_Elevation_m());

									routeable_message rMsg;
									rMsg.initialize(loc);

									this->IncomingMessage(rMsg, 0, 0, msg.get_timestamp());
								}

								sendMsg = encode(encodedBsm, bsm);
								if (!simBSM) return;
							}
							break;
						case ABBR_SRM:
							if (bytes.size() >= 32 && dataLength >= 24)
							{
								if (!simSRM) return;

								//extract data
								//vehicleId(4), heading*M(4), speed*K(4), (latitude+180)*M(4), (longitude+180)*M(4), role (4)
								SrmMessage *srm = DecodeSrm(ntohl(*((uint32_t*)&(bytes.data()[8]))),
										ntohl(*((uint32_t*)&(bytes.data()[12]))),
										ntohl(*((uint32_t*)&(bytes.data()[16]))),
										ntohl(*((uint32_t*)&(bytes.data()[20]))),
										ntohl(*((uint32_t*)&(bytes.data()[24]))),
										ntohl(*((uint32_t*)&(bytes.data()[28]))));
								sendMsg = encode(encodedSrm, srm);

							}
							break;
						default:
							{
								PLOG(logDEBUG4) << "Unknown byte format.  Dropping message";
							}
							return;
						}
					}
				}
				
			}
			catch (exception &ex)
			{
				this->HandleException(ex, false);
				return;
			}
		}
	}

	// Make sure the timestamp matches the incoming source message

	sendMsg->set_timestamp(msg.get_timestamp());

	// Keep a count of each type of message received
	string name(sendMsg->get_subtype());
	if (!IsJ2735Message(*sendMsg))
	{
		// If not a J2735 message, save the type also
		name.insert(0, "/");
		name.insert(0, sendMsg->get_type());
	}

	if (!totalCount.count(name))
		totalCount[name] = 1;
	else
		totalCount[name]++;

	// Check to see if forward is disabled for this type
	bool fwd = true;
	GetConfigValue(name, fwd);

	if (fwd)
	{
		PLOG(logDEBUG) << "Routing " << name << " message.";

		if (routeDsrc)
			sendMsg->set_flags(IvpMsgFlags_RouteDSRC);
		else
			sendMsg->set_flags(IvpMsgFlags_None);

		this->OutgoingMessage(*sendMsg);
	}
}

void MessageReceiverPlugin::UpdateConfigSettings()
{
	// Atomic flags
	GetConfigValue("RouteDSRC", routeDsrc);
	GetConfigValue("EnableSimulatedBSM", simBSM);
	GetConfigValue("EnableSimulatedSRM", simSRM);
	GetConfigValue("EnableSimulatedLocation", simLoc);

	lock_guard<mutex> lock(syncLock);

	GetConfigValue("IP", ip);
	GetConfigValue("Port", port);

	cfgChanged = true;
}

void MessageReceiverPlugin::OnConfigChanged(const char *key, const char *value)
{
	TmxMessageManager::OnConfigChanged(key, value);
	if (_plugin->state == IvpPluginState_registered)
		UpdateConfigSettings();
}

void MessageReceiverPlugin::OnStateChange(IvpPluginState state)
{
	TmxMessageManager::OnStateChange(state);

	if (state == IvpPluginState_registered)
	{
		UpdateConfigSettings();
	}
}

int MessageReceiverPlugin::Main()
{
	PLOG(logINFO) << "Starting plugin.";

	byte_stream incoming(4000);
	std::unique_ptr<tmx::utils::UdpServer> server;

	while (_plugin->state != IvpPluginState_error)
	{
		// See if the server values are different
		if (cfgChanged) {
			lock_guard<mutex> lock(syncLock);

			if (port > 0 && (
					!server || (server->GetAddress() != ip || server->GetPort() != port)))
			{
				PLOG(logDEBUG) << "Creating UDPServer ip " << ip << " port " << port;
				server.reset(new UdpServer(ip, port));
			}

			cfgChanged = false;
		}

		try
		{
			int len = server ? server->TimedReceive((char *)incoming.data(), incoming.size(), 5) : 0;

			if (len > 0)
			{
				uint64_t time = Clock::GetMillisecondsSinceEpoch();
				PLOG(logDEBUG) << "Received "  << len << " bytes.";

				totalBytes += len;

				// Support different encodings
				string enc;
				if (incoming.size() > 0)
				{
					switch (incoming[0]) {
					case 0x00:
						enc = api::ENCODING_ASN1_UPER_STRING;
						break;
					case 0x30:
						enc = api::ENCODING_ASN1_BER_STRING;
						break;
					case '{':
						enc = api::ENCODING_JSON_STRING;
						break;
					default:
						enc = api::ENCODING_BYTEARRAY_STRING;
						break;
					}
				}

				this->IncomingMessage(incoming.data(), len, enc.empty() ? NULL : enc.c_str(), 0, 0, time);
			}
			else if (len < 0)
			{
				if (errno != EAGAIN && errThrottle.Monitor(errno))
				{
					PLOG(logERROR) << "Could not receive from socket: " << strerror(errno);
				}
			}
		}
		catch (exception &ex)
		{
			this->HandleException(ex, false);
		}

		if (statThrottle.Monitor(1))
		{
			uint64_t b = totalBytes;
			auto msCount = Clock::GetMillisecondsSinceEpoch() - Clock::GetMillisecondsSinceEpoch(this->_startTime);

			SetStatus("Total KBytes Received", b / 1024.0);

			for (auto iter = totalCount.begin(); iter != totalCount.end(); iter++) {
				string param("Avg ");
				param += iter->first;
				param += " Message Interval (ms)";

				uint32_t c = totalCount[iter->first];
				SetStatus(param.c_str(), c == 0 ? 0 : 1.0 * msCount / c);

				param = "Total ";
				param += iter->first;
				param += " Messages Received";
				SetStatus(param.c_str(), c);
			}
		}
	}

	return 0;
}

} /* namespace MessageRec*1000eiver */

int main(int argc, char *argv[])
{
	return run_plugin<MessageReceiver::MessageReceiverPlugin>("MessageReceiver", argc, argv);
}
