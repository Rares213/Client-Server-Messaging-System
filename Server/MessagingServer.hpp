#pragma once

#include "../Commons/netw.hpp"
#include "../Commons/logger.hpp"
#include "../Commons/MessagingAppComm.hpp"
#include "../Commons/Protocols.hpp"

#include <thread>
#include <cstdlib>
#include <array>


namespace msgapp
{
	class MessagingServer;
	class MessagingClientServer;
	class MessageProcessor;
	class Broadcaster;

	using ClientsList = std::unordered_map<ID, MessagingClientServer>;
	using PendingClient = std::pair<ID, MessagingClientServer>;

	class MessagingClientServer : public MessagingClient
	{
	public:
		MessagingClientServer() {}
		MessagingClientServer(netw::Client client) : MessagingClient(Authority::SERVER, std::move(client)) {}
		virtual ~MessagingClientServer() {}

		MessagingClientServer(MessagingClientServer&& other) noexcept : MessagingClient(std::move(other))
		{

		}


	};
	
	class MessagingServer : public netw::TCPServer
	{
	public:
		MessagingServer(const char* port) : netw::TCPServer(getHints(), port) {}
		virtual ~MessagingServer() {}

		MessagingClientServer* getClient(ID id) noexcept
		{
			try
			{
				return &m_clients.at(id);
			}
			catch (const std::exception& error)
			{
				{ LOG_ERROR(error.what()) }

				return nullptr;
			}
		}

		int32_t getMaxChatLen() const noexcept { return MAX_MSG_LEN; }

		ClientsList& getClientsList() noexcept { return m_clients; }

		ClientsInfo getClientsInfo()
		{
			ClientsInfo info;

			for (const auto& client_pair : m_clients)
			{
				info.addClientInfo(client_pair.first, client_pair.second.getName());
			}

			return info;
		}

		void setMessagerProcessor(MessageProcessor* msg_processor) noexcept { m_msg_processor = msg_processor; }

		void setBroadcaster(Broadcaster* broadcaster) noexcept { m_broadcaster = broadcaster; }

		static void init()
		{
			netw::initNetw();
		}

		void run()
		{
			startListening(m_should_listening_block);

			m_connection_thread = std::jthread(&MessagingServer::threadIncommingClients, this);
			m_messaging_thread = std::jthread(&MessagingServer::threadIncomingMessages, this);

		}

		void shutdown()
		{
			m_shutdown = true;
		}

		void kickClient(int id)
		{
			if (id < 0)
			{
				return;
			}

			deleteClient(id);
		}

		virtual netw::ClientsMessages pollSockets(int timeout = 0) override
		{
			handleDeletionQueue();
			handlePendings();

			return netw::TCPServer::pollSockets(timeout);
		}

		virtual void removeClientPolling(netw::Socket_t client_socket, int error) override
		{
			deleteClient(client_socket);

			TCPServer::removeClientPolling(client_socket, error);
		}

	protected:

		void threadIncommingClients();

		void threadIncomingMessages();

		bool m_shutdown = false;
		bool m_should_listening_block = false;
		int m_listening_timeout = 1000;

		std::jthread m_connection_thread;
		std::jthread m_messaging_thread;
		std::mutex m_mut;

		std::queue<PendingClient> m_pendings;
		std::queue<ID> m_delete_pendings;

		std::unordered_map<ID, MessagingClientServer> m_clients;

		MessageProcessor* m_msg_processor = nullptr;
		Broadcaster* m_broadcaster = nullptr;

		static addrinfo getHints()
		{
			addrinfo hints = {};

			hints.ai_family = AF_INET;
			hints.ai_socktype = SOCK_STREAM;
			hints.ai_protocol = IPPROTO_TCP;
			hints.ai_flags = AI_PASSIVE;

			return hints;
		}
		
		UserFate decideUserFate(MessagingClientServer& client)
		{	
			prot::ProtocolStatus status = prot::Protocols::connection(client);

			if (status == prot::ProtocolStatus::SUCCESS)
			{
				return UserFate::ACCEPT;
			}
			else
			{
				return UserFate::REJECT;
			}

		}

