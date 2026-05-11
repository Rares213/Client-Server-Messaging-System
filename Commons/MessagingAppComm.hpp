#pragma once

#include <string_view>
#include <array>
#include <expected>

#include "netw.hpp"
#include "logger.hpp"

#define CHECK_RESULT(result)  result == netw::errs::NET_CONN_DOWN || result == netw::errs::NET_SOCKET_ERROR

namespace msgapp
{
	constexpr size_t NAME_LEN = 15u;
	constexpr std::string_view PORT = "30802";

	class MessagingClient;

	enum class UserFate { ACCEPT, REJECT };

	using ID = uint64_t;
	
	using MsgSize = uint64_t;
	using MsgHeaderValues = std::pair<char, MsgSize>;
	constexpr size_t headerSize = sizeof(char) + sizeof(MsgSize);


	// 0-9
	enum class Response : unsigned char
	{
		INVALID,
		ACCEPT,
		REJECT,
		TIMEOUT
	};
	// 10 - 99
	enum class Request : unsigned char
	{
		MAX_CHAT_MSG_LEN = 10,
		CHAT_HISTORY,
		CLIENTS_INFO,
		CLIENT_NAME,
		NEW_NAME
	};

	// 100 - 255
	// these should always be replicated to all
	// the chat members
	enum class Command : unsigned char
	{
		CHAT_MSG = 100,
		SERVER_MSG,
		CLIENT_JOINED,
		CLIENT_DISCONNECT
	};

	struct ClientInfo
	{
		ID id;
		std::string name;
	};

	class ClientsInfo : public netw::Serializable
	{
	public:
		virtual ~ClientsInfo() {}

		// number of client's info stored
		using NumInfo = uint32_t;
		// number of chars a name has (255 is enough for a name)
		using LenName = uint8_t;
		
		virtual netw::SBuffer serialize() override
		{
			netw::SBuffer buffer;
			buffer.reserve(sizeof(NumInfo) + m_clients_info.size() * (sizeof(ID) + sizeof(LenName) + NAME_LEN));

			writeVectorSize(buffer, static_cast<NumInfo>(m_clients_info.size()));

			for (const ClientInfo& client : m_clients_info)
			{
				writeID(buffer, client.id);
				writeStrLen(buffer, client.name);
				writeName(buffer, client.name);
			}

			buffer.shrink_to_fit();
			return buffer;
		}

		virtual void deserialize(netw::SBuffer& data)
		{

			NumInfo num_vector_info = readVectorSize(data);
			if (num_vector_info == 0) return;
			m_clients_info.reserve(num_vector_info);

			netw::SBuffer::iterator buff_iter = data.begin();
			buff_iter += sizeof(NumInfo);

			for (size_t i = 0; i < num_vector_info; ++i)
			{
				ClientInfo client_info;

				client_info.id = readID(buff_iter);
				buff_iter += sizeof(ID);

				LenName len_name = readLenName(buff_iter);
				buff_iter += sizeof(LenName);

				client_info.name = readName(buff_iter, len_name);
				buff_iter += client_info.name.size();

				m_clients_info.push_back(std::move(client_info));
			}

		}

		void addClientInfo(ID id, const std::string& name)
		{
			m_clients_info.push_back({ id, name });
		}

		void removeClientInfo(ID id)
		{
			for (auto iter = m_clients_info.begin(); iter < m_clients_info.end(); ++iter)
			{
				if (iter->id == id)
				{
					m_clients_info.erase(iter);
					break;
				}
			}
		}

		std::vector<ClientInfo>& getClientsList() { return m_clients_info; }

	private:

		std::vector<ClientInfo> m_clients_info;

	private:

		static void writeVectorSize(netw::SBuffer& buffer, NumInfo vec_len)
		{
			std::array<char, sizeof(NumInfo)> client_info_size_bytes = netw::serializeType<NumInfo>(vec_len);

			for (size_t i = 0; i < client_info_size_bytes.size(); ++i)
			{
				buffer.push_back(client_info_size_bytes[i]);
			}
		}

