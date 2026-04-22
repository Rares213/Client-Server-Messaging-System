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

#define SYSTEM_ERROR(str) throw std::system_error(std::error_code(lastError(), std::generic_category()), str);
#define SYSTEM_ERROR_NUM(err, str) throw std::system_error(std::error_code(err, std::generic_category()), str);

namespace netw
{
    #ifdef _WIN32
        inline WSADATA wsaData;
        inline bool b_wsa_initialized = false;
        using Socket_t = SOCKET;
        using SBytes = int;
        constexpr Socket_t NET_INVALID_SOCKET = INVALID_SOCKET;
        constexpr int NET_SOCKET_ERROR = SOCKET_ERROR;
        constexpr int NET_CONN_DOWN = 0;
    #else
        using Socket_t = int;
        using SBytes = ssize_t;
        constexpr Socket_t NET_INVALID_SOCKET = -1;
        constexpr int NET_SOCKET_ERROR = -1;
        constexpr int NET_CONN_DOWN = 0;
    #endif

    class Client;
    class ClientMessage;

    enum class UserFate { ACCEPT, REJECT };
    using cbClientValidation = UserFate (*)(Client& client);
    using cbClientDeletion = void (*)(Client& client);
    using ID = int;
    using PendingClient = std::pair<ID, Client>;
    using DBuffer = std::vector<uint8_t>;
    using ClientsMessages = std::vector<ClientMessage>;

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

    int lastError()
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

    Socket_t Socket(int domain, int type, int protocol)
    {
        Socket_t socket { NET_INVALID_SOCKET };
        
        if(int result = ::socket(domain, type, protocol); result != -1)
        {
            socket = result;
        }

        return socket;
    }

    int Bind(Socket_t socket, const sockaddr* addr, socklen_t addrlen)
    {
        return bind(socket, addr, addrlen);
    }

    int Listen(Socket_t socket, int backlog)
    {
        return ::listen(socket, backlog);
    }

    Socket_t Accept(Socket_t socket)
    {
        int result = ::accept(socket, NULL, NULL);
        if(result != -1)
        {
            return Socket_t { result };
        }
        else 
        {
            return Socket_t { NET_INVALID_SOCKET };
        }
    }

    int Connect(Socket_t socket, const sockaddr* addr, socklen_t addrlen)
    {
        return connect(socket, addr, addrlen);
    }

    SBytes Send(Socket_t socket, const void* buff, size_t size)
    {
        return ::send(socket, buff, size, 0);
    }

    SBytes Recv(Socket_t socket, void* buff, size_t size)
    {
        return ::recv(socket, buff, size, 0);
    }

    int Close(Socket_t socket)
    {
        #ifdef _WIN32
            closesocket(socket);
        #else
            return close(socket);
        #endif 
    }

    Socket_t connectToServer(addrinfo* hints, const char* ip, const char* port)
    {
        addrinfo* address = (addrinfo*) std::calloc(1, sizeof(addrinfo));
        if(address == NULL)
        {
            throw std::bad_alloc();
        }

        if(int result = getaddrinfo(ip, port, hints, &address); result != 0)
        {
            freeaddrinfo(address);
            SYSTEM_ERROR("Failed to get address info")
        }

        Socket_t socket = NET_INVALID_SOCKET;
        for(addrinfo* info = address; info != NULL; info = info->ai_next)
        {
            Socket_t socket = Socket(info->ai_family, info->ai_socktype, info->ai_protocol);
            if(socket == NET_INVALID_SOCKET)
            {
                freeaddrinfo(address);
                SYSTEM_ERROR("Failed to create socket")
            }

            if(int result = Connect(socket, info->ai_addr, info->ai_addrlen); result == NET_SOCKET_ERROR)
            {
                // add callback
                Close(socket);
                socket = NET_INVALID_SOCKET;
                continue;
            }
            break;
        }

        return socket;
    }

    class Client
    {   
        public:

        Client() {}
        Client(Socket_t socket) : m_socket_h(socket) {}
        Client(Socket_t socket, std::string name) : m_socket_h(socket), m_name(name) {}
        ~Client() { if(m_socket_h != NET_INVALID_SOCKET) Close(m_socket_h); }

