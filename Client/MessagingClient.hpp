#pragma once

#include "../Commons/MessagingAppComm.hpp"
#include "../Commons/GUI.hpp"
#include "../Commons/logger.hpp"
#include "../Commons/Protocols.hpp"

#include <chrono>
#include <cinttypes>
#include <variant>

namespace msgapp
{
	class ClientUser : public MessagingClient
	{
	public:
		ClientUser() {}
		ClientUser(netw::Client client) : MessagingClient(Authority::CLIENT, std::move(client)) {}
		virtual ~ClientUser() {}

		ClientUser(ClientUser&& other) noexcept : MessagingClient(std::move(other))
		{

		}

		/*
		  Sends a chat message to the server.
		  If any of the network operations fail, will throw system_error.
		*/
		void sendChatMessage(const std::vector<char>& msg)
		{
			{ LOG_INFO("Send message to server: %", std::string(msg.data(), msg.size())) }

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

		std::string readChatMsg(MsgSize buffer_size)
		{
			netw::SBuffer msg_buffer(buffer_size);

			receiveMessage(msg_buffer);

			std::string msg;
			copySBufferToStr(msg_buffer, msg);

			return msg;
		}

		ClientInfo readNewClientInfo()
		{
			netw::bytes_t<ID> id_bytes;
			if (netw::SBytes result = receiveMessageRaw(id_bytes.data(), sizeof(ID)); CHECK_RESULT(result))
			{
				SYSTEM_ERROR("Failed to read new client info")
			}

			ID id = netw::deserializeType<ID>(id_bytes);

			netw::bytes_t<uint8_t> name_size_bytes;
			if (netw::SBytes result = receiveMessageRaw(name_size_bytes.data(), name_size_bytes.size()); CHECK_RESULT(result))
			{
				SYSTEM_ERROR("Failed to read new client info")
			}

			uint8_t name_size = netw::deserializeType<uint8_t>(name_size_bytes);

			netw::SBuffer name_bytes(name_size);
			if (netw::SBytes result = receiveMessage(name_bytes); CHECK_RESULT(result))
			{
				SYSTEM_ERROR("Failed to read new client info")
			}

			std::string name(name_bytes.data(), name_bytes.size());

			ClientInfo info{ id, std::move(name) };
			return info;
		}

		ID readClientDisconnect()
		{
			netw::bytes_t<ID> id_bytes;
			if (netw::SBytes result = receiveMessageRaw(id_bytes.data(), sizeof(ID)); CHECK_RESULT(result))
			{
				SYSTEM_ERROR("Failed to read new client info")
			}

			ID id = netw::deserializeType<ID>(id_bytes);
			return id;
		}

	private:

	};
	
	addrinfo getHints()
	{
		addrinfo hints{};

		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;

		return hints;
	}

	std::expected<ClientUser, MessageKind> connectToMessagingServer(std::string_view name, std::string_view ip)
	{
		addrinfo hints = getHints();

		ClientUser user(netw::connectToServer(&hints, ip, msgapp::PORT));
		user.setName(name.data());

		prot::ProtocolStatus status = prot::Protocols::connection(user);
		if (status == prot::ProtocolStatus::SUCCESS)
		{
			return user;
		}
		else
		{
			return std::unexpected(MessageKind::REJECT);
		}
	}
	
	enum class STATE
	{
		NONE, TRANSITION_SUCCESS, TRASNSITION_FAILED, TRANSITION_UNC_TO_CONN, TRANSITION_CONN_TO_UNC
	};
	
	class Unconnected;
	class Connected;
	class Disconnected;

	class IState
	{
	public:

		virtual ~IState() {}

		virtual STATE runState() = 0;
		virtual void passError(const std::string& error) = 0;
	};

	class Unconnected : public IState
	{
	public:

		Unconnected() {}

