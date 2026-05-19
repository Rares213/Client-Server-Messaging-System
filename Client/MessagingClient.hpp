#pragma once

#include "../Commons/MessagingAppComm.hpp"
#include "../Commons/GUI.hpp"
#include "../Commons/logger.hpp"
#include "../Commons/Protocols.hpp"

#include <chrono>
#include <cinttypes>
#include <variant>
#include <limits>

namespace msgapp
{
	class ErrorAcceptable;
	class MessageAcceptable;
	class Screenable;
	class Unconnected;
	class Connected;
	class Screenable;
	class MessagingClientSide;
	class MessagingClientGUI;
	struct ServerMessageKind;

	enum class ServerStatus { TIMEOUT, CONNECTED, NOT_CONNECTED };
	using MessagesStored = std::vector<std::string>;
	using ServerMessageResult = std::expected<ServerMessageKind, ServerStatus>;

	class Screenable
	{
	public:

		virtual ~Screenable() {}

		virtual void showScreen() = 0;
		virtual void passError(const std::string& error) = 0;
	};

	// Interface to implement in order to receive error messages.
	class ErrorAcceptable
	{
	public:
		virtual ~ErrorAcceptable() {}

		virtual void receiveError(const std::string& error, int code) = 0;
	};

	struct ServerMessageKind
	{
		MessageKind kind;
		netw::SBuffer message;
	};

	// Interface to implement to receive messages from server.
	class MessageAcceptable
	{
	public:
		virtual ~MessageAcceptable() {}

		virtual void receiveMessage(ServerMessageKind message) = 0;
	};

	class ClientUser : public MessagingClient
	{
	public:
		ClientUser() {}
		ClientUser(netw::Client client) : MessagingClient(Authority::CLIENT, std::move(client)) {}
		virtual ~ClientUser() {}

		ClientUser(ClientUser&& other) noexcept : MessagingClient(std::move(other)) {}

		void operator=(ClientUser&& other) noexcept
		{
			MessagingClient::operator=(std::move(other));
		}

		// if any of the network operations fail, will throw system_error
		ServerMessageResult getServerMessage()
		{
			MessageKind msg_kind = MessageKind::INVALID;
			if (netw::SBytes result = readMessageKind(msg_kind); result == netw::errs::NET_SOCKET_ERROR)
			{
				int error = netw::lastError();
				if (netw::isNonBlockingError(error) == true)
				{
					return std::unexpected(ServerStatus::TIMEOUT);
				}
				else
				{
					SYSTEM_ERROR_NUM(error, "Connection error reading message kind")
				}
			}

			ServerMessageKind server_message;
			server_message.kind = msg_kind;

			switch (msg_kind)
			{
			case MessageKind::REJECT:
			case MessageKind::CLIENT_CHAT_MSG:
			case MessageKind::CLIENT_REQUEST_MAX_CHAT_MSG_LEN:
			case MessageKind::CLIENT_REQUEST_CLIENTS_INFO:
			case MessageKind::SERVER_CHAT_MSG:
			case MessageKind::SERVER_CLIENT_JOINED:
			case MessageKind::SERVER_CLIENT_DISCONNECT:
			{
				const MsgSize buffer_size = readMessageSize();
				if (buffer_size != 0)
				{
					server_message.message = readMessage(buffer_size);
				}
				return server_message;
			}
			default:
			{ LOG_ERROR("Messaage kind not supported: %", messageKindToString(msg_kind)) };

			server_message.kind = MessageKind::INVALID;
			return server_message;
			}
		}

		/*
		  Sends a chat message to the server.
		  If any of the network operations fail, will throw system_error.
		*/
		void sendChatMessage(const std::vector<char>& msg)
		{
			{ LOG_DEBUG("Send message to server: %", std::string(msg.data(), msg.size())) }

			netw::SBuffer buffer;
			buffer.reserve(msg.size());

			for (size_t i = 0; i < msg.size(); ++i)
			{
				if (msg[i] == '\0')
				{
					break;
				}

				buffer.push_back(msg[i]);
			}
			buffer.shrink_to_fit();
			addMessageHeader((char)MessageKind::CLIENT_CHAT_MSG, buffer);

			if (netw::SBytes result = sendMessage(buffer); CHECK_RESULT(result))
			{
				SYSTEM_ERROR("Failed to send message")
			}
		}


