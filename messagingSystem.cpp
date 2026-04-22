#include "netw.hpp"

#include <iostream>
#include <thread>

#define DEFAULT_PORT "27015"

// max len of a message
constexpr size_t BUFF_LEN = 512;

// max len for a client name
constexpr size_t BUFF_NAME_LEN = 3 * sizeof(void*);

// seperation between name and message
constexpr std::string_view SEPERATOR_STR = ": ";

// total len that can be sent to clients
constexpr size_t MAX_MESSAGE_BUFF_LEN = BUFF_NAME_LEN + BUFF_NAME_LEN + BUFF_LEN;

constexpr int TIMEOUT_MESSAGES = 1000;
#define TIMEOUT_ACCEPT 1000ms
bool g_b_shutdown = false;

netw::UserFate cbDecideUserFate(netw::Client& client)
{
    char buff_name[BUFF_NAME_LEN];
    std::string name;

    if(int result = netw::Recv(client.getSockHandle(), buff_name, BUFF_NAME_LEN); result >= 0)
    {
        name = std::string(buff_name, result);
        std::cout << "Name number bytes: " << result << "\nName: " << name << std::endl;
        
        client.setName(name);
        return netw::UserFate::ACCEPT;
    }
    else
    {
        //std::cout << "Error name: " << std::strerror(errno) << std::endl;
        return netw::UserFate::REJECT;
    }
}

void cbClientDeletion(netw::Client& client)
{

}

void processMessages(netw::TCPServer& server, std::vector<netw::ClientMessage>& msgs)
{
    for(netw::ClientMessage& client_msg : msgs)
    {
        netw::Client& client = server.getClient(client_msg.client_id);

        const size_t total_size = client.getName().size() + SEPERATOR_STR.size() + client_msg.buffer.size(); 
        netw::DBuffer complete_msg(total_size);

        size_t offset_compl_msg = 0u;
        std::string name = client.getName();
        //std::copy(client.getName().begin(), client.getName().end(), complete_msg.begin());
        for(size_t i = 0; i < name.size(); ++i)
        {
            complete_msg[i] = static_cast<uint8_t>(name[i]);
        }
        offset_compl_msg += name.size();

        std::copy_if(SEPERATOR_STR.begin(), SEPERATOR_STR.end(), complete_msg.begin() + offset_compl_msg, 
        [&offset_compl_msg](auto elem)
        {
            if(elem != '\0') 
            {
                offset_compl_msg++;
                return true;
            }
            else return false;
        });

        std::copy(client_msg.buffer.begin(), client_msg.buffer.end(), complete_msg.begin() + offset_compl_msg);

        client_msg.buffer = complete_msg;
    }   
}

void threadIncomingUsers(netw::TCPServer& server)
{
    using namespace std::chrono_literals;

    while(!g_b_shutdown)
    {
        std::cout << "Waiting for connection..." << std::endl;
        if(server.acceptConnection() == EAGAIN)
        {
            std::cout << "No connection; timeout" << std::endl;
            std::this_thread::sleep_for(TIMEOUT_ACCEPT);
        }

    }
    std::cout << "Thread incoming users exiting..." << std::endl;

}

void threadIncomingMessages(netw::TCPServer& server)
{
    while(!g_b_shutdown)
    {
        std::cout << "Waiting for messages..." << std::endl;
        netw::ClientsMessages msgs = server.waitUsersMessages(TIMEOUT_MESSAGES);
        if(!msgs.empty())
        {
            processMessages(server, msgs);
            std::cout << "Sending messages" << std::endl;
            server.broadcastMessages(msgs);
        }
    }

    std::cout << "Thread incoming messages exiting..." << std::endl;
}


int main()
{
    try
    {   
        addrinfo hints = {};
    
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_flags = AI_PASSIVE;

        netw::TCPServer server(hints, DEFAULT_PORT);
        server.setCallbackClientValidation(cbDecideUserFate);
        server.setCallbackClientDeletion(cbClientDeletion);
        
        std::jthread listen_thread(threadIncomingUsers, std::ref(server));
        std::jthread messages_thread(threadIncomingMessages, std::ref(server));

        while(!g_b_shutdown)
        {
            std::string cmd;
            std::getline(std::cin, cmd);

            if(cmd == "exit")
            {
                g_b_shutdown = true;
            }
        }
        

    }
    catch(const std::system_error& err)
    {
        std::cout << err.code().message() << std::endl;
    }
}

