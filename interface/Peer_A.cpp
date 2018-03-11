//
// Created by mc on 3/9/18.
//

#include "Peer_A.hpp"


string Peer_A::generate_regular_response(string request) {
    // For the request does not pass the verification
    // They will receive a normal web page
    stringstream read_stream;
    read_stream.write(request.c_str(), request.length() + 1);

    string method, uri, protocol;
    read_stream >> method >> uri >> protocol;

    stringstream response;
    if (protocol == "HTTP/1.1" && method == "GET") {
        response
                << "HTTP/1.1 200 OK\r\nServer: nginx\r\nContent-Type: text/html\r\nLocation: https://errno104.com/\r\n\r\n<html>\r\n<head><title>Hello World!</title></head><body><center><h1>It works!</h1></center></body></html>\r\n\r\n";
    } else {
        // Bad Request
        response
                << "HTTP/1.1 400 Bad Request\r\nServer: nginx\r\nContent-Type: text/html\r\nLocation: https://errno104.com/\r\n\r\n<html>\r\n<head><title>Bad Request</title></head><body><center><h1>Bad Request</h1></center></body></html>\r\n\r\n";
    }

    return response.str();
}

string Peer_A::generate_fake_response(string request) {
    // Send confirm code and process to next step

    stringstream response;
    response
            << "HTTP/1.1 200 OK\r\nServer: nginx\r\nContent-Type: text/html\r\nLocation: https://errno104.com/\r\n\r\n<html>\r\n<head><title>Hello World!</title></head><body><center><h1>It works!</h1></center></body></html>\r\n\r\n";
    response << Encryption::sha_hash(core->confirm_password);

    size_t padding = FAKE_HEADER_SIZE - response.str().length();
    response << string(padding, ' ');

    return response.str();
}

void Peer_A::start() {
    // Read
    read_handler = new Async_Read(loop, socket_id, new char[FAKE_HEADER_SIZE], FAKE_HEADER_SIZE);
    read_handler->set_timeout(DEFAULT_TIMEOUT);
    read_handler->read_event = read_handler->closed_event = read_handler->failed_event = [this](char *buf, ssize_t s) {
        string header(buf);
        delete buf;
        verify_client(header);
    };

    // Write
    write_handler = new Async_Write(loop, socket_id);
    write_handler->set_timeout(DEFAULT_TIMEOUT);
    write_handler->wrote_event = write_handler->closed_event = write_handler->failed_event = [this](char *buf, ssize_t s) {
        delete buf;
        delete this;
    };

    // Start
    read_handler->start();
}

void Peer_A::verify_client(string s) {
    string key = Encryption::sha_hash(core->password);
    auto found = s.find(key);
    if (found == string::npos) {
        // Fail the verification
        string response = generate_regular_response(s);
        write_handler->reset(strdup(response.c_str()), response.length());
        write_handler->start();

    } else {
        // Pass the verification
        string response = generate_fake_response(s);
        write_handler->reset(strdup(response.c_str()), FAKE_HEADER_SIZE);
        write_handler->wrote_event = [this](char *buf, ssize_t s) {
            delete buf;
            prepare_for_use();
        };
        write_handler->start();
    }
}

void Peer_A::prepare_for_use() {
    // Read
    on_reading_data = false;
    read_handler->set_timeout(0);
    read_handler->reset((char *) new Packet_Meta, sizeof(Packet_Meta));
    read_handler->read_event = [this](char *buf, ssize_t s) {
        DEBUG(cout << "["<< socket_id << "]\t"<<"|==> " << s << endl;)
        if (on_reading_data){
            // Current Packet is a DATA packet
            auto * connection = core->connection_b[read_meta->dispatcher];
            if (connection == nullptr){
                if (read_meta->sequence == 0){
                    // If sequence number is ZERO, then create a new connection
                    connection = core->connect_socks_server(read_meta->dispatcher);
                    connection->sort_buffer[read_meta->sequence] = new Packet(buf, (size_t) s);
                    connection->start_writer();
                }else{
                    // The connection is closed
                    write_buffer.push_back(Packet::generate_closed_signal(read_meta->dispatcher));
                    start_writer();
                    delete buf;
                }
            }else{
                // If the connection is open, inform the interface B
                connection->sort_buffer[read_meta->sequence] = new Packet(buf, (size_t) s);
                connection->start_writer();
            }

            // Prepare for the next
            delete read_meta;
            read_meta = nullptr;
            on_reading_data = false;
            read_handler->reset((char *) new Packet_Meta, sizeof(Packet_Meta));

        } else {
            // Current Packet is a META packet
            read_meta = (Packet_Meta *) buf;
            if (read_meta->signal == 0){
                // Normal data
                on_reading_data = true;
                read_handler->reset(new char[read_meta->size], read_meta->size);
            } else if (read_meta->signal == 1){
                // Close connection
                auto * connection = core->connection_b[read_meta->dispatcher];
                if (connection != nullptr){
                    connection->terminate();
                }
                read_handler->reset();
            } else if (read_meta->signal == 1){
                //ACK
                auto * connection = core->connection_b[read_meta->dispatcher];
                if (connection != nullptr){
                    connection->clear_read_buffer(read_meta->sequence);
                }
                read_handler->reset();
            }
        }
        read_handler->start();
    };
    read_handler->closed_event = read_handler->failed_event = [this](char *buf, ssize_t s) {
        DEBUG(cout << "["<< socket_id << "]\t"<<"|==x " << s << endl;)
        //close(socket_id);
        delete this;
    };

    // Write
    write_handler->set_timeout(0);
    write_handler->wrote_event = [this](char *buf, ssize_t s) {
        DEBUG(cout << "["<< socket_id << "]\t"<<"|<== " << s << endl;)
        delete write_pointer;
        write_pointer = nullptr;

        on_writing = false;
        start_writer();
    };
    write_handler->closed_event = write_handler->failed_event = [this](char *buf, ssize_t s) {
        DEBUG(cout << "["<< socket_id << "]\t"<<"|x== " << s << endl;)
        //close(socket_id);
        delete this;
    };

    // Set Available
    core->connection_a_available.push_back(this);
    read_handler->start();
}

void Peer_A::start_writer() {
    if ((!on_writing) && (!write_buffer.empty())){
        on_writing = true;

        write_pointer = write_buffer.front();
        write_buffer.pop_front();

        write_handler->reset(write_pointer->buffer, write_pointer->length);
        write_handler->start();
    }
}

Peer_A::Peer_A(ev::default_loop *loop, int descriptor, Peer_Core *core) {
    // Copy Values
    this->loop = loop;
    this->socket_id = descriptor;
    this->core = core;
    this->on_writing = false;
    this->on_reading_data = false;
}

Peer_A::~Peer_A() {
    // Read
    delete read_handler;
    delete read_meta;

    // Write
    delete write_handler;
    while (!write_buffer.empty()) {
        auto *buf = write_buffer.front();
        write_buffer.pop_front();
        delete buf;
    }
    delete write_pointer;

    // Parent
    for (int i = 0; i < core->connection_a.size(); ++i) {
        if (core->connection_a[i] == this) {
            core->connection_a.erase(core->connection_a.begin() + i);
            break;
        }
    }
    for (int i = 0; i < core->connection_a_available.size(); ++i) {
        if (core->connection_a_available[i] == this) {
            core->connection_a_available.erase(core->connection_a_available.begin() + i);
            break;
        }
    }

}

string Peer_A::info() {
    stringstream ss;
    ss << "Peer_A   -\t socket ID:" << socket_id;
    ss << "\t\t";
    ss << "W:" << write_buffer.size();
    return ss.str();
}