	private:

		MsgSize readMessageSize()
		{
			netw::bytes_t<MsgSize> msg_size_bytes;
			if (netw::SBytes result = receiveMessageRaw(msg_size_bytes.data(), msg_size_bytes.size()); CHECK_RESULT(result))
			{
				SYSTEM_ERROR("Failed to read length of chat message");
			}
			const MsgSize msg_size = netw::deserializeType<MsgSize>(msg_size_bytes);

			return msg_size;
		}

		netw::SBuffer readMessage(MsgSize buffer_size)
		{
			netw::SBuffer buffer(buffer_size);

			if (netw::SBytes result = receiveMessage(buffer); CHECK_RESULT(result))
			{
				SYSTEM_ERROR("Failed to read chat message")
			}

			return buffer;
		}

	};

	class Unconnected : public Screenable
	{
	public:

		Unconnected() {}

		virtual void showScreen() override;

		virtual void passError(const std::string& error) override
		{
			m_errors.push_back(error);
		}

		void setGui(MessagingClientGUI* gui) { m_gui = gui; }

		std::string getName() const 
		{ 
			std::string name;
			for (size_t i = 0; i < m_name.size(); ++i)
			{
				if (m_name[i] == '\0') break;

				name.push_back(m_name[i]);
			}
			name.shrink_to_fit();

			return name; 
		}
		std::string getIP() const { return std::string(m_ip.data(), m_ip.size()); }

	private:

		MessagingClientGUI* m_gui = nullptr;

		static constexpr std::string_view INVALID_NAME = "Invalid name";
		static constexpr std::string_view INVALID_IP = "Invalid ip";

		std::array<char, NAME_LEN> m_name{ '\0' };
		std::array<char, netw::IPV4_LEN + 1u> m_ip{ '\0' };
		std::vector<std::string> m_errors;

		bool m_show_error = false;

		void checkName()
		{
			bool isOnlyWhiteSpc = isNameOnlyWhiteSpc();
			bool isEmpty = isNameEmpty();

			if (isOnlyWhiteSpc == true || isEmpty == true)
			{		
				m_errors.push_back(std::string(INVALID_NAME.data(), INVALID_NAME.size()));
			}
		}

		void checkIP()
		{
			if (isIpEmpty() == true )
			{
				m_errors.push_back(std::string(INVALID_IP.data(), INVALID_IP.size()));
			}
		}

		bool isNameOnlyWhiteSpc()
		{
			for (size_t i = 0; i < m_name.size(); ++i)
			{
				if (m_name[i] != ' ')
				{
					return false;
				}
			}

			return true;
		}

		bool isNameEmpty()
		{
			if (m_name[0] == '\0')
			{
				return true;
			}
			else
			{
				return false;
			}
		}
	
		bool isIpEmpty()
		{
			if (m_ip[0] == '\0')
			{
				return true;
			}
			else
			{
				return false;
			}
		}

	};

	class Connected : public Screenable
	{
	public:
		Connected() 
		{
			m_msg = std::vector<char>(m_max_chat_len);
		}
		virtual ~Connected() {}

		void setGui(MessagingClientGUI* gui) { m_gui = gui; }

		void setName(std::string_view name) { m_my_name = name; }

		virtual void showScreen() override
		{
			m_win_size = gui::GUI::getScreenSize();

			showChat();

			showUsers();

			showSettings();
		}

		virtual void passError(const std::string& error) override
		{
			m_errors.push_back(error);
		}

		void pushChatMessage(std::string message)
		{
			if (m_chat_msgs.size() < m_max_chat_msgs)
			{
				m_chat_msgs.push_back(std::move(message));
			}
			else
			{
				m_chat_msgs.erase(m_chat_msgs.begin());
				m_chat_msgs.push_back(std::move(message));
			}
		}