		STATE runState()
		{
			m_state = STATE::NONE;
			
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

			bool should_transition = false;
			if (should_transition = ImGui::Button("Connect"); should_transition == true)
			{
				{
					LOG_DEBUG(std::string(m_name.data(), m_name.size()))
					LOG_DEBUG(std::string(m_ip.data(), m_ip.size()))
				}

				m_errors.clear();
				checkName();
				checkIP();
			}

			if (!m_errors.empty())
			{
				for(const std::string& error : m_errors)
				{
					ImGui::Dummy(ImVec2{ 0.0f, 5.0f });
					ImGui::PushTextWrapPos((float)win_size.first / 4.0f);
					ImGui::TextColored(ImVec4{ 1.0f, 0.0f, 0.0f, 1.0f }, error.c_str());
				}
			}

			ImGui::End();

			if (should_transition == true)
			{
				size_t size = m_errors.size();
				if (size == 0)
				{
					m_state = STATE::TRANSITION_UNC_TO_CONN;
				}
			}

			return m_state;
		}

		virtual void passError(const std::string& error) override
		{
			m_errors.push_back(error);
		}

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

		static constexpr std::string_view INVALID_NAME = "Invalid name";
		static constexpr std::string_view INVALID_IP = "Invalid ip";

		std::array<char, NAME_LEN> m_name{ '\0' };
		std::array<char, netw::IPV4_LEN + 1u> m_ip{ '\0' };
		std::vector<std::string> m_errors;

		STATE m_state = STATE::NONE;

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

	class Connected : public IState
	{
	public:
		Connected() {}
		Connected(ClientUser user) : m_user(std::move(user))
		{
			std::expected<uint32_t, prot::ProtocolStatus> result = prot::Protocols::requestMaximumChatMessageLength(m_user);
			if (result.has_value())
			{
				{ LOG_INFO("Chat message max length is %", (int)result.value()) }
				m_msg = std::vector<char>(result.value(), '\0');
			}
			else
			{
				{ LOG_ERROR("Failed to get chat message max length. Status: %", (int)result.error()) }
			}

			try
			{
				std::expected<ClientsInfo, prot::ProtocolStatus> result = prot::Protocols::requestClientsInfo(m_user);
				if (result.has_value())
				{
					{ LOG_INFO("Received clients info") }
					m_clients_info = std::move(result.value());
				}
				else
				{
					{ LOG_ERROR("Failed to get list clients. Status: %", (int)result.error()) }
				}
			}
			catch (const std::runtime_error& error)
			{
				{ LOG_ERROR(error.what()) }
			}

			m_user.setBlocking(false);

		}
		virtual ~Connected() {}

		virtual STATE runState() override
		{
			m_win_size = gui::GUI::getScreenSize();

			showChat();

			showUsers();

			showSettings();

			getMessageServer();

			return m_state;
		}

		virtual void passError(const std::string& error) override
		{

		}

	private:

		ClientUser m_user;

		std::vector<char> m_msg;
		std::vector<std::string> m_chat_msgs;
		ClientsInfo m_clients_info;

		STATE m_state = STATE::NONE;

		int32_t m_max_chat_len = 0;
		float scroll_to_pos_px = 0.0f;

		gui::WinSize m_win_size;

		inline static ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBackground;

	private:

		void showChatMembers()
		{
			ImGui::Text("%s (You)", m_user.getName().c_str());

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

			//ImGui::SetScrollFromPosY(ImGui::GetCursorStartPos().y + scroll_to_pos_px);

			// show chat messages
			for (size_t i = 0; i < m_chat_msgs.size(); ++i)
			{
				ImGui::PushTextWrapPos((float)m_win_size.first / 2.0f);
				ImGui::Text("%s", m_chat_msgs[i].c_str());
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
				sendUserChatMessage();
			}
			const bool isMsgBoxActive = ImGui::IsItemActive();
			ImGui::PopItemWidth();
			ImGui::SameLine();
			if (ImGui::Button("Send") == true)
			{
				sendUserChatMessage();
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

		void showSettings()
		{
			ImGui::SetNextWindowSize(ImVec2{ (float)m_win_size.first / 4.0f, (float)m_win_size.second });
			ImGui::SetNextWindowPos(ImVec2{ (float)m_win_size.first - (float)m_win_size.first * 0.25f,  0.0f });
			ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBackground);

			if (ImGui::Button("Disconnect") == true)
			{
				m_state = STATE::TRANSITION_CONN_TO_UNC;
			}

			ImGui::End();
		}

		void getMessageServer()
		{
			MsgHeaderValues value;
			if (netw::SBytes result = m_user.readMessageHeader(value); CHECK_RESULT(result))
			{
				int error = netw::lastError();
				if (error == netw::errs::NET_WOULDBLOCK)
				{
					return;
				}

				SYSTEM_ERROR_NUM(error, "Connection error")
			}
			else
			{
				const MessageKind msg = (MessageKind)value.first;
				switch (msg)
				{
				case MessageKind::CLIENT_CHAT_MSG:
				case MessageKind::SERVER_CHAT_MSG:
				{
					const MsgSize msg_chat_size = value.second;
					
					std::string chat_msg = m_user.readChatMsg(msg_chat_size);

					m_chat_msgs.push_back(std::move(chat_msg));
					break;
				}
				case MessageKind::SERVER_CLIENT_JOINED:
				{
					ClientInfo info = m_user.readNewClientInfo();

					m_clients_info.addClientInfo(info.id, info.name);
					break;
				}

				case MessageKind::SERVER_CLIENT_DISCONNECT:
				{
					ID id = m_user.readClientDisconnect();

					m_clients_info.removeClientInfo(id);
					break;
				}
				}
			}
		}

		void sendUserChatMessage()
		{
			if (m_msg[0] == '\0')
			{
				return;
			}

			m_user.sendChatMessage(m_msg);
			clearUserMsg();
		}

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

	class Disconnected
	{

	};

	class MessagingClientSide
	{
	public:
		
		MessagingClientSide()
		{
			m_unc = std::make_unique<Unconnected>();
			m_active_state = m_unc.get();
		}

		void run()
		{
			while (!gui::GUI::shouldClose())
			{
				try
				{
					gui::GUI::pollEvents();

					ImGui_ImplOpenGL3_NewFrame();
					ImGui_ImplGlfw_NewFrame();
					ImGui::NewFrame();

					ImGui::ShowDemoWindow();

					STATE state = m_active_state->runState();
					evaluateState(state);

					ImGui::Render();
					gui::GUI::clearScreen();
					ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
					gui::GUI::updateScreen();
				}
				catch (const std::exception& error)
				{
					{ LOG_ERROR(error.what()) }
					if (m_con != nullptr)
					{
						m_con.reset();
					}

					m_active_state = m_unc.get();
					m_unc->passError(error.what());

					// finish imgui stuff or else crash
					ImGui::Render();
					gui::GUI::clearScreen();
					ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
					gui::GUI::updateScreen();
				}
			}
		}


	private:

		IState* m_active_state = nullptr;

		std::unique_ptr<Unconnected> m_unc;
		std::unique_ptr<Connected> m_con;
		std::unique_ptr<Disconnected> m_dis;

	private:

		void evaluateState(STATE state)
		{
			switch (state)
			{
			case STATE::NONE:
				return;

			case STATE::TRANSITION_UNC_TO_CONN:
				if(STATE result = transitionUncToConn(); result == STATE::TRANSITION_SUCCESS)
				{
					m_active_state = m_con.get();
				}
				break;

			case STATE::TRANSITION_CONN_TO_UNC:
				m_con.reset();
				m_active_state = m_unc.get();
				break;


			}
		}

		STATE transitionUncToConn()
		{
			try
			{
				std::expected<ClientUser, MessageKind> result = connectToMessagingServer(m_unc->getName(), m_unc->getIP());
				if (result.has_value())
				{
					if (m_con == nullptr)
					{
						m_con = std::make_unique<Connected>(std::move(result.value()));
					}
				}
				else
				{
					return STATE::TRASNSITION_FAILED;
				}

				return STATE::TRANSITION_SUCCESS;
			}
			catch (const std::system_error& error)
			{
				LOG_ERROR(error.what())

				m_active_state->passError(error.what());
				return STATE::TRASNSITION_FAILED;
			}
			catch (const std::exception& error)
			{
				LOG_ERROR(error.what())

				m_active_state->passError("Failed to connect");
				return STATE::TRASNSITION_FAILED;
			}
		}
	};


}