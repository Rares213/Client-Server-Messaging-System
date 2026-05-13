#pragma once

#include <cstdint>
#include <memory>
#include <queue>
#include <exception>
#include <vector>
#include <string>
#include <string_view>
#include <expected>
#include <mutex>
#include <chrono>
#include <unordered_map>
#include <algorithm>
#include <new>
#include <array>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN 
    #include <windows.h>
    #include <winsock2.h>
    #include <ws2tcpip.h>

    #pragma comment (lib, "Ws2_32.lib")
    #pragma comment (lib, "Mswsock.lib")
    #pragma comment (lib, "AdvApi32.lib")
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netdb.h>
    #include <arpa/inet.h>
    #include <poll.h>
    #include <fcntl.h>
#endif

#define SYSTEM_ERROR(str) throw std::system_error(std::error_code(netw::lastError(), std::system_category()), str);
#define SYSTEM_ERROR_NUM(err, str) throw std::system_error(std::error_code(err, std::system_category()), str);

namespace netw
{
    constexpr size_t IPV4_LEN = 15u;

    #ifdef _WIN32
        inline WSADATA wsaData;
        inline bool b_wsa_initialized = false;
        using Socket_t = SOCKET;
        using SBytes = int;
    #else
        using Socket_t = int;
        using SBytes = ssize_t;
    #endif

    class ClientMessage;

    using SBuffer = std::vector<char>;
    using ClientsMessages = std::vector<ClientMessage>;
    
    template<typename T>
    using bytes_t = std::array<char, sizeof(T)>;
    
    namespace errs
    {
        #ifdef _WIN32

            constexpr Socket_t NET_INVALID_SOCKET = INVALID_SOCKET;
            constexpr long NET_WOULDBLOCK = WSAEWOULDBLOCK;
            constexpr int NET_SOCKET_ERROR = SOCKET_ERROR;
            constexpr int NET_CONN_DOWN = 0;

        #else

            constexpr Socket_t NET_INVALID_SOCKET = -1;
            constexpr long NET_WOULDBLOCK = EWOULDBLOCK;
            constexpr int NET_SOCKET_ERROR = -1;
            constexpr int NET_CONN_DOWN = 0;

        #endif
    }

    SBuffer initBuffer(size_t size) { return SBuffer('\0', size); }

    void initNetw()
    {
        #ifdef _WIN32
            if(b_wsa_initialized == false)
            {
                if(int result = WSAStartup(MAKEWORD(2, 2), &wsaData); result != 0)
                {
                    SYSTEM_ERROR_NUM(result, "WSAStartup failed")
                }
                b_wsa_initialized = true;
            }
        #endif
    }

    void cleanup()
    {
        #ifdef _WIN32

            if(b_wsa_initialized == true)
            {
                WSACleanup();
                b_wsa_initialized = false;
            }

        #endif
    }

    int lastError() noexcept
    {
        #ifdef _WIN32
            return WSAGetLastError();
        #else
            return errno;
        #endif
    }
    
    std::string reventsToStr(short revent)
    {
        std::string revents;
        
        if(revent & POLLIN)
        {
            revents += std::string(" POLLIN ");
        }
        if(revent & POLLHUP)
        {
            revents += std::string(" POLLHUP ");
        }

        if(revent & POLLERR)
        {
            revents += std::string(" POLLERR ");
        }

        return revents;
    }

    Socket_t Socket(int domain, int type, int protocol) noexcept
    {
        Socket_t socket { errs::NET_INVALID_SOCKET };
        
        if(int result = ::socket(domain, type, protocol); result != -1)
        {
            socket = result;
        }

        return socket;
    }

    int Bind(Socket_t socket, const sockaddr* addr, socklen_t addrlen) noexcept
    {
        return bind(socket, addr, addrlen);
    }

    int Listen(Socket_t socket, int backlog) noexcept
    {
        return listen(socket, backlog);
    }

    Socket_t Accept(Socket_t socket) noexcept
    {
        Socket_t result = ::accept(socket, NULL, NULL);
        if(result != -1)
        {
            return result;
        }
        else 
        {
            return Socket_t { errs::NET_INVALID_SOCKET };
        }
    }

    int Connect(Socket_t socket, const sockaddr* addr, socklen_t addrlen) noexcept
    {
        return connect(socket, addr, addrlen);
    }

    SBytes Send(Socket_t socket, const void* buff, size_t size) noexcept
    {
        #ifdef _WIN32   
            return send(socket, static_cast<const char*>(buff), size, 0);
        #else
            return send(socket, buff, size, 0);
        #endif
    }

    SBytes Recv(Socket_t socket, void* buff, size_t size) noexcept
    {
        #ifdef _WIN32
            return recv(socket, static_cast<char*>(buff), size, 0);
        #else
            return recv(socket, buff, size, 0);
        #endif
    }