		static void writeID(netw::SBuffer& buffer, ID id)
		{
			std::array<char, sizeof(ID)> id_bytes = netw::serializeType<ID>(id);

			for (size_t i = 0; i < id_bytes.size(); ++i)
			{
				buffer.push_back(id_bytes[i]);
			}
		}

		static void writeStrLen(netw::SBuffer& buffer, const std::string& name)
		{
			LenName name_len = static_cast<LenName>(name.size());

			std::array<char, sizeof(LenName)> len_bytes = netw::serializeType<LenName>(name_len);
			buffer.push_back(len_bytes[0]);
		}

		static void writeName(netw::SBuffer& buffer, const std::string& name)
		{
			netw::SBuffer name_bytes = netw::serializeBasicStr(name);

			for (size_t i = 0; i < name_bytes.size(); ++i)
			{
				buffer.push_back(name_bytes[i]);
			}
		}

		static NumInfo readVectorSize(netw::SBuffer& buffer)
		{
			netw::SBuffer::iterator iter = buffer.begin();

			std::array<char, sizeof(NumInfo)> vector_len_bytes;
			std::memcpy(vector_len_bytes.data(), buffer.data(), vector_len_bytes.size());

			NumInfo vector_len = netw::deserializeType<NumInfo>(vector_len_bytes);
			return vector_len;
		}

		static ID readID(netw::SBuffer::iterator buff_iter)
		{
			std::array<char, sizeof(ID)> id_bytes;
			std::memcpy(id_bytes.data(), &(*buff_iter), id_bytes.size());

			ID id = netw::deserializeType<ID>(id_bytes);
			return id;
		}

		static LenName readLenName(netw::SBuffer::iterator buff_iter)
		{
			std::array<char, sizeof(LenName)> len_name_bytes;
			std::memcpy(len_name_bytes.data(), &(*buff_iter), sizeof(LenName));

			LenName name_len = netw::deserializeType<LenName>(len_name_bytes);
			return name_len;
		}

		static std::string readName(netw::SBuffer::iterator buff_iter, LenName len_name)
		{
			std::string name(&(*buff_iter), len_name);

			return name;
		}

	};

	MsgHeaderValues getMessageHeader(std::array<char, headerSize> header_bytes)
	{
		MsgHeaderValues values;
		values.first = header_bytes[0];

		netw::bytes_t<MsgSize> size_bytes;
		std::copy(header_bytes.begin() + 1, header_bytes.end(), size_bytes.begin());

		values.second = netw::deserializeType<MsgSize>(size_bytes);

		return values;
	}

	enum class Authority : unsigned char { NONE, SERVER, CLIENT };

	class MessagingClient : public netw::Client
	{
	public:
		MessagingClient() {}
		MessagingClient(Authority authority, netw::Client client) 
			: 
			netw::Client(std::move(client)),
			m_authority(authority) 
		{ 
			setBlocking(true); 
		}

		MessagingClient(Authority authority, const std::string& name, netw::Client client) 
			: 
			netw::Client(std::move(client)), 
			m_name(name), 
			m_authority(authority)
		{ 
			setBlocking(true); 
		}
		virtual ~MessagingClient() {}

		MessagingClient(MessagingClient&& other) noexcept : netw::Client(std::move(other))
		{
			m_name = other.m_name;
			m_authority = other.m_authority;

			other.m_authority = Authority::NONE;
		}

		void setName(const std::string& name) { m_name = name; }
		const std::string& getName() const { return m_name; }

		ID getID() const noexcept { return (ID)getSockHandle(); }

		netw::SBytes readMessageHeader(MsgHeaderValues& values)
		{
			std::array<char, headerSize> header_bytes;
			netw::SBytes result = receiveMessageRaw(header_bytes.data(), header_bytes.size());

			values = getMessageHeader(header_bytes);

			return result;
		}