		void addClient(ID id, MessagingClientServer client)
		{
			const std::lock_guard lock(m_mut);
			m_pendings.push({ id, std::move(client) });
		}

		void handlePendings()
		{
			const std::lock_guard lock(m_mut);
			while (!m_pendings.empty())
			{
				PendingClient pending_client = std::move(m_pendings.front());

				addClientPolling(pending_client.second);
				auto iter_pair = m_clients.emplace(pending_client.first, std::move(pending_client.second));
				
				if (iter_pair.second == true)
				{
					MessagingClientServer& new_client = iter_pair.first->second;
					announceNewClient(new_client);
				}
				else
				{
					{ LOG_ERROR("Failed to add new client somehow with id % ???", pending_client.first) }
				}

				m_pendings.pop();
			}
		}

		void deleteClient(ID client_id)
		{
			if (m_clients.find(client_id) != m_clients.end())
			{
				m_delete_pendings.push(client_id);
			}
		}

		void handleDeletionQueue()
		{
			while (!m_delete_pendings.empty())
			{
				ID id_delete = m_delete_pendings.front();

				try
				{
					LOG_INFO("Deleting client with ID % Name: %", id_delete, m_clients.at(id_delete).getName())
				}
				catch (const std::exception& error)
				{
					LOG_ERROR(error.what())
				}
				

				// this should never be null, but idk
				MessagingClientServer* client = getClient(id_delete);
				if (client != nullptr)
				{
					announceLeavingClient(*client);
					m_clients.erase(id_delete);
				}

				m_delete_pendings.pop();
			}
		}
		
		/*
		  Chat message about new client joining.
		*/
		netw::SBuffer createNewClientJoinedChatMessage(const std::string& name);

		/*
		  Sends a notification to clients about a new client.
		*/
		netw::SBuffer createNewClientNotification(ID id, std::string_view name);

		/*
		  Chat message about client disconnecting.
		*/
		netw::SBuffer createClientDisconnectChatMessage(const std::string& name);

		/*
		  Sends a notification to client about a client disconnecting.
		*/
		netw::SBuffer createClientDisconnectNotification(ID id);

		void announceNewClient(const MessagingClientServer& new_client);

		void announceLeavingClient(const MessagingClientServer& new_client);

	};

	class MessageProcessor
	{
	public:
		MessageProcessor(MessagingServer& messaging_server) : m_server(messaging_server), m_separator(": ") {}

		void setSeparator(const std::string& separator) noexcept { m_separator = separator; }

		std::string_view getSeparator() const noexcept { return m_separator; }

		void processMessage(netw::ClientsMessages& msgs)
		{
			for (netw::ClientMessage& msg : msgs)
			{
				processClientMessage(msg);
			}
		}

		void processMessage(netw::ClientMessage& msg)
		{
			processClientMessage(msg);
		}

	private:
						
		MessagingServer& m_server;
		std::string m_separator;

	private:

		void processClientMessage(netw::ClientMessage& msg)
		{
			unsigned char option = static_cast<unsigned char>(msg.buffer[0]);

			if (option >= 10u && option <= 99u)
			{
				{ LOG_INFO("Processing request % from %", requestToStr((Request)option), (ID)msg.socket_client) }

				processRequest(msg, (Request)option);
			}
			else if (option >= 100)
			{
				{ LOG_INFO("Processing command % from %", commandToStr((Command)option), (ID)msg.socket_client) }

				processCommand(msg, (Command)(option));
			}
			else
			{
				{ LOG_ERROR("Invalid byte from %. Byte was %", msg.socket_client, (unsigned char)msg.buffer[0]) }
				msg.buffer.clear();
				msg.buffer.push_back((char)Response::INVALID);
			}
		}