    int Close(Socket_t socket) noexcept
    {
        #ifdef _WIN32
            return closesocket(socket);
        #else
            return close(socket);
        #endif 
    }

    int Poll(pollfd* fds, unsigned long nfds, int timeout) noexcept
    {
        #ifdef _WIN32
            return WSAPoll(fds, nfds, timeout);
        #else
            return poll(fds, nfds, timeout);
        #endif
    }

    int setBlockingSocket(Socket_t socket, bool should_block) noexcept
    {
        #ifdef _WIN32

            unsigned long mode = should_block ? 0 : 1;
            return ioctlsocket(socket, FIONBIO, &mode);

        #else

            int flags = fcntl(socket, F_GETFL);
            if (flags == -1) return -1;

            flags = should_block ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);

            return fcntl(socket, F_SETFL, flags);

        #endif
    }

    class Serializable
    {
    public:
        virtual ~Serializable() {}

        virtual SBuffer serialize() = 0;
        virtual void deserialize(SBuffer& data) = 0;
    };

    class Client
    {   
    public:

        Client() noexcept {}
        Client(Socket_t socket) noexcept : m_socket_h(socket) {}
        virtual ~Client() 
        { 
            if(m_socket_h != errs::NET_INVALID_SOCKET) 
            {
                Close(m_socket_h);
            }
        }

        Client(Client&& other) noexcept
        {
            m_socket_h = other.m_socket_h;

            other.m_socket_h = errs::NET_INVALID_SOCKET;
        }
        void operator=(Client&& other) noexcept
        {
            m_socket_h = other.m_socket_h;
 
            other.m_socket_h = errs::NET_INVALID_SOCKET;
        }

        Client(const Client& other) = delete;
        void operator=(const Client& other) = delete;


        /*
          Return the numbers of bytes send,
          else return NET_SOCKET_ERROR.
        */
        virtual SBytes sendMessageRaw(const void* buffer, size_t size)
        {
            return Send(m_socket_h, buffer, size);
        }

        /*
          Return the numbers of bytes received,
          else return NET_SOCKET_ERROR.
        */
        virtual SBytes receiveMessageRaw(void* buffer, size_t size)
        {
            return Recv(m_socket_h, buffer, size);
        }

        /*
          Return the numbers of bytes send,
          else return NET_SOCKET_ERROR.
        */
        virtual SBytes sendMessage(const SBuffer& buffer) noexcept
        {
            return Send(m_socket_h, buffer.data(), buffer.size());
        }

        /*
          Return the numbers of bytes received,
          else return NET_SOCKET_ERROR.
        */
        virtual SBytes receiveMessage(SBuffer& buffer) noexcept
        {
             return Recv(m_socket_h, buffer.data(), buffer.size());
        }

        /*
          Sends one byte.
          Return the numbers of bytes send,
          else return NET_SOCKET_ERROR.
        */
        virtual SBytes sendSignal(char signal) noexcept
        {
            return Send(m_socket_h, &signal, sizeof(char));
        }

        /*
          Receives a signal which can be storred in signal_received, else ignored.
          Return the numbers of bytes received,
          else return NET_SOCKET_ERROR.
        */
        virtual SBytes receiveSignal(char* signal_received = nullptr)
        {
            char signal = 0;
            SBytes result = Recv(m_socket_h, &signal, sizeof(char));
            if (result != errs::NET_SOCKET_ERROR && signal_received != nullptr)
            {
                *signal_received = signal;
            }

            return result;
        }

        /*
        * Get the native handle of he socket
        */
        Socket_t getSockHandle() const noexcept { return m_socket_h; } 

        /*
        * Set if the socekt should be blocking or not
        * true - block
        * false - non-blocking
        */
        int setBlocking(bool should_block) noexcept { return setBlockingSocket(m_socket_h, should_block); }

    private:

        Socket_t m_socket_h = { errs::NET_INVALID_SOCKET };
    };

    /*
        Throws system_error if:
        - can't get address info;
        - can't create a socket.

        May also throw bad_alloc if there's not enough memory
        to allocate internal structures.
    */
    Client connectToServer(addrinfo* hints, std::string_view ip, std::string_view port)
    {
        addrinfo* address = (addrinfo*)std::calloc(1, sizeof(addrinfo));
        if (address == NULL)
        {
            throw std::bad_alloc();
        }

        if (int result = getaddrinfo(ip.data(), port.data(), hints, &address); result != 0)
        {
            std::free(address);
            SYSTEM_ERROR("Failed to get address info")
        }

        Socket_t socket = errs::NET_INVALID_SOCKET;
        for (addrinfo* info = address; info != NULL; info = info->ai_next)
        {
            socket = Socket(info->ai_family, info->ai_socktype, info->ai_protocol);
            if (socket == errs::NET_INVALID_SOCKET)
            {
                freeaddrinfo(address);
                SYSTEM_ERROR("Failed to create socket")
            }

            if (int result = Connect(socket, info->ai_addr, info->ai_addrlen); result == errs::NET_SOCKET_ERROR)
            {
                // add callback
                int error = lastError();
                Close(socket);
                socket = errs::NET_INVALID_SOCKET;
                continue;
            }
            break;
        }

        return Client(socket);
    }

    struct ClientMessage
    {
        Socket_t socket_client = errs::NET_INVALID_SOCKET;
        SBuffer buffer;
    };

    /*
      Interface to modify how data is read into buffers for pollSockets.
    */
    class PollRead
    {
    public:
        virtual ~PollRead() {}

        /*
          Implement this method to add your own way to read the data into buffers.
          This method will be completely responsible all the reads.
          The method should return the number of bytes that have been read to ensure
          correct error checking.
        */
        virtual SBytes onPollRead(ClientMessage& client_msg) = 0;
    };

    class TCPServer
    {
        public:
        // can throw system_error
        TCPServer(addrinfo hints, const char* port)
        {
            m_server = (addrinfo*) std::calloc(1, sizeof(addrinfo));
            if(m_server == NULL)
            {
                throw std::bad_alloc();
            }

            int result = getaddrinfo(NULL, port, &hints, &m_server);
            if(result < 0)
            {
                std::free(m_server);
                m_server = nullptr;
                SYSTEM_ERROR("Failed to construct server")
            }
        }

        virtual ~TCPServer() 
        { 
            if(m_server != nullptr) 
            {
                freeaddrinfo(m_server); 
            }
        }

        void startListening(bool should_block = false)
        {
            m_listen_socket = std::move(ListenSocket(m_server));
            m_listen_socket.setBlocking(should_block);
        }

        // return a Client or int with WOULDBLOCK if listen socket is set to be non-blocking
        // else throws system_error 
        std::expected<Client, int> acceptConnection()
        {
            Socket_t new_socket = m_listen_socket.acceptClient();
            if (new_socket != errs::NET_INVALID_SOCKET)
            {
                return Client(new_socket);
            }
            else
            {
                int error = lastError();

                #ifdef _WIN32
                    if (error == errs::NET_WOULDBLOCK)
                    {
                        return std::unexpected(error);
                    }
                #else

                    if (error == EAGAIN || error == errs::NET_WOULDBLOCK)
                    {
                        return NET_WOULDBLOCK;
                    }
                #endif

                SYSTEM_ERROR_NUM(error, "Accept failed")
            }
        }

        void addClientPolling(const Client& client) { m_polls.push_back({ client.getSockHandle(), POLLIN, 0 }); }

        virtual void removeClientPolling(Socket_t client_socket, int error)
        {
            for (auto iter = m_polls.begin(); iter != m_polls.end(); ++iter)
            {
                if (iter->fd == client_socket)
                {
                    m_polls.erase(iter);
                    break;
                }
            }
        }

        // throws system_error if poll fails
        virtual ClientsMessages pollSockets(int timeout = 0)
        {
            if (m_polls.size() == 0u)
            {
                return ClientsMessages();
            }
            
            if (int result = Poll(m_polls.data(), m_polls.size(), timeout); result > 0)
            {
                return getMessages(result);
            }
            else if(result == 0)
            {
                return ClientsMessages();
            }
            else
            {
                SYSTEM_ERROR("Poll failed")
            }
        }

        protected:
        class ListenSocket
        {
            public:
            ListenSocket() {}
            ListenSocket(addrinfo* addr) 
            {
                m_socket_h = Socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
                if(m_socket_h == errs::NET_INVALID_SOCKET)
                {
                    SYSTEM_ERROR("Failed to create listen socket")
                }

                if(Bind(m_socket_h, addr->ai_addr, addr->ai_addrlen) == -1)
                {
                    SYSTEM_ERROR("Failed to bind listening socket")
                }

                if(Listen(m_socket_h, SOMAXCONN) == -1)
                {
                    SYSTEM_ERROR("Failed to listen")
                }
            }
            ~ListenSocket() { if(m_socket_h != errs::NET_INVALID_SOCKET) Close(m_socket_h); }

            ListenSocket(const ListenSocket& other) = delete;
            void operator=(const ListenSocket& other) = delete;
            void operator=(ListenSocket&& other) noexcept
            {
                m_socket_h = other.m_socket_h;

                other.m_socket_h = errs::NET_INVALID_SOCKET;
            }

            int setBlocking(bool should_block)
            {
                return setBlockingSocket(m_socket_h, should_block);
            }

            Socket_t acceptClient()
            {
                return Accept(m_socket_h);
            }

            private:

            Socket_t m_socket_h = errs::NET_INVALID_SOCKET;
        };

        static inline int32_t MAX_MSG_LEN = 512;

        
        protected:

        addrinfo* m_server = nullptr;
        ListenSocket m_listen_socket;

        std::vector<pollfd> m_polls;

        std::unique_ptr<PollRead> m_poll_read_mod;

        ClientsMessages getMessages(int num_polls_revents)
        {
            ClientsMessages msgs;
            msgs.reserve(num_polls_revents);
            
            for(size_t index_fd = 0; index_fd < m_polls.size();)
            {
                // current poll
                pollfd& c_poll = m_polls[index_fd];
                
                if(c_poll.revents != 0)
                {
                    if(c_poll.revents & POLLHUP )
                    {
                        removeClientPolling(c_poll.fd, POLLHUP);
                    }
                    else if (c_poll.revents & POLLERR)
                    {
                        removeClientPolling(c_poll.fd, POLLERR);
                    }
                    else if(c_poll.revents & POLLIN)
                    {
                        ClientMessage client_msg{ .socket_client = c_poll.fd };
                        SBytes result = 0;

                        if (m_poll_read_mod != nullptr)
                        {
                            result = m_poll_read_mod->onPollRead(client_msg);
                        }
                        else
                        {
                            result = fallbackReadPoll(client_msg);
                        }

                        if (result == errs::NET_SOCKET_ERROR)
                        {
                            removeClientPolling(c_poll.fd, errs::NET_SOCKET_ERROR);
                        }
                        else if (result == errs::NET_CONN_DOWN)
                        {
                            removeClientPolling(c_poll.fd, errs::NET_CONN_DOWN);
                        }
                        else
                        {
                            msgs.push_back(std::move(client_msg));
                            ++index_fd;
                        }
                    }
                }
                else
                {
                    index_fd++;
                }
            }
            
            return msgs;
        }

        static SBytes fallbackReadPoll(ClientMessage& client_msg)
        {
            SBuffer recv_msg_buffer(MAX_MSG_LEN, '\0');

            const SBytes result = Recv(client_msg.socket_client, recv_msg_buffer.data(), recv_msg_buffer.size());
            
            SBuffer msg(result);
            std::memcpy(msg.data(), recv_msg_buffer.data(), result * sizeof(char));

            client_msg.buffer = std::move(msg);

            return result;
        }

    };

    template<typename T>
    std::array<char, sizeof(T)> serializeType(T src)
    {
        std::array<char, sizeof(T)> t_bytes;
        std::memcpy(t_bytes.data(), &src, sizeof(T));

        return t_bytes;
    }

    netw::SBuffer serializeBasicStr(const std::string& src)
    {
        netw::SBuffer basic_str_bytes(src.size());
        std::memcpy(basic_str_bytes.data(), src.data(), src.size());

        return basic_str_bytes;
    }

    /*
      Takes an array of bytes of type T and copies them
      into a variable of type T.
    */
    template<typename T>
    T deserializeType(std::array<char, sizeof(T)> src)
    {
        T des_var = 0;
        std::memcpy(&des_var, src.data(), src.size());

        return des_var;
    }

    /*
      Takes a serialized buffer and copies sizeof T 
      bytes into a variable of type T.
      If start_iter + sizeof T is equal or greater than
      past the end iterator, will throw out_of_range.
    */
    template<typename T>
    T deserializeType(const SBuffer& buffer, const SBuffer::iterator start_iter)
    {
        if (start_iter + (sizeof(T) - 1u) >= buffer.end())
        {
            throw std::out_of_range("Buffer is smaller than the size of the type");
        }

        T des_var = 0;
        std::memcpy(&des_var, &start_iter, sizeof(T));
        return des_var;
    }

    /*
      Takes a ptr to the start and the end of the buffer.
      If the difference between them is smaller than sizeof T,
      will throw runtime_error, else copies sizeof T bytes into
      a variable of type T.
    */
    template<typename T>
    T deserializeType(const char* buffer_start, const char* buffer_end)
    {
        ptrdiff_t size = buffer_end - buffer_start;
        if (size != sizeof(T))
        {
            throw std::runtime_error("Buffer isn't big enough for copying");
        }

        T des_var = 0;
        std::memcpy(&des_var, buffer_start, sizeof(T));

        return des_var;
    }

    /*
      Takes a raw pointer to a buffer and copies sizeof T bytes
      into a variable of type T.
      Make sure the buffer is at least sizeof T or else bad things might happen !
    */
    template<typename T>
    T deserializeType(const void* buffer)
    {
        T des_var = 0;

        std::memcpy(&des_var, buffer, sizeof(T));
        return des_var;
    }
};