		Authority getAuthority() const noexcept { return m_authority; }

	protected:

		std::string m_name;
		
	private:

		Authority m_authority = Authority::NONE;
	};
	
	/*
	  Copies the contents of buffer in str
	*/
	void copySBufferToStr(const netw::SBuffer& buffer, std::string& str)
	{
		str = std::string(buffer.data(), buffer.size());
	}

	/*
	  Copies at most copy_len items from buffer to str
	  If copy_len > buffer.size(), it will only copy buffer.size() elements
	  Clears the contents of the string before copying
	  and also shrinks to fit
	*/
	void copySBufferToStr(const netw::SBuffer& buffer, std::string& str, size_t copy_len)
	{
		str.clear();
		str.reserve(copy_len);

		for (size_t i = 0; i < buffer.size() && i < copy_len; ++i)
		{
			str.push_back(buffer[i]);
		}
		str.shrink_to_fit();
	}

	/*
	  Copies the contents of str into buffer
	*/
	void copyStrToSBuffer(netw::SBuffer& buffer, const std::string& str)
	{
		buffer = netw::SBuffer(str.size());
		std::copy(str.begin(), str.end(), buffer.begin());
	}

	/*
	  Returns a buffer with the contents of str
	*/
	netw::SBuffer copyStrToSBuffer(const std::string& str)
	{
		netw::SBuffer buffer(str.size());
		std::copy(str.begin(), str.end(), buffer.begin());

		return buffer;
	}

	/*
	  Checks if response is in range for Response enum
	*/
	bool isResponse(unsigned char response) noexcept
	{
		if (response >= 0 && response <= 9) 
		{
			return true;
		}
		else
		{
			return false;
		}
	}

	/*
	  Checks if the first byte corresponds to a request enum
	  Return false if buffer is empty
	*/
	bool isRequest(Request request, const netw::SBuffer& buffer) noexcept
	{
		if (buffer.size() > 0u)
		{
			if (buffer[0] == (char)request) 
			{
				return true;
			}
		}

		return false;
	}

	std::string_view requestToStr(Request request)
	{
		switch (request)
		{
		case Request::MAX_CHAT_MSG_LEN:
			return "MAX CHAT MESSAGE LENGTH";

		case Request::CHAT_HISTORY:
			return "CHAT HISTORY";

		case Request::CLIENTS_INFO:
			return "CLIENTS INFO";

		case Request::CLIENT_NAME:
			return "CLIENT NAME";

		case Request::NEW_NAME:
			return "NEW NAME";

		default:
			return "NOT IMPLEMENTED";
		}
	}

	std::string_view commandToStr(Command cmd)
	{
		switch (cmd)
		{
		case Command::CHAT_MSG:
			return "CHAT_MSG";

		case Command::CLIENT_JOINED:
			return "CLIENT_JOINED";

		case Command::CLIENT_DISCONNECT:
			return "CLIENT_DISCONNECTED";

		default:
			return "NOT IMPLEMENTED";
		}
	}

	void addMessageHeader(char msg_type, netw::SBuffer& buffer)
	{
		netw::bytes_t<MsgSize> msg_size_bytes = netw::serializeType((MsgSize)buffer.size());

		for (size_t i = 0u; i < headerSize; ++i)
		{
			buffer.insert(buffer.begin(), '\0');
		}

		buffer[0] = msg_type;
		std::memcpy(buffer.data() + sizeof(char), msg_size_bytes.data(), msg_size_bytes.size());
	}

	/*
	  Todo: what happens when buffer size is lower than headerSize ?
	*/
	void removeMessageHeader(netw::SBuffer& buffer)
	{
		for (size_t i = 0; i < headerSize; ++i)
		{
			buffer.erase(buffer.begin());
		}
	}

}
