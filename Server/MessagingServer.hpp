#pragma once

#include "../Commons/netw.hpp"
#include "../Commons/logger.hpp"
#include "../Commons/MessagingAppComm.hpp"
#include "../Commons/Protocols.hpp"
#include "../Commons/Timer.hpp"


#include <thread>
#include <cstdlib>
#include <array>


namespace msgapp
{
	class MessagingServer;
	class MessagingClientServer;
	class MessagingPollRead;
	class MessageProcessor;
	class Broadcaster;
	class TimerMessagesCompleted;

	struct ClientMessageKind;
	struct CollectionClientMessageKind;

	using ClientsList = std::unordered_map<ID, MessagingClientServer>;
	using PendingClient = std::pair<ID, MessagingClientServer>;

	struct ClientMessageKind
	{
		MessageKind msg_kind = MessageKind::INVALID;
		netw::ClientMessage msg;
	};

	class TimerMessagesCompleted : public timer::Timeable
	{
	public:

		virtual ~TimerMessagesCompleted() {}

		virtual void onTimerUp() override
		{
			uint64_t msg_count = m_messages_count.exchange(0u);
			{ LOG_INFO("% messages completed in % ms", msg_count, m_delta.load(std::memory_order_relaxed)) }
		}

		void setDelta(int delta) 
		{ 
			if (delta < 0)
			{
				m_delta.store(timer::DEFAULT_DELTA, std::memory_order_relaxed);
			}
			else
			{
				m_delta.store(delta, std::memory_order_relaxed); 
			}

		}

		void incrementMessageCount() noexcept { ++m_messages_count; }

	private:

		std::atomic_int m_delta = timer::DEFAULT_DELTA;
		std::atomic_uint64_t m_messages_count = 0u;
	};

	struct CollectionClientMessageKind
	{
		std::vector<ClientMessageKind> msgs_kind;

		void convertClientsMessages(netw::ClientsMessages& msgs)
		{
			for (netw::ClientMessage& msg : msgs)
			{
				const MessageKind msg_kind = (MessageKind)msg.buffer[0];
				switch (msg_kind)
				{
				case MessageKind::CLIENT_REQUEST_MAX_CHAT_MSG_LEN:
				case MessageKind::CLIENT_REQUEST_CHAT_HISTORY:
				case MessageKind::CLIENT_REQUEST_CLIENTS_INFO:
				case MessageKind::CLIENT_CHAT_MSG:
					msg.buffer.erase(msg.buffer.begin());
					break;

				default: { LOG_INFO("Invalid message kind received: %", messageKindToString(msg_kind)) } continue;
				}

				msgs_kind.emplace_back(msg_kind, std::move(msg));
			}
		}
	};

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
	
	class MessagingPollRead : public netw::PollRead
	{
	public:
		MessagingPollRead(MessagingServer& server) : m_server(server) {}
		virtual ~MessagingPollRead() {}

		virtual netw::SBytes onPollRead(netw::ClientMessage& client_msg) override
		{
			char msg_type = 0;
			
			netw::SBytes result = netw::Recv(client_msg.socket_client, &msg_type, sizeof(char));
			if (result > 0)
			{
				const MessageKind msg_kind = MessageKind(msg_type);
				
				switch (msg_kind)
				{
				case MessageKind::CLIENT_REQUEST_MAX_CHAT_MSG_LEN:
				case MessageKind::CLIENT_REQUEST_CHAT_HISTORY:
				case MessageKind::CLIENT_REQUEST_CLIENTS_INFO:
					client_msg.buffer.push_back((char)msg_kind);
					break;

				case MessageKind::CLIENT_CHAT_MSG:
					if(netw::SBytes chat_msg_result = readClientChatMessage(client_msg); CHECK_RESULT(chat_msg_result))
					{
						return chat_msg_result;
					}
					client_msg.buffer.insert(client_msg.buffer.begin(), msg_type);
					break;

				// the client shouldn't be able to send and invalid message kind
				// this place should not be reachable
				default: 
					{ LOG_ERROR("Invalid message kind from %", client_msg.socket_client) } 
					return netw::errs::NET_SOCKET_ERROR;
				}
				client_msg.buffer.shrink_to_fit();
			}

			return result;
		}

	protected:

		MessagingServer& m_server;

		/*
		  TO DO: maybe this Recv should be non-blocking.
		  If a client sends a signal with this message kind,
		  the thread will block.
		*/
		netw::SBytes readClientChatMessage(netw::ClientMessage& client_msg);
	};