		void setClientsInfo(ClientsInfo clients_info) { m_clients_info = std::move(clients_info); }

		void removeClientInfo(ID client_id) { m_clients_info.removeClientInfo(client_id); }

		void addClientInfo(ID id, const std::string& name) { m_clients_info.addClientInfo(id, name); }

		void setMaxMsgChatLen(uint32_t max_len) 
		{ 
			m_max_chat_len = max_len;
			m_msg = std::vector<char>(m_max_chat_len);
		}

	private:

		MessagingClientGUI* m_gui = nullptr;

		std::vector<std::string> m_chat_msgs;
		int m_max_chat_msgs = DEFAULT_CHAT_MAX_MSGS;

		std::vector<char> m_msg;
		uint32_t m_max_chat_len = DEFAULT_MAX_CHAT_MSG_LEN;

		std::string m_my_name;

		ClientsInfo m_clients_info;

		float scroll_to_pos_px = 0.0f;

		gui::WinSize m_win_size;

		bool m_auto_scroll = false;

		std::vector<std::string> m_errors;

		static constexpr int DEFAULT_CHAT_MAX_MSGS = 128;
		static constexpr uint32_t DEFAULT_MAX_CHAT_MSG_LEN = 512u;
		static constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBackground;

	private:

		void showChatMembers()
		{
			ImGui::Text("%s (You)", m_my_name.c_str());

			for (const ClientInfo& info : m_clients_info.getClientsList())
			{
				ImGui::PushTextWrapPos((float)m_win_size.first / 2.0f);
				ImGui::Text("ID: %" PRIu64 " Name: %s", info.id, info.name.c_str());
			}
		}

		void showChat()
		{
			ImGui::SetNextWindowSize(ImVec2{ (float)m_win_size.first / 2.0f, (float)m_win_size.second - (float)m_win_size.second / 12.0f });
			ImGui::SetNextWindowPos(ImVec2{ (float)m_win_size.first / 4.0f, 0.0f });

			ImGui::Begin("Chat room", nullptr, flags);
			ImGui::PushID("##VerticalChat");

			// show chat messages
			for (size_t i = 0; i < m_chat_msgs.size(); ++i)
			{
				ImGui::PushTextWrapPos((float)m_win_size.first / 2.0f);
				ImGui::Text("%s", m_chat_msgs[i].c_str());
				ImGui::Text(" ");
				if (m_auto_scroll == true && i == (m_chat_msgs.size() - 1) )
				{
					ImGui::SetScrollHereY(1.0f);
				}
			}

			ImGui::PopID();
			ImGui::End();
			
			//input msg box
			ImGui::SetNextWindowSize(ImVec2{ (float)m_win_size.first / 2.0f, (float)m_win_size.second / 14.0f });
			ImGui::SetNextWindowPos(ImVec2{ (float)m_win_size.first / 4.0f,  (float)m_win_size.second - 50.0f });
			ImGui::Begin("MessageInputBox", nullptr, flags);

			ImGui::PushItemWidth((float)m_win_size.first / 2.5f);
			if (ImGui::InputText("##MessageUserBox", m_msg.data(), m_msg.size(), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_EscapeClearsAll))
			{
				{ LOG_INFO("Sending user message") }
				sendChatMessage();
			}
			const bool isMsgBoxActive = ImGui::IsItemActive();
			ImGui::PopItemWidth();
			ImGui::SameLine();
			if (ImGui::Button("Send") == true)
			{
				sendChatMessage();
			}

			ImGui::End();

			//underlay for msg box
			if (isMsgBoxActive == false && m_msg[0] == '\0')
			{
				ImGui::SetNextWindowSize(ImVec2{ (float)m_win_size.first / 2.0f, (float)m_win_size.second / 14.0f });
				ImGui::SetNextWindowPos(ImVec2{ (float)m_win_size.first / 4.0f,  (float)m_win_size.second - 71.0f });
				ImGui::Begin("MessageInputBox", nullptr);

				ImGui::Text("Message");

				ImGui::End();
			}
		}