		void processRequest(netw::ClientMessage& client_msg, Request request)
		{
			MessagingClientServer* client = m_server.getClient((ID)client_msg.socket_client);
			if (client == nullptr)
			{
				{ LOG_ERROR("Client with ID % was not found to process request", client_msg.socket_client) }
				client_msg.buffer[0] = (char)Response::INVALID;
				return;
			}

			client_msg.buffer.clear();
			switch (request)
			{
			case Request::MAX_CHAT_MSG_LEN:
				processRequestMaxChatMsgLen(client_msg.buffer, m_server.getMaxChatLen());

				{ LOG_INFO("Processed max chat message length request from %", client_msg.socket_client) }
				break;

			case Request::CLIENTS_INFO:
			{
				ClientsInfo info = m_server.getClientsInfo();
				processRequestClientsInfo(client_msg, info);

				{ LOG_INFO("Processed clients info request from %", client_msg.socket_client) }
				break;
			}

			default:
				client_msg.buffer.push_back((char)Response::INVALID);

				{ LOG_INFO("Invalid request from %", client_msg.socket_client) }
				return;
			}

			addMessageHeader((char)Response::ACCEPT, client_msg.buffer);
		}

		void processCommand(netw::ClientMessage& client_msg, Command cmd)
		{
			MessagingClientServer* client = m_server.getClient((ID)client_msg.socket_client);
			if (client == nullptr)
			{
				{ LOG_ERROR("Client with ID % was not found to process command", client_msg.socket_client) }
				client_msg.buffer[0] = (char)Response::INVALID;
				return;
			}

			//removeMessageHeader(client_msg.buffer);

			switch (cmd)
			{
			case Command::CHAT_MSG:
				client_msg.buffer.erase(client_msg.buffer.begin());
				processChatMsg(client_msg, client->getName(), m_separator);

				{ LOG_INFO("Processed chat message from %", client_msg.socket_client) }
				break;

			default:
				client_msg.buffer.clear();
				client_msg.buffer.push_back((char)Response::INVALID);

				{ LOG_INFO("Invalid command from %", client_msg.socket_client) }
				return;
			}

			addMessageHeader((char)cmd, client_msg.buffer);
		}

		static void processChatMsg(netw::ClientMessage& client_msg, const std::string& client_name, std::string_view seperator)
		{
			// the message send to all the client will be in the for
			// [name][: ][message]

			const size_t total_size = client_name.size() + seperator.size() + client_msg.buffer.size();
			netw::SBuffer complete_msg(total_size);

			// keep track where to start writing
			size_t offset_compl_msg = 0u;

			// add [name]
			processChatMsgAttachName(complete_msg, offset_compl_msg, client_name);
			offset_compl_msg += client_name.size();

			// add [: ]
			processChatMsgAttachSeperator(complete_msg, offset_compl_msg, seperator);
			offset_compl_msg += seperator.size();

			// add [message]
			processChatMsgAttachMsg(complete_msg, offset_compl_msg, client_msg.buffer);
			offset_compl_msg += client_msg.buffer.size();

			client_msg.buffer = std::move(complete_msg);
		}

		static void processRequestClientsInfo(netw::ClientMessage& client_msg, ClientsInfo& info)
		{
			info.removeClientInfo((ID)client_msg.socket_client);

			netw::SBuffer& buffer = client_msg.buffer;

			buffer = std::move(info.serialize());
		}

		static void processRequestMaxChatMsgLen(netw::SBuffer& buffer, int32_t max_msg_len)
		{
			netw::bytes_t<uint32_t> msg_len_bytes = netw::serializeType(max_msg_len);
			for (size_t i = 0; i < msg_len_bytes.size(); ++i)
			{
				buffer.push_back(msg_len_bytes[i]);
			}
			buffer.shrink_to_fit();
		}

	private:

		static void processChatMsgAttachCommd(netw::SBuffer& buffer, size_t buffer_start_index, Command cmd)
		{
			buffer[buffer_start_index] = static_cast<char>(cmd);
		}

		static void processChatMsgAttachName(netw::SBuffer& buffer, size_t buffer_start_index, const std::string& name)
		{
			std::copy(name.begin(), name.end(), buffer.begin() + buffer_start_index);
		}

		static void processChatMsgAttachSeperator(netw::SBuffer& buffer, size_t buffer_start_index, std::string_view separator)
		{
			std::copy(separator.begin(), separator.end(), buffer.begin() + buffer_start_index);
		}