	class MessagingServer : public netw::TCPServer
	{
	public:
		MessagingServer(const char* port) : netw::TCPServer(getHints(), port), m_server_name("Server") 
		{
			setPollRead(std::make_unique<MessagingPollRead>(*this));
		}
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
		}

		virtual netw::ClientsMessages pollSockets(int timeout = 0) override
		{
			handleDeletionQueue();
			handlePendings();

			if (m_polls.empty())
			{
				const std::chrono::milliseconds TIMEOUT_MESSAGES(timeout);
				std::this_thread::sleep_for(TIMEOUT_MESSAGES);
				return netw::ClientsMessages();
			}
			else
			{
				return netw::TCPServer::pollSockets(timeout);
			}
		}

		virtual void removeClientPolling(netw::Socket_t client_socket, int error) override
		{
			deleteClient(client_socket);

			TCPServer::removeClientPolling(client_socket, error);
		}

		const std::string_view getServerName() const { return m_server_name; }

		void setDeltaTimer(int delta = -1)
		{
			if (m_timer == nullptr)
			{
				m_timer = std::make_unique<timer::TimerMessagesProcessed>();
				m_timeable_messages = std::make_shared<TimerMessagesCompleted>();

				m_timer->addTimeable(m_timeable_messages);
			}

			m_timer->setDelta(delta);
			m_timeable_messages->setDelta(delta);
		}

		void stopTimer()
		{
			m_timeable_messages.reset();
			m_timer.reset();
		}

	protected:

		void threadIncommingClients();

		void threadIncomingMessages();

		bool m_shutdown = false;
		bool m_should_listening_block = true;
		int m_listening_timeout = 1000;

		std::string m_server_name;

		std::jthread m_connection_thread;
		std::jthread m_messaging_thread;
		std::mutex m_mut;

		std::queue<PendingClient> m_pendings;
		std::queue<ID> m_delete_pendings;

		std::unordered_map<ID, MessagingClientServer> m_clients;

		MessageProcessor* m_msg_processor = nullptr;
		Broadcaster* m_broadcaster = nullptr;

		std::unique_ptr<timer::TimerMessagesProcessed> m_timer;
		std::shared_ptr<TimerMessagesCompleted> m_timeable_messages;