		void showUsers()
		{
			ImGui::SetNextWindowSize(ImVec2{ (float)m_win_size.first / 4.0f, (float)m_win_size.second });
			ImGui::SetNextWindowPos(ImVec2{ 0.0f, 0.0f });
			ImGui::Begin("Users", nullptr, flags);
			ImGui::PushID("##VerticalUsers");

			showChatMembers();

			ImGui::PopID();
			ImGui::End();
		}

		void showSettings();

		void sendChatMessage();

		void clearUserMsg()
		{
			for (size_t i = 0; i < m_msg.size(); ++i)
			{
				if (m_msg[i] == '\0')
				{
					break;
				}

				m_msg[i] = '\0';
			}
		}

	};

	class MessagingClientSide
	{
	public:

		ServerStatus connectMessagingServer(std::string_view name, std::string_view ip, bool should_block = false) 
		{
			try
			{
				addrinfo hints = getHints();
				ClientUser user(netw::connectToServer(&hints, ip, msgapp::PORT));
				user.setName(name.data());

				prot::ProtocolStatus status = prot::Protocols::connection(user);
				if (status == prot::ProtocolStatus::SUCCESS)
				{
					m_user = std::move(user);
					m_user.setBlocking(should_block);

					m_server_status = ServerStatus::CONNECTED;

					sendRequest(MessageKind::CLIENT_REQUEST_MAX_CHAT_MSG_LEN);

					m_shutdown = false;

					m_listening_server_thread = std::jthread(&MessagingClientSide::listenServerMessages, this);
				}
			}
			catch (const std::system_error& error)
			{
				{ LOG_ERROR(error.what()) }

				if (m_error_passer != nullptr)
				{
					m_error_passer->receiveError(error.what(), error.code().value());
				}

				return ServerStatus::NOT_CONNECTED;
			}

			return m_server_status;
		}

		// WARNING: only one may be added for now
		void addErrorAcceptable(ErrorAcceptable& error_passer) { m_error_passer = &error_passer; }

		// WARNING: only one may be added for now
		void addMessageAcceptable(MessageAcceptable& msg_acceptable) { m_msg_acceptable = &msg_acceptable; }

		// >= 0 will be set to specified amount, else to default
		void setTimeoutServerListening(int timeout)
		{
			if (timeout >= 0)
			{
				m_timeout = timeout;
			}
			else
			{
				timeout = DEFAULT_TIMEOUT_SERVER_LISTENING;
			}
		}

		void sendRequest(MessageKind request)
		{
			if (m_server_status == ServerStatus::NOT_CONNECTED)
			{
				sendErrorListeners("Client not connected to a server", -100);
			}
			
			switch (request)
			{
			case MessageKind::CLIENT_REQUEST_CHAT_HISTORY:
			case MessageKind::CLIENT_REQUEST_CLIENTS_INFO:
			case MessageKind::CLIENT_REQUEST_MAX_CHAT_MSG_LEN:
				m_user.sendSignal((char)request);
				break;

			default:
			{
				std::string error = std::string("Invalid request: ") + std::string(messageKindToString(request));

				{ LOG_ERROR(error) };
				sendErrorListeners(error, -100);
			}
			}
		}

		void sendChatMessage(const std::vector<char>& message)
		{
			if (m_server_status == ServerStatus::NOT_CONNECTED)
			{
				sendErrorListeners("Client not connected to a server", -100);
			}
			
			if (message.size() > m_max_msg)
			{
				sendErrorListeners("Message too long !", -100);
				return;
			}

			try
			{
				m_user.sendChatMessage(message);
			}
			catch (const std::system_error& error)
			{
				{ LOG_ERROR(error.what()) }
				sendErrorListeners(error.what(), error.code().value());
			}
			catch (const std::exception& error)
			{
				{ LOG_ERROR(error.what()) }
				sendErrorListeners(error.what(), -100);
			}
		}

		void signalShutdown()
		{
			m_shutdown = true;
		}

	private:

		void listenServerMessages()
		{
			while (!m_shutdown)
			{
				try
				{
					ServerMessageResult msg = m_user.getServerMessage();
					if (msg.has_value())
					{
						{ LOG_INFO("Received message kind of %", messageKindToString(msg.value().kind)) }

						ServerMessageKind server_message = std::move(msg.value());

						if (server_message.kind == MessageKind::CLIENT_REQUEST_MAX_CHAT_MSG_LEN)
						{
							updateMaxChatMsg(server_message.message);
						}

						sendMessageListeners(server_message);
					}
					else
					{
						const std::chrono::milliseconds timeout(m_timeout);

						std::this_thread::sleep_for(timeout);
					}
				}
				catch (const std::system_error& error)
				{
					{ LOG_ERROR(error.what()) }

					const int error_code = error.code().value();
					sendErrorListeners(error.what(), error_code);

					if (error_code == netw::errs::NET_CONN_DOWN || error_code == netw::errs::NET_SOCKET_ERROR 
						|| error_code == netw::errs::NET_INVALID_SOCKET || error_code == netw::errs::NET_CONN_RESET)
					{
						signalShutdown();
					}
				}
			}
			{ LOG_INFO("Listening thread exiting...") }
			m_user.closeClient();
		}

	private:

		ClientUser m_user;
		ErrorAcceptable* m_error_passer = nullptr;
		MessageAcceptable* m_msg_acceptable = nullptr;

		std::jthread m_listening_server_thread;
		
		ServerStatus m_server_status = ServerStatus::NOT_CONNECTED;
		uint32_t m_max_msg = DEFAULT_MAX_CHAT_MSG_LEN;
		int m_timeout = DEFAULT_TIMEOUT_SERVER_LISTENING;

		volatile bool m_shutdown = false;

	private:

		static constexpr int DEFAULT_TIMEOUT_SERVER_LISTENING = 1000;
		static constexpr uint32_t DEFAULT_MAX_CHAT_MSG_LEN = 512u;

		static addrinfo getHints()
		{
			addrinfo hints{};

			hints.ai_family = AF_INET;
			hints.ai_socktype = SOCK_STREAM;
			hints.ai_protocol = IPPROTO_TCP;

			return hints;
		}

		void updateMaxChatMsg(const netw::SBuffer& buffer)
		{
			if (buffer.size() != sizeof(uint32_t))
			{
				LOG_ERROR("Expected 4 bytes for max chat msg, but buffer has %", buffer.size())
			}
			else
			{
				m_max_msg = netw::deserializeType<uint32_t>(buffer.data());
			}
		}

		void sendMessageListeners(ServerMessageKind& server_message)
		{
			if (m_msg_acceptable != nullptr)
			{
				m_msg_acceptable->receiveMessage(std::move(server_message));
			}
		}

		void sendErrorListeners(const std::string& error, int code)
		{
			if (m_error_passer != nullptr)
			{
				m_error_passer->receiveError(error, code);
			}
		}
	
	};

	class MessagingClientGUI : public MessageAcceptable, public ErrorAcceptable
	{
	public:
		MessagingClientGUI()
		{
			m_unconnected_screen.setGui(this);
			m_connected_screen.setGui(this);

			m_current_screen = &m_unconnected_screen;
		}
		virtual ~MessagingClientGUI() {}

		virtual void receiveMessage(ServerMessageKind message) override
		{
			const std::lock_guard<std::mutex> lock(m_mutex);

			m_queue_msgs.push(std::move(message));
		}

		virtual void receiveError(const std::string& error, int code) override
		{
			const std::lock_guard<std::mutex> lock(m_mutex);

			m_queue_error.push({ error, code });
		}

		void setMessagingClient(MessagingClientSide& msg_user) 
		{ 
			m_messaging_user = &msg_user; 

			msg_user.addErrorAcceptable(*this);
			msg_user.addMessageAcceptable(*this);
		}

