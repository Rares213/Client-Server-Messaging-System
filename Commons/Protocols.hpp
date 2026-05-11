#pragma once

#include "MessagingAppComm.hpp"

namespace msgapp
{
	namespace prot
	{
		enum class ProtocolStatus { NO_AUTHORITY, SUCCESS, FAILED };
		
		class Protocols
		{
		public:

			static ProtocolStatus connection(MessagingClient& client, int number_attempts = 2, int timeout = 500)
			{
				const Authority auth = client.getAuthority();
				if (auth == Authority::SERVER)
				{
					return serverSideConnectionProtocol(client, number_attempts, timeout);
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

			static std::expected<uint32_t, ProtocolStatus> maximumChatMessageLength(MessagingClient& client)
			{
				const Authority auth = client.getAuthority();
				if (auth != Authority::CLIENT)
				{
					{ LOG_ERROR("Client with ID % has no authority to run this", client.getID()) }
					return std::unexpected(ProtocolStatus::NO_AUTHORITY);
				}

				if (netw::SBytes result = client.sendSignal((char)Request::MAX_CHAT_MSG_LEN); CHECK_RESULT(result)) { SYSTEM_ERROR("Connection error") }

				MsgHeaderValues values;
				if (netw::SBytes result = client.readMessageHeader(values); CHECK_RESULT(result)) {SYSTEM_ERROR("Connection error")}
				
				netw::bytes_t<uint32_t> char_len_bytes;
				if (netw::SBytes result = client.receiveMessageRaw(char_len_bytes.data(), char_len_bytes.size()); CHECK_RESULT(result)) { SYSTEM_ERROR("Connection error") }
				
				uint32_t msg_len = netw::deserializeType<uint32_t>(char_len_bytes);
				return msg_len;
			}

			static std::expected<ClientsInfo, ProtocolStatus> clientsInfo(MessagingClient& client)
			{
				const Authority auth = client.getAuthority();
				if (auth != Authority::CLIENT)
				{
					{ LOG_ERROR("Client with ID % has no authority to run this", client.getID()) }
					return std::unexpected(ProtocolStatus::NO_AUTHORITY);
				}

				if (netw::SBytes result = client.sendSignal((char)Request::CLIENTS_INFO); CHECK_RESULT(result)) { SYSTEM_ERROR("Connection error") }
				
				MsgHeaderValues values;
				if(netw::SBytes result = client.readMessageHeader(values); CHECK_RESULT(result)) { SYSTEM_ERROR("Connection error") }

				const char response = values.first;
				if (response == (char)Response::REJECT) return std::unexpected(ProtocolStatus::FAILED);

				const uint64_t buffer_size = values.second;
				netw::SBuffer buffer(buffer_size);

				if (netw::SBytes result = client.receiveMessage(buffer); CHECK_RESULT(result)) { SYSTEM_ERROR("Connection error") }

				ClientsInfo infos;
				infos.deserialize(buffer);

				return infos;
			}



		private:

			Protocols() {}

			static ProtocolStatus serverSideConnectionProtocol(MessagingClient& client, int number_attempts, int timeout)
			{
				if (number_attempts < 0) number_attempts = 2;
				if (timeout < 0) timeout = 500;

				if (netw::SBytes result = client.sendSignal((char)Request::CLIENT_NAME); CHECK_RESULT(result))
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
					
					const char response = header_values.first;
					const uint64_t buffer_size = header_values.second;
					
					if (response != (char)Response::ACCEPT || buffer_size > NAME_LEN || buffer_size == 0u)
					{
						{ LOG_INFO("Client failed to complete the protocol.\nResponse: %, Name size: %", response, buffer_size) }
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
				return ProtocolStatus::SUCCESS;
			}

			static ProtocolStatus clientSideConnectionProtocol(MessagingClient& client)
			{
				char server_signal = '\0';
				if (netw::SBytes result = client.receiveSignal(&server_signal); CHECK_RESULT(result))
				{
					{ LOG_INFO("Connection error in connection protocol") }
					return ProtocolStatus::FAILED;
				}

				if (server_signal != (char)Request::CLIENT_NAME)
				{
					{ LOG_ERROR("Accept protocol incorrect! Expected CLIENT_NAME request.") }
					return ProtocolStatus::FAILED;
				}

				netw::SBuffer buffer = copyStrToSBuffer(client.getName());
				addMessageHeader((char)Response::ACCEPT, buffer);

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



		};
	}
}