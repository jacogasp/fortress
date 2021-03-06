#include "TCPServer.h"

TCPServer::TCPServer(uint16_t port) : m_server(port) {
    m_server.onClient(
        [&](void *arg, AsyncClient *c) {
            c->onData([&](void *arg, AsyncClient *client, void *data, size_t len) { onData(arg, client, data, len); });
            onConnect(arg, c);
        },
        this);
}

void TCPServer::onConnect(void *arg, AsyncClient *client) {
    std::cout << "New client connected from " << client->remoteIP().toString().c_str() << '\n';
    // Dummy handshake
    Message msg;
    msg.header.id = fortress::net::MsgTypes::ServerAccept;
    sendMessage(msg, client);
}

void TCPServer::begin() { m_server.begin(); }

void TCPServer::end() { m_server.end(); }

void TCPServer::printData(uint8_t *data, size_t len) {
    for (auto ptr{data}; ptr != data + len; ++ptr) {
        std::cout << std::setfill('0') << std::setw(2) << std::hex << (int)(*ptr) << ' ';
    }
    std::cout << std::endl;
}

void TCPServer::readHeader(uint8_t *data) {
    std::memcpy(&m_tempInMessage.header, data, sizeof(Header));

    if (m_tempInMessage.header.size > 0) {
        // Make space to fit the incoming body
        m_tempInMessage.body.resize(m_tempInMessage.header.size);
        // Tell the server that it should expect a body
        m_expectedBodyLength = m_tempInMessage.header.size;
    }
}

void TCPServer::readBody(uint8_t *data) { std::memcpy(m_tempInMessage.body.data(), data, m_tempInMessage.size()); }

void TCPServer::onMessage(Message &msg, AsyncClient *client) {
    // Make an action according to the Header ID
    if (msg.header.id == fortress::net::MsgTypes::ServerPing) sendMessage(msg, client);
    if (m_onMessageCallback) m_onMessageCallback(msg, client);
}

// Here we can have data of any length: smaller, greater or equal to the full
// message (header + body), so we have to keep track of the amount of data we
// already stored
void TCPServer::onData(void *arg, AsyncClient *client, void *data, size_t len) {
    // Cast the pointer in order to have a valid arithmetic
    uint8_t *buffer = static_cast<uint8_t *>(data);
    size_t offset = 0;
    // Keep processing data until we have processed all the valid messages in
    // the queue
    while (len - offset > 0) {
        // We are ready to read the header
        if (m_expectedBodyLength == 0) {
            readHeader(buffer + offset);
            offset += sizeof(Header);
        }
        // We already read the header, thus we expect a not-null body
        else {
            readBody(buffer + offset);
            offset += m_tempInMessage.size();
            // We read the body
            m_expectedBodyLength = 0;
        }

        // There is no more data to process, thus we have a valid message
        if (m_expectedBodyLength == 0) {
            onMessage(m_tempInMessage, client);
        }
    }
}

void TCPServer::writeHeader(AsyncClient *client) {
    assert(!m_qMessagesOut.empty() && "Write header: empty message queue");

    auto msg = &m_qMessagesOut.front();

    // Check if msg header is valid
    assert(msg->header.id >= 0 && msg->header.id <= fortress::net::MsgTypes::MessageAll);
    // Check if body size is reasonable
    assert(msg->header.size < 128);
    // Check if body size matches the actual buffer length
    if (msg->header.size != msg->body.size()) { std::cout << msg << '\n'; }
    assert(msg->header.size == msg->body.size());

    std::array<char, sizeof(Header)> headerData;
    std::memcpy(headerData.data(), &msg->header, sizeof(Header));

    // Prepare header buffer for sending
    client->add(headerData.data(), sizeof(Header));

    if (client->send()) {

        // If header has been succesfully sent, send body
        if (msg->body.empty()) {
            // If the message hasn't body, pop it out and continue with next message
            m_qMessagesOut.pop_front();
            if (!m_qMessagesOut.empty()) {
                writeHeader(client);
            }
        } else {
            writeBody(client);
        }
    } else {
        std::cout << "Failed to send header\n";
        // Retry
        if (!m_qMessagesOut.empty()) {
            writeHeader(client);
        }
    }
}

void TCPServer::writeBody(AsyncClient *client) {
    assert(!m_qMessagesOut.empty() && "Write body: empty message queue");

    auto msg = &m_qMessagesOut.front();
    // Check if msg header is valid
    assert(msg->header.id >= 0 && msg->header.id <= fortress::net::MsgTypes::MessageAll);
    // Check if body size is reasonable
    assert(msg->header.size < 128);
    // Check if body size matches the actual buffer length
    if (msg->header.size != msg->body.size()) { std::cout << msg << '\n'; }
    assert(msg->header.size == msg->body.size());
    
    client->add(reinterpret_cast<char *>(msg->body.data()), msg->size());
    if (client->send()) {
        // Message sent, remove it from the queue
        m_qMessagesOut.pop_front();

        // If there are left messages in the queue, send the next one.
        if (!m_qMessagesOut.empty()) writeHeader(client);
    } else {
        std::cout << "Failed to send body data\n";
        // Retry write body
        if (!m_qMessagesOut.empty()) {
            writeBody(client);
        }
    }
}

void TCPServer::sendMessage(const Message &msg, AsyncClient *client) {
     // Check if msg header is valid
    assert(msg.header.id >= 0 && msg.header.id <= fortress::net::MsgTypes::MessageAll);
    // Check if body size is reasonable
    assert(msg.header.size < 128);
    // Check if body size match the actual buffer length
    assert(msg.header.size == msg.body.size());
    
    bool isWritingMessage = !m_qMessagesOut.empty();
    m_qMessagesOut.push_back(msg);
    // If the queue was not empty before inserting a new message, the client is still
    // busy to finish sending previous messages
    if (!isWritingMessage) writeHeader(client);
}

void TCPServer::setOnMessageCallback(std::function<void(Message &, AsyncClient *)> callback) { m_onMessageCallback = std::move(callback); }