		void runUI()
		{
			while(!gui::GUI::shouldClose())
			{
				handleMsgsQueue();
				handleErrorMsgsQueue();

				gui::GUI::pollEvents();

				initImGuiFrame();

				ImGui::ShowDemoWindow();

				if(m_current_screen != nullptr)
				{
					m_current_screen->showScreen();
				}

				renderImGuiFrame();
			}

			signalDisconnect();
		}

		void connectToMessagingServer(std::string_view name, std::string_view ip)
		{
			if (m_messaging_user != nullptr)
			{
				const ServerStatus status = m_messaging_user->connectMessagingServer(name, ip);
				if (status == ServerStatus::CONNECTED)
				{
					m_messaging_user->sendRequest(MessageKind::CLIENT_REQUEST_CLIENTS_INFO);
					m_current_screen = &m_connected_screen;
					m_connected_screen.setName(name);
				}

			}
		}

		void sendChatMessage(const std::vector<char>& message)
		{
			if (m_messaging_user != nullptr)
			{
				m_messaging_user->sendChatMessage(message);
			}
		}

		void signalDisconnect() 
		{
			if (m_messaging_user != nullptr)
			{
				m_messaging_user->signalShutdown();
			}

			m_current_screen = &m_unconnected_screen;
		}

	private:
		struct Error
		{
			std::string error;
			int code;
		};

	private:

		MessagingClientSide* m_messaging_user = nullptr;

		Unconnected m_unconnected_screen;
		Connected m_connected_screen;

		Screenable* m_current_screen = nullptr;

		ServerStatus m_server_status = ServerStatus::NOT_CONNECTED;

		std::mutex m_mutex;
		std::queue<ServerMessageKind> m_queue_msgs;
		std::queue<Error> m_queue_error;

	private:

		void initImGuiFrame()
		{
			ImGui_ImplOpenGL3_NewFrame();
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();
		}

		void renderImGuiFrame()
		{
			ImGui::Render();
			gui::GUI::clearScreen();
			ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
			gui::GUI::updateScreen();
		}

		void handleMsgsQueue()
		{
			const std::lock_guard<std::mutex> lock(m_mutex);

			while (!m_queue_msgs.empty())
			{
				ServerMessageKind server_msg = std::move(m_queue_msgs.front());
				{ LOG_INFO("Processing message for connected screen: %", messageKindToString(server_msg.kind)) }

				switch (server_msg.kind)
				{
				case MessageKind::CLIENT_CHAT_MSG:
				case MessageKind::SERVER_CHAT_MSG:
				{
					std::string chat_msg = copySBufferToStr(server_msg.message);
					m_connected_screen.pushChatMessage(std::move(chat_msg));
					break;
				}
				case MessageKind::SERVER_CLIENT_DISCONNECT:
				{
					const ID id = netw::deserializeType<ID>(server_msg.message.data());
					m_connected_screen.removeClientInfo(id);
					break;
				}
				case MessageKind::SERVER_CLIENT_JOINED:
				{
					const ID id = netw::deserializeType<ID>(server_msg.message.data());

					uint8_t name_size = netw::deserializeType<uint8_t>(server_msg.message.data() + sizeof(ID));

					netw::SBuffer name_buffer(name_size);
					std::copy(server_msg.message.begin() + sizeof(ID) + sizeof(uint8_t), server_msg.message.end(), name_buffer.begin());

					const std::string name = copySBufferToStr(name_buffer);

					m_connected_screen.addClientInfo(id, name);
					break;
				}
				case MessageKind::CLIENT_REQUEST_CLIENTS_INFO:
				{
					ClientsInfo info;
					info.deserialize(server_msg.message);

					m_connected_screen.setClientsInfo(std::move(info));
					break;
				}
				case MessageKind::CLIENT_REQUEST_MAX_CHAT_MSG_LEN:
				{
					const uint32_t max_len = netw::deserializeType<uint32_t>(server_msg.message.data());
					m_connected_screen.setMaxMsgChatLen(max_len);
					break;
				}
				default:
				{ LOG_ERROR("Invalid message kind for connected screen: %", messageKindToString(server_msg.kind)) }
				}

				m_queue_msgs.pop();
			}
		}