		static addrinfo getHints()
		{
			addrinfo hints = {};

			hints.ai_family = AF_INET;
			hints.ai_socktype = SOCK_STREAM;
			hints.ai_protocol = IPPROTO_TCP;
			hints.ai_flags = AI_PASSIVE;

			return hints;
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
		bool hasMessagesPending() 
		{
			return !m_pending_messages.empty();
		}

		void processMessage(CollectionClientMessageKind& msgs)
		{
			for (ClientMessageKind& msg : msgs.msgs_kind)
			{
				processMessageKind(msg);
			}

			const std::lock_guard<std::mutex> lock(m_mutex);
			while (!m_pending_messages.empty())
			{
				ClientMessageKind& msg = m_pending_messages.front();

				msgs.msgs_kind.push_back(std::move(msg));

				m_pending_messages.pop();
			}
		}

		void injectProcessedMessage(ClientMessageKind msg)
		{
			const std::lock_guard<std::mutex> lock(m_mutex);

			m_pending_messages.push(std::move(msg));
		}

	private:
						
		MessagingServer& m_server;
		std::string m_separator;

		std::mutex m_mutex;
		std::queue<ClientMessageKind> m_pending_messages;

	private:

		void processMessageKind(ClientMessageKind& msg)
		{
			switch (msg.msg_kind)
			{
			case MessageKind::CLIENT_REQUEST_MAX_CHAT_MSG_LEN:
				processRequestMaxChatMsgLen(msg.msg.buffer, m_server.getMaxChatLen());
				break;

			case MessageKind::CLIENT_REQUEST_CHAT_HISTORY:
				processRequestChatHistory();
				break;

			case MessageKind::CLIENT_REQUEST_CLIENTS_INFO:
			{
				ClientsInfo info = m_server.getClientsInfo();
				processRequestClientsInfo(msg.msg, info);
				break;
			}
			case MessageKind::CLIENT_CHAT_MSG:
			{
				const std::string& name = m_server.getClient((ID)msg.msg.socket_client)->getName();
				processClientChatMsg(msg.msg, name, m_separator);
				break;
			}
			default: { LOG_INFO("Can't process message of kind %", messageKindToString(msg.msg_kind)) }
			}
		}

		static void processClientChatMsg(netw::ClientMessage& client_msg, const std::string& client_name, std::string_view seperator)
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

		// TO DO: implement
		static void processRequestChatHistory() {}

	private:

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

		void broadcastMessages(CollectionClientMessageKind& msgs)
		{
			for (ClientMessageKind& msg : msgs.msgs_kind)
			{
				broadcastMessage(msg);
			}
		}

	private:

		MessagingServer& m_server;

		void broadcastMessage(ClientMessageKind& msg)
		{
			switch (msg.msg_kind)
			{
			case MessageKind::CLIENT_REQUEST_MAX_CHAT_MSG_LEN: broadcastRequestMaxChatMsgLen(msg); break;
			case MessageKind::CLIENT_REQUEST_CHAT_HISTORY: break;
			case MessageKind::CLIENT_REQUEST_CLIENTS_INFO: broadcastRequestClientsInfo(msg); break;

			case MessageKind::CLIENT_CHAT_MSG: 
			case MessageKind::SERVER_CHAT_MSG:
			case MessageKind::SERVER_CLIENT_JOINED:
			case MessageKind::SERVER_CLIENT_DISCONNECT:
				replicateMessageToClients(msg);
				break;
			default: { LOG_ERROR("Invalid message kind for broadcast: %", messageKindToString(msg.msg_kind)) }

			}
		}

		void broadcastRequestMaxChatMsgLen(ClientMessageKind& msg)
		{
			MessagingClientServer* client = m_server.getClient((ID)msg.msg.socket_client);
			if (client != nullptr)
			{
				addMessageHeader((char)MessageKind::CLIENT_REQUEST_MAX_CHAT_MSG_LEN, msg.msg.buffer);
				client->sendMessage(msg.msg.buffer);
			}
			else
			{
				{ LOG_ERROR("Client was null in broadcastRequestMaxChatMsgLen (somehow?).\nClient id: %", msg.msg.socket_client) }
			}
		}

		void broadcastRequestClientsInfo(ClientMessageKind& msg)
		{
			MessagingClientServer* client = m_server.getClient((ID)msg.msg.socket_client);
			if (client != nullptr)
			{
				addMessageHeader((char)MessageKind::CLIENT_REQUEST_CLIENTS_INFO, msg.msg.buffer);
				client->sendMessage(msg.msg.buffer);
			}
			else
			{
				{ LOG_ERROR("Client was null in broadcastRequestClientsInfo (somehow?).\nClient id: %", msg.msg.socket_client) }
			}
		}

		/*
		  Messages that should be send to all the clients, expect the socket in ClientMessage
		*/
		void replicateMessageToClients(ClientMessageKind& msg)
		{
			ClientsList& clients_list = m_server.getClientsList();
			addMessageHeader((char) msg.msg_kind, msg.msg.buffer);

			for (auto& client_pair : clients_list)
			{
				if ((ID)msg.msg.socket_client != client_pair.second.getID())
				{
					client_pair.second.sendMessage(msg.msg.buffer);
				}
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
					if (prot::Protocols::connection(server_client, nullptr) == prot::ProtocolStatus::SUCCESS)
					{
						{ LOG_INFO("User with ID % and name % accepted", server_client.getID(), server_client.getName()) }

						const ID id = server_client.getID();
						const std::string name = server_client.getName();

						addClient(id, std::move(server_client));
					}
					else
					{
						{ LOG_INFO("User with ID % and name % rejected", server_client.getID(), server_client.getName()) }
					}
				}
			}
			catch (const std::system_error& error)
			{
				LOG_ERROR(error.what())
			}
			catch (const std::exception& error)
			{
				LOG_ERROR(error.what())
			}
		}
		{ LOG_INFO("Thread incoming users exiting...") }
	}

	void MessagingServer::threadIncomingMessages()
	{
		while (!m_shutdown)
		{
			try
			{
				{ LOG_DEBUG("Waiting for messages...") }

				netw::ClientsMessages msgs = pollSockets(m_listening_timeout);
				const bool has_msgs = !msgs.empty();
				
				bool has_msgs_pendings = false;
				if (m_msg_processor != nullptr) has_msgs_pendings = m_msg_processor->hasMessagesPending();

				if (has_msgs == true || has_msgs_pendings == true)
				{
					// increment number request

					CollectionClientMessageKind msgs_kind;
					msgs_kind.convertClientsMessages(msgs);

					if (m_msg_processor != nullptr)
					{
						{ LOG_DEBUG("Processing messages") }
						m_msg_processor->processMessage(msgs_kind);
					}

					if (m_broadcaster != nullptr)
					{
						{ LOG_DEBUG("Sending messages") }
						m_broadcaster->broadcastMessages(msgs_kind);
					}

					if (m_timeable_messages != nullptr)
					{
						m_timeable_messages->incrementMessageCount();
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

		return buffer;
	}

	netw::SBuffer MessagingServer::createNewClientNotification(ID id, std::string_view name)
	{
		netw::bytes_t<ID>      id_bytes        = netw::serializeType(id);
		netw::bytes_t<uint8_t> name_size_bytes = netw::serializeType((uint8_t)name.size());
		netw::SBuffer          name_bytes      = netw::serializeBasicStr(name.data());

		netw::SBuffer client_joined_msg(id_bytes.size() + name_size_bytes.size() + name_bytes.size());

		std::copy(id_bytes.begin(),        id_bytes.end(),        client_joined_msg.begin());
		std::copy(name_size_bytes.begin(), name_size_bytes.end(), client_joined_msg.begin() + id_bytes.size());
		std::copy(name_bytes.begin(),      name_bytes.end(),      client_joined_msg.begin() + id_bytes.size() + name_size_bytes.size());

		return client_joined_msg;
	}

	netw::SBuffer MessagingServer::createClientDisconnectNotification(ID id)
	{
		netw::bytes_t<ID> id_bytes = netw::serializeType(id);

		netw::SBuffer client_disc_msg(id_bytes.size());
		std::copy(id_bytes.begin(), id_bytes.end(), client_disc_msg.begin());

		return client_disc_msg;
	}

	void MessagingServer::announceNewClient(const MessagingClientServer& client)
	{
		if (m_clients.empty())
		{
			return;
		}
	
		ClientMessageKind new_client_chat_msg;
		new_client_chat_msg.msg_kind = MessageKind::SERVER_CHAT_MSG;
		new_client_chat_msg.msg.socket_client = client.getSockHandle();
		new_client_chat_msg.msg.buffer = createNewClientJoinedChatMessage(client.getName());

		ClientMessageKind new_client_notification;
		new_client_notification.msg_kind = MessageKind::SERVER_CLIENT_JOINED;
		new_client_notification.msg.socket_client = client.getSockHandle();
		new_client_notification.msg.buffer = createNewClientNotification(client.getID(), client.getName());

		if (m_msg_processor != nullptr)
		{
			m_msg_processor->injectProcessedMessage(std::move(new_client_chat_msg));
			m_msg_processor->injectProcessedMessage(std::move(new_client_notification));
		}
	}

	void MessagingServer::announceLeavingClient(const MessagingClientServer& client)
	{
		if (m_clients.empty())
		{
			return;
		}
		
		ClientMessageKind disconnected_client_chat_msg;
		disconnected_client_chat_msg.msg_kind = MessageKind::SERVER_CHAT_MSG;
		disconnected_client_chat_msg.msg.socket_client = client.getSockHandle();
		disconnected_client_chat_msg.msg.buffer = createClientDisconnectChatMessage(client.getName());

		ClientMessageKind disconnected_client_notification;
		disconnected_client_notification.msg.socket_client = client.getSockHandle();
		disconnected_client_notification.msg_kind = MessageKind::SERVER_CLIENT_DISCONNECT;
		disconnected_client_notification.msg.buffer = createClientDisconnectNotification(client.getID());

		if (m_msg_processor != nullptr)
		{
			m_msg_processor->injectProcessedMessage(std::move(disconnected_client_chat_msg));
			m_msg_processor->injectProcessedMessage(std::move(disconnected_client_notification));
		}
	}

	netw::SBytes MessagingPollRead::readClientChatMessage(netw::ClientMessage& client_msg)
	{
		netw::SBytes result = 0;

		netw::bytes_t<MsgSize> msg_len_bytes;
		if (netw::SBytes len_bytes_result = netw::Recv(client_msg.socket_client, msg_len_bytes.data(), msg_len_bytes.size()); CHECK_RESULT(len_bytes_result))
		{
			return len_bytes_result;
		}
		else
		{
			result += len_bytes_result;
		}
		const MsgSize msg_len = netw::deserializeType<MsgSize>(msg_len_bytes);
		if (msg_len > m_server.getMaxChatLen())
		{
			// remove client if size is too big
			return netw::errs::NET_SOCKET_ERROR;
		}

		netw::SBuffer msg_buffer(msg_len);
		if (netw::SBytes buffer_result = netw::Recv(client_msg.socket_client, msg_buffer.data(), msg_buffer.size()); CHECK_RESULT(buffer_result))
		{
			return buffer_result;
		}
		else
		{
			result += buffer_result;
		}

		client_msg.buffer = std::move(msg_buffer);

		return result;
	}

};