        Client(Client&& other)
        {
            m_socket_h = other.m_socket_h;
            m_name = other.m_name;

            other.m_socket_h = NET_INVALID_SOCKET;
        }
        void operator=(Client&& other)
        {
            m_socket_h = other.m_socket_h;
            m_name = other.m_name;

            other.m_socket_h = NET_INVALID_SOCKET;
        }

        Client(const Client& other) = delete;
        void operator=(const Client& other) = delete;

        SBytes sendMessage(DBuffer& buffer)
        {
            return Send(m_socket_h, buffer.data(), buffer.size());
        }

        SBytes sendMessage(const void* buffer, size_t size)
        {
            return Send(m_socket_h, buffer, size);
        }

        SBytes receiveMessage(DBuffer& buffer)
        {
            return Recv(m_socket_h, buffer.data(), buffer.size());
        }

        SBytes receiveMessage(void* buffer, size_t size)
        {
            return Recv(m_socket_h, buffer, size);
        }

        ID getID() const { return static_cast<ID>(m_socket_h); }
        const std::string& getName() const { return m_name; }
        Socket_t getSockHandle() const { return m_socket_h; } 

        void setName(std::string name) { m_name = name; }

        private:

        Socket_t m_socket_h = { NET_INVALID_SOCKET };
        std::string m_name;
    };

    struct ClientMessage
    {
        ID client_id;
        DBuffer buffer;
    };

    class TCPServer
    {
        public:
        // can throw system_error
        TCPServer(const addrinfo& hints, const char* port)
        {
            m_server = (addrinfo*) std::calloc(1, sizeof(addrinfo));
            if(m_server == NULL)
            {
                throw std::bad_alloc();
            }

            int result = getaddrinfo(NULL, port, &hints, &m_server);
            if(result < 0)
            {
                SYSTEM_ERROR("Failed to construct server")
            }

            m_listen_socket = std::move(ListenSocket(m_server));
            result = m_listen_socket.setBlocking(false);            
        }

        ~TCPServer() 
        { 
            if(m_server != nullptr) 
            {
                freeaddrinfo(m_server); 
            }
        }

        int acceptConnection()
        {
            Socket_t new_socket = m_listen_socket.acceptClient();
            if(new_socket != NET_INVALID_SOCKET)
            {
                Client new_client(new_socket);
                if(m_cb_client_validation != nullptr)
                {
                    UserFate fate = m_cb_client_validation(new_client);
                    if(fate == UserFate::ACCEPT)
                    {
                        queueAddClient(new_socket, std::move(new_client));
                    }
                }
                else
                {
                    queueAddClient(new_socket, std::move(new_client));
                }

                return 0;
            }
            else
            {
                int error = lastError();
                if(error == EAGAIN && error == EWOULDBLOCK)
                {
                    return EAGAIN;
                }
                else
                {
                    SYSTEM_ERROR_NUM(error, "Accept failed")
                }
            }
        }
        
        Client& getClient(int id) { return m_clients.at(id); }

        void queueAddClient(ID id, Client client)
        {
            const std::lock_guard lock(m_mut);
            m_pendings.push( { id, std::move(client)} );
        }

        void deleteClient(ID client_id) { m_delete_pendings.push(client_id); }

        // may throw system_error if poll fails
        ClientsMessages waitUsersMessages(int timeout)
        {
            handlePendings();
            
            if(int result = poll(m_polls.data(), m_polls.size(), timeout); result > 0)
            {
                ClientsMessages msgs = getMessages();
                return msgs;
            }
            else if(result == 0)
            {
                return ClientsMessages();
            }
            else
            {
                int error = lastError();
                if(error == EINTR)
                {
                    return ClientsMessages();
                }

                SYSTEM_ERROR_NUM(error, "Failed to listen to messages")
            }
        }
        
        void broadcastMessages(ClientsMessages& msgs)
        {
            for(auto& client_pair : m_clients)
            {
                int client_id = client_pair.first;
                Client& client = client_pair.second;

                for(ClientMessage& client_msg : msgs)
                {
                    if(client_id != client_msg.client_id)
                    {
                        if(SBytes result = client.sendMessage(client_msg.buffer); result == NET_SOCKET_ERROR)
                        {
                            int error = lastError();
                            if(error == EAGAIN || error == EWOULDBLOCK) return;
                            if(error == ECONNRESET || error == EPIPE) return;

                            SYSTEM_ERROR_NUM(error, "Failed to send message")
                        }
                        
                    }
                }
            }
        }

