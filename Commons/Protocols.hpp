#pragma once

#include "MessagingAppComm.hpp"
#include <thread>

namespace msgapp
{
	namespace prot
	{
		enum class ProtocolStatus { NO_AUTHORITY, SUCCESS, FAILED };

		class ConnectionValidation
		{
		public:
			virtual ~ConnectionValidation() {}

			virtual UserFate validateClient(MessagingClient& client) = 0;
		};

		class Protocols
		{
		public:

			static ProtocolStatus connection(MessagingClient& client, ConnectionValidation* validation = nullptr, int number_attempts = 2, int timeout = 500)
			{
				const Authority auth = client.getAuthority();
				if (auth == Authority::SERVER)
				{
					if (serverSideConnectionProtocol(client, number_attempts, timeout) == ProtocolStatus::SUCCESS)
					{
						if (validation != nullptr)
						{
							if (validation->validateClient(client) == UserFate::ACCEPT)
							{
								client.sendSignal((char)MessageKind::ACCEPT);
								return ProtocolStatus::SUCCESS;
							}
							else
							{
								client.sendSignal((char)MessageKind::REJECT);
								return ProtocolStatus::FAILED;
							}
						}

						client.sendSignal((char)MessageKind::ACCEPT);
						return ProtocolStatus::SUCCESS;
					}
					else
					{
						client.sendSignal((char)MessageKind::REJECT);
						return ProtocolStatus::FAILED;
					}
				}
				else if (auth == Authority::CLIENT)
				{
					return clientSideConnectionProtocol(client);
				}
				else
				{
					return ProtocolStatus::NO_AUTHORITY;
				}
			}

			static std::expected<uint32_t, ProtocolStatus> requestMaximumChatMessageLength(MessagingClient& client)
			{
				if (client.getAuthority() != Authority::CLIENT)
				{
					{ LOG_ERROR("Client with ID % has no authority to run this.\nAuthority: %", client.getID(), authorityToString(client.getAuthority())) }
					return std::unexpected(ProtocolStatus::NO_AUTHORITY);
				}

				if (netw::SBytes result = client.sendSignal((char)MessageKind::CLIENT_REQUEST_MAX_CHAT_MSG_LEN); CHECK_RESULT(result))
				{
					SYSTEM_ERROR("Connection error")
				}

				MsgHeaderValues values;
				if (netw::SBytes result = client.readMessageHeader(values); CHECK_RESULT(result))
				{
					SYSTEM_ERROR("Connection error")
				}

				const MessageKind response = (MessageKind)values.first;
				if (response != MessageKind::ACCEPT && response != MessageKind::REJECT)
				{
					{ LOG_ERROR("Request for maximum chat message length failed due to unexpected response: %", messageKindToString(response)) }
					return std::unexpected(ProtocolStatus::FAILED);
				}

				if (response == MessageKind::ACCEPT)
				{
					netw::bytes_t<uint32_t> char_len_bytes;
					if (netw::SBytes result = client.receiveMessageRaw(char_len_bytes.data(), char_len_bytes.size()); CHECK_RESULT(result))
					{
						SYSTEM_ERROR("Connection error")
					}

					uint32_t msg_len = netw::deserializeType<uint32_t>(char_len_bytes);
					return msg_len;
				}
				else
				{
					return std::unexpected(ProtocolStatus::FAILED);
				}
			}

			static ProtocolStatus responseMaximumChatMessageLength(MessagingClient& client, MessageKind response, netw::SBuffer* buffer)
			{
				if (client.getAuthority() != Authority::SERVER)
				{
					{ LOG_ERROR("Client with ID % has no authority to run this.\nAuthority: %", client.getID(), authorityToString(client.getAuthority())) }
					return ProtocolStatus::NO_AUTHORITY;
				}
				if (response != MessageKind::ACCEPT && response != MessageKind::REJECT)
				{
					sendRejectForInvalidResponse(client, response);
					return ProtocolStatus::FAILED;
				}

				if (buffer == nullptr || response == MessageKind::REJECT)
				{
					sendRejectResponse(client);
					return ProtocolStatus::SUCCESS;
				}

				addMessageHeader((char)MessageKind::ACCEPT, *buffer);
				if (netw::SBytes result = client.sendMessage(*buffer); CHECK_RESULT(result))
				{
					return ProtocolStatus::FAILED;
				}
				else
				{
					return ProtocolStatus::SUCCESS;
				}
			}

			static std::expected<ClientsInfo, ProtocolStatus> requestClientsInfo(MessagingClient& client)
			{
				const Authority auth = client.getAuthority();
				if (auth != Authority::CLIENT)
				{
					{ LOG_ERROR("Client with ID % has no authority to run this.\nAuthority: %", client.getID(), authorityToString(client.getAuthority())) }
					return std::unexpected(ProtocolStatus::NO_AUTHORITY);
				}

				if (netw::SBytes result = client.sendSignal((char)MessageKind::CLIENT_REQUEST_CLIENTS_INFO); CHECK_RESULT(result)) 
				{ 
					SYSTEM_ERROR("Connection error") 
				}
				
				MsgHeaderValues values;
				if(netw::SBytes result = client.readMessageHeader(values); CHECK_RESULT(result)) 
				{ 
					SYSTEM_ERROR("Connection error") 
				}

				const MessageKind response = (MessageKind)values.first;
				if (response == MessageKind::REJECT) 
				{
					return std::unexpected(ProtocolStatus::FAILED);
				}

				const uint64_t buffer_size = values.second;
				netw::SBuffer buffer(buffer_size);

				if (netw::SBytes result = client.receiveMessage(buffer); CHECK_RESULT(result)) 
				{ 
					SYSTEM_ERROR("Connection error") 
				}

				ClientsInfo infos;
				infos.deserialize(buffer);

				return infos;
			}