		static void processChatMsgAttachMsg(netw::SBuffer& buffer, size_t buffer_start_index, const netw::SBuffer& msg_buffer)
		{
			std::copy(msg_buffer.begin(), msg_buffer.end(), buffer.begin() + buffer_start_index);
		}

	};

	class Broadcaster
	{
	public:
		Broadcaster(MessagingServer& server) : m_server(server) {}

		void broadcastMessages(netw::ClientsMessages& msgs)
		{
			for (netw::ClientMessage& msg : msgs)
			{
				broadcastMessage(msg);
			}
		}

		void broadcastMessages(netw::ClientMessage& msg)
		{
			broadcastMessage(msg);
		}

		void serverBroadcastMessages(netw::SBuffer msg, ID user_except = netw::errs::NET_INVALID_SOCKET)
		{
			Command option = (Command)msg[0];

			switch (option)
			{
			case Command::SERVER_MSG:
			case Command::CLIENT_JOINED:
			case Command::CLIENT_DISCONNECT:
				for (auto& client : m_server.getClientsList())
				{
					if (user_except != netw::errs::NET_INVALID_SOCKET && user_except == client.second.getID())
					{
						continue;
					}

					client.second.sendMessage(msg);
				}
				break;

			default: { LOG_ERROR("Invalid server message for broadcast. Command was: %", commandToStr(option)) }; break;
			}

		}

	private:

		MessagingServer& m_server;

		void broadcastMessage(netw::ClientMessage& msg)
		{
			ClientsList& clients = m_server.getClientsList();
			
			unsigned char option = static_cast<unsigned char>(msg.buffer[0]);

			if (option >= 0 && option <= 9)
			{
				ID id_client = static_cast<ID>(msg.socket_client);
				MessagingClient& client = clients[id_client];
				client.sendMessage(msg.buffer);
			}
			else if (option >= 100 && option <= 255)
			{
				for (auto& client_pair : clients)
				{
					if (client_pair.first != static_cast<ID>(msg.socket_client))
					{
						client_pair.second.sendMessage(msg.buffer);
					}
				}
			}
			else
			{
				{ LOG_ERROR("Invalid message to broadcast: ID % | option %", msg.socket_client, option) }
			}

		}

	};

	void MessagingServer::threadIncommingClients()
	{
		std::chrono::milliseconds TIMEOUT_ACCEPT(m_listening_timeout);

		while (!m_shutdown)
		{
			try
			{
				{ LOG_DEBUG("Waiting for connection...") }

				std::expected<netw::Client, int> result = acceptConnection();
				if (!result.has_value())
				{
					{ LOG_DEBUG("No connection; timeout") }

					std::this_thread::sleep_for(TIMEOUT_ACCEPT);
				}
				else
				{
					MessagingClientServer server_client(std::move(result.value()));
					if (UserFate fate = decideUserFate(server_client); fate == UserFate::ACCEPT)
					{
						{ LOG_INFO("User with ID % and name % accepted", server_client.getID(), server_client.getName()) }

						server_client.sendSignal((char)Response::ACCEPT);

						const ID id = server_client.getID();
						const std::string name = server_client.getName();

						addClient(id, std::move(server_client));
					}
					else
					{
						{ LOG_INFO("User with ID % and name % rejected", server_client.getID(), server_client.getName()) }

						server_client.sendSignal((char)Response::REJECT);
					}
				}
			}
			catch (const std::system_error& error)
			{
				LOG_ERROR(error.what())
			}
		}
		{ LOG_INFO("Thread incoming users exiting...") }
	}

	void MessagingServer::threadIncomingMessages()
	{
		std::chrono::milliseconds TIMEOUT_MESSAGES(m_listening_timeout);

		while (!m_shutdown)
		{
			try
			{
				{ LOG_DEBUG("Waiting for messages...") }

				netw::ClientsMessages msgs = pollSockets();
				if (msgs.empty())
				{
					std::this_thread::sleep_for(TIMEOUT_MESSAGES);
				}
				else
				{
					if (m_msg_processor != nullptr)
					{
						{ LOG_DEBUG("Processing messages") }
						m_msg_processor->processMessage(msgs);
					}


					if (m_broadcaster != nullptr)
					{
						{ LOG_DEBUG("Sending messages") }
						m_broadcaster->broadcastMessages(msgs);
					}
				}
			}
			catch (const std::system_error& error)
			{
				{ LOG_ERROR(error.what()) }
			}
		}
		{ LOG_INFO("Thread incoming messages exiting...") }
	}