		void handleErrorMsgsQueue()
		{
			const std::lock_guard<std::mutex> lock(m_mutex);

			while (!m_queue_error.empty())
			{
				Error error = std::move(m_queue_error.front());

				m_current_screen->passError(error.error);

				m_queue_error.pop();
			}
		}
	};

	void Unconnected::showScreen()
	{
		static ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBackground;
		gui::WinSize win_size = gui::GUI::getScreenSize();
		ImGui::SetNextWindowSize(ImVec2{ (float)win_size.first / 4.0f, (float)win_size.second });
		ImGui::SetNextWindowPos(ImVec2{ (float)win_size.first / 4.0f + (float)win_size.first / 8.0f, (float)win_size.second / 3.0f });
		ImGui::SetNextWindowContentSize(ImVec2{ 50, 50 });

		ImGui::Begin("Establish connection", nullptr, flags);
		ImGui::PushItemWidth((float)win_size.first / 4.0f - 17.0f);

		ImGui::Text("Name");
		ImGui::PushID(1);
		ImGui::InputText("", m_name.data(), m_name.size());
		ImGui::PopID();

		ImGui::Dummy(ImVec2{ 0.0f, 30.0f });

		ImGui::PushID(2);
		ImGui::Text("IP");
		ImGui::InputText("", m_ip.data(), m_ip.size(), ImGuiInputTextFlags_CharsDecimal);
		ImGui::PopID();
		ImGui::PopItemWidth();

		ImGui::Dummy(ImVec2{ 0.0f, 30.0f });

		if (ImGui::Button("Connect") == true)
		{
			{ LOG_DEBUG(std::string(m_name.data(), m_name.size())) }
			{ LOG_DEBUG(std::string(m_ip.data(), m_ip.size())) }	

			m_errors.clear();
			checkName();
			checkIP();

			if (m_errors.empty())
			{
				if (m_gui != nullptr)
				{
					std::string name(getName());
					std::string ip(getIP());

					m_gui->connectToMessagingServer(name, ip);
				}
			}
		}

		if (!m_errors.empty())
		{
			for (const std::string& error : m_errors)
			{
				ImGui::Dummy(ImVec2{ 0.0f, 5.0f });
				ImGui::PushTextWrapPos((float)win_size.first / 4.0f);
				ImGui::TextColored(ImVec4{ 1.0f, 0.0f, 0.0f, 1.0f }, error.c_str());
			}
		}

		ImGui::End();
	}

	void Connected::showSettings()
	{
		ImGui::SetNextWindowSize(ImVec2{ (float)m_win_size.first / 4.0f, (float)m_win_size.second });
		ImGui::SetNextWindowPos(ImVec2{ (float)m_win_size.first - (float)m_win_size.first * 0.25f,  0.0f });
		ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBackground);

		if (ImGui::Button("Disconnect") == true)
		{
			if (m_gui != nullptr)
			{
				m_gui->signalDisconnect();

				m_chat_msgs.clear();
				m_clients_info.getClientsList().clear();
				m_errors.clear();
			}
		}

		ImGui::Text("Messages %d", m_chat_msgs.size());

		if (ImGui::Button("Clear chat") == true)
		{
			m_chat_msgs.clear();
		}

		ImGui::Checkbox("Auto scroll", &m_auto_scroll);

		if (!m_errors.empty())
		{
			if (ImGui::Button("Clear errors") == true)
			{
				m_errors.clear();
			}

			for (const std::string& error_msg : m_errors)
			{
				ImGui::PushTextWrapPos((float)m_win_size.first / 4.0f);
				ImGui::TextColored(ImVec4{ 1.0f, 0.0f, 0.0f, 1.0f }, error_msg.c_str());
			}
		}

		ImGui::End();
	}

	void Connected::sendChatMessage()
	{
		if (m_msg[0] == '\0')
		{
			return;
		}

		if (m_gui != nullptr)
		{
			m_gui->sendChatMessage(m_msg);
		}

		std::string you_msg("You: " + copySBufferToStr(m_msg));
		pushChatMessage(std::move(you_msg));

		clearUserMsg();
	}


}