        static void setCallbackClientDeletion(cbClientDeletion cb) { m_cb_client_deletion = cb; }
        static void setCallbackClientValidation(cbClientValidation cb) { m_cb_client_validation = cb; }

        private:
        class ListenSocket
        {
            public:
            ListenSocket() {}
            ListenSocket(addrinfo* addr) 
            {
                m_socket_h = Socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
                if(m_socket_h == NET_INVALID_SOCKET)
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
            ~ListenSocket() { if(m_socket_h != NET_INVALID_SOCKET) Close(m_socket_h); }

            ListenSocket(const ListenSocket& other) = delete;
            void operator=(const ListenSocket& other) = delete;
            void operator=(ListenSocket&& other)
            {
                m_socket_h = other.m_socket_h;

                other.m_socket_h = NET_INVALID_SOCKET;
            }

            int setBlocking(bool should_block)
            {
                #ifdef _WIN32

                    unsigned long mode = blocking ? 0 : 1;
                    return ioctlsocket(m_socket_h, FIONBIO, &mode);

                #else
                
                    int flags = fcntl(m_socket_h, F_GETFL);
                    if(flags == -1) return -1;
                    
                    flags = should_block ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);

                    return fcntl(m_socket_h, F_SETFL, flags);

                #endif
                
            }

            Socket_t acceptClient()
            {
                return Accept(m_socket_h);
            }

            private:

            Socket_t m_socket_h = NET_INVALID_SOCKET;
        };

        static inline size_t BUFF_LEN = 512;
        
        static inline cbClientDeletion m_cb_client_deletion = nullptr;
        static inline cbClientValidation m_cb_client_validation = nullptr;

        private:

        addrinfo* m_server = nullptr;
        ListenSocket m_listen_socket;

        std::queue<PendingClient> m_pendings;
        std::queue<ID> m_delete_pendings;
        std::mutex m_mut;

        std::unordered_map<ID, Client> m_clients;
        std::vector<pollfd> m_polls;

        ClientsMessages getMessages()
        {
            ClientsMessages msgs;
            msgs.reserve(m_polls.size());
            
            DBuffer recv_msg_buffer(BUFF_LEN);

            for(const pollfd& poll_fd : m_polls)
            {
                if(poll_fd.revents != 0)
                {
                    if(poll_fd.revents & POLLHUP || poll_fd.revents & POLLERR)
                    {
                        // client disconnected or something bad has happened, so delete
                        deleteClient(poll_fd.fd);
                    }
                    else if(poll_fd.revents & POLLIN)
                    {
                        // read the message from the client
                        Client& client = getClient(poll_fd.fd);

                        if(SBytes result = client.receiveMessage(recv_msg_buffer); result > 0)
                        {
                            DBuffer client_msg = recv_msg_buffer;
                            
                            // for now, use the fd as id
                            msgs.push_back( { poll_fd.fd, std::move(client_msg) } );
                        }
                        else if(result == NET_CONN_DOWN)
                        {
                            deleteClient(poll_fd.fd);
                        }
                    }
                }
            }

            handleDeletionQueue(); 
            
            return msgs;
        }

        void handlePendings()
        {
            const std::lock_guard lock(m_mut);
            while(!m_pendings.empty())
            {
                PendingClient pending_client = std::move(m_pendings.front());
                m_polls.push_back( { pending_client.first, POLLIN, 0 } );
                m_clients.emplace( pending_client.first, std::move(pending_client.second) );

                m_pendings.pop();
            }
        }
        
        void deleteUser(ID client_id)
        {
            Client& to_delete_client = m_clients[client_id];
            
            if(m_cb_client_deletion != nullptr)
            {
                m_cb_client_deletion(to_delete_client);
            }

            m_clients.erase(client_id);
            for(auto iter_polls = m_polls.begin(); iter_polls != m_polls.end(); ++iter_polls)
            {
                if(iter_polls->fd == client_id)
                {
                    m_polls.erase(iter_polls);
                    break;
                }
            }
        }

        void handleDeletionQueue()
        {
            while(!m_delete_pendings.empty())
            {
                deleteUser(m_delete_pendings.front());
                m_delete_pendings.pop();
            }
        }


    };

};