	netw::SBuffer MessagingServer::createNewClientJoinedChatMessage(const std::string& name)
	{
		std::string separator;
		if (m_msg_processor != nullptr)
		{
			separator = m_msg_processor->getSeparator();
		}
		else
		{
			separator = ": ";
		}

		std::string server_msg("Server" + separator + name + " has connected.");
		netw::SBuffer buffer = copyStrToSBuffer(server_msg);

		addMessageHeader((char)Command::SERVER_MSG, buffer);

		return buffer;
	}

	netw::SBuffer MessagingServer::createClientDisconnectChatMessage(const std::string& name)
	{
		std::string separator;
		if (m_msg_processor != nullptr)
		{
			separator = m_msg_processor->getSeparator();
		}
		else
		{
			separator = ": ";
		}

		std::string server_msg("Server" + separator + name + " has disconnected.");
		netw::SBuffer buffer = copyStrToSBuffer(server_msg);

		addMessageHeader((char)Command::SERVER_MSG, buffer);

		return buffer;
	}

	netw::SBuffer MessagingServer::createNewClientNotification(ID id, std::string_view name)
	{
		netw::bytes_t<ID>      id_bytes = netw::serializeType(id);
		netw::bytes_t<uint8_t> name_size_bytes = netw::serializeType((uint8_t)name.size());
		netw::SBuffer          name_bytes = netw::serializeBasicStr(name.data());

		netw::SBuffer client_joined_msg(id_bytes.size() + name_size_bytes.size() + name_bytes.size());

		std::copy(id_bytes.begin(), id_bytes.end(), client_joined_msg.begin());
		std::copy(name_size_bytes.begin(), name_size_bytes.end(), client_joined_msg.begin() + id_bytes.size());
		std::copy(name_bytes.begin(), name_bytes.end(), client_joined_msg.begin() + id_bytes.size() + name_size_bytes.size());

		addMessageHeader((char)Command::CLIENT_JOINED, client_joined_msg);
		return client_joined_msg;
	}

	netw::SBuffer MessagingServer::createClientDisconnectNotification(ID id)
	{
		netw::bytes_t<ID> id_bytes = netw::serializeType(id);

		netw::SBuffer client_disc_msg(id_bytes.size());
		std::copy(id_bytes.begin(), id_bytes.end(), client_disc_msg.begin());

		addMessageHeader((char)Command::CLIENT_DISCONNECT, client_disc_msg);
		return client_disc_msg;
	}

	void MessagingServer::announceNewClient(const MessagingClientServer& client)
	{
		if (m_clients.empty())
		{
			return;
		}
		
		netw::SBuffer new_client_chat_msg = createNewClientJoinedChatMessage(client.getName());
		netw::SBuffer new_client_notification = createNewClientNotification(client.getID(), client.getName());

		if (m_broadcaster != nullptr)
		{
			m_broadcaster->serverBroadcastMessages(new_client_notification, client.getID());
			m_broadcaster->serverBroadcastMessages(new_client_chat_msg, client.getID());
		}
	}

	void MessagingServer::announceLeavingClient(const MessagingClientServer& client)
	{
		if (m_clients.empty())
		{
			return;
		}
		
		netw::SBuffer client_disc_notification = createClientDisconnectNotification(client.getID());
		netw::SBuffer client_disc_chat_msg = createClientDisconnectChatMessage(client.getName());

		if (m_broadcaster != nullptr)
		{
			m_broadcaster->serverBroadcastMessages(client_disc_notification, client.getID());
			m_broadcaster->serverBroadcastMessages(client_disc_chat_msg, client.getID());
		}
	}

};