			static ProtocolStatus responseClientsInfo(MessagingClient& client, MessageKind response, netw::SBuffer* buffer)
			{
				if (client.getAuthority() != Authority::SERVER)
				{
					{ LOG_ERROR("Client with ID % has no authority to run this", client.getID()) }
					return ProtocolStatus::NO_AUTHORITY;
				}
				if (response != MessageKind::ACCEPT && response != MessageKind::REJECT)
				{
					sendRejectForInvalidResponse(client, response);
					return ProtocolStatus::FAILED;
				}

				addMessageHeader((char)MessageKind::ACCEPT, *buffer);
				if (netw::SBytes result = client.sendMessage(*buffer); CHECK_RESULT(result))
				{
					return ProtocolStatus::FAILED;
				}
				else
				{
					return ProtocolStatus::SUCCESS;
				}
			}

		private:

			Protocols() {}

			static ProtocolStatus serverSideConnectionProtocol(MessagingClient& client, int number_attempts, int timeout)
			{
				if (number_attempts < 0) number_attempts = 2;
				if (timeout < 0) timeout = 500;

				if (netw::SBytes result = client.sendSignal((char)MessageKind::SERVER_REQUEST_CLIENT_NAME); CHECK_RESULT(result))
				{
					{ LOG_ERROR("Connection error in connection protocol") }
					return ProtocolStatus::FAILED;
				}

				client.setBlocking(false);

				std::chrono::milliseconds TIMEOUT(timeout);

				std::string client_name;
				for (int attempt = 0; attempt < number_attempts; ++attempt)
				{
					MsgHeaderValues header_values;
					if (netw::SBytes result = client.readMessageHeader(header_values); CHECK_RESULT(result))
					{
						int error = netw::lastError();
						if (error == netw::errs::NET_WOULDBLOCK)
						{
							std::this_thread::sleep_for(TIMEOUT);
							continue;
						}
						else
						{
							{ SYSTEM_ERROR_NUM(error, "Connection error") }
						}
					}
					
					const MessageKind response = (MessageKind) header_values.first;
					const uint64_t buffer_size = header_values.second;
					
					if (response != MessageKind::ACCEPT || buffer_size > NAME_LEN || buffer_size == 0u)
					{
						{ LOG_INFO("Client failed to complete the protocol.\nResponse: %, Name size: %", messageKindToString(response), buffer_size) }
						return ProtocolStatus::FAILED;
					}

					netw::SBuffer name_buffer(buffer_size);
					if (netw::SBytes result = client.receiveMessage(name_buffer); CHECK_RESULT(result))
					{
						{ LOG_ERROR("Connection error in connection protocol") }
						return ProtocolStatus::FAILED;
					}

					copySBufferToStr(name_buffer, client_name);
					client.setName(client_name);
					break;
				}

				client.setBlocking(true);
				if (client_name.empty())
				{
					return ProtocolStatus::FAILED;
				}
				else
				{
					return ProtocolStatus::SUCCESS;
				}
			}

			static ProtocolStatus clientSideConnectionProtocol(MessagingClient& client)
			{
				char server_signal = '\0';
				if (netw::SBytes result = client.receiveSignal(&server_signal); CHECK_RESULT(result))
				{
					{ LOG_INFO("Connection error in connection protocol") }
					return ProtocolStatus::FAILED;
				}

				const MessageKind server_msg = (MessageKind) server_signal;
				if (server_msg != MessageKind::SERVER_REQUEST_CLIENT_NAME)
				{
					{ LOG_ERROR("Accept protocol incorrect! Expected % request, but got %", messageKindToString(MessageKind::SERVER_REQUEST_CLIENT_NAME), messageKindToString(server_msg)) }
					return ProtocolStatus::FAILED;
				}

				netw::SBuffer buffer = copyStrToSBuffer(client.getName());
				addMessageHeader((char)MessageKind::ACCEPT, buffer);

				if (netw::SBytes result = client.sendMessage(buffer); CHECK_RESULT(result))
				{
					{ LOG_INFO("Connection error in connection protocol") }
					return ProtocolStatus::FAILED;
				}

				char signal_fate = '\0';
				if (netw::SBytes result = client.receiveSignal(&signal_fate); CHECK_RESULT(result))
				{
					{ LOG_INFO("Connection error in connection protocol") }
					return ProtocolStatus::FAILED;
				}
			
				return ProtocolStatus::SUCCESS;
			}

			/*
			  A response from server should be either ACCEPT or REJECT, but if it's not, then this should be called.
			*/
			static void sendRejectForInvalidResponse(MessagingClient& client, MessageKind response)
			{
				{ LOG_ERROR("Invalid response.\nResponse was %.\nSending reject message...", messageKindToString(response)) }
				sendRejectResponse(client);
			}

			static void sendRejectResponse(MessagingClient& client)
			{
				netw::SBuffer reject_buffer;
				addMessageHeader((char)MessageKind::REJECT, reject_buffer);

				client.sendMessage(reject_buffer);
			}
		};
	}
}