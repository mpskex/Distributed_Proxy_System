//
// Created by mc on 2/23/18.
//

#include "Client_B.hpp"


char *Client_B::generate_fake_request() {
    string fake_header = "GET / HTTP/1.1\r\nHost:errno104.com\r\nConnection: keep-alive\r\nPragma: no-cache\r\nCache-Control: no-cache\r\nUser-Agent: Mozilla/5.0 (Windows NT 6.1; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/63.0.3239.132 Safari/537.36\r\nAccept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8\r\nAccept-Encoding: gzip, deflate, br\r\nAccept-Language: en-CA,en;q=0.9,zh;q=0.8,zh-CN;q=0.7,zh-TW;q=0.6,ja;q=0.5,fr;q=0.4\r\n";

    //Construct key
    string key = Encryption::sha_hash(this->peer->password);
    string request = fake_header + key;
    request = request + string(FAKE_HEADER_SIZE - request.size(), ' ');

    return strdup(request.c_str());
}


/////////////////////////////////////
// STEP 1
void Client_B::start() {
    // Read
    this->read_handler = new Async_Read(this->loop, this->socket_id, new char[FAKE_HEADER_SIZE], FAKE_HEADER_SIZE);
    this->read_handler->set_timeout(DEFAULT_TIMEOUT);
    this->read_handler->read_event = [this](char *buf, ssize_t s) {
        string header(buf);
        delete buf;
        this->verify_peer(header);
    };
    this->read_handler->closed_event = this->read_handler->failed_event = [this](char *buf, ssize_t s) {
        delete buf;
        delete this;
    };

    // Write
    this->write_handler = new Async_Write(this->loop, socket_id, this->generate_fake_request(), FAKE_HEADER_SIZE);
    this->write_handler->set_timeout(DEFAULT_TIMEOUT);
    this->write_handler->wrote_event = [this](char *buf, ssize_t s) {
        delete buf;
        this->read_handler->start();
    };
    this->write_handler->closed_event = this->write_handler->failed_event = [this](char *buf, ssize_t s) {
        delete buf;
        delete this;
    };

    // Start
    this->write_handler->start();

}

// STEP 2
void Client_B::verify_peer(string s) {
    string key = Encryption::sha_hash(this->core->password_confirm);
    size_t found = s.find(key);
    if (found == string::npos) {
        delete this;
    } else {
        prepare_for_use();

        // Set Available
        core->connection_b_available.push_back(this);
        read_handler->start();
    }
}

// Features

void Client_B::prepare_for_use() {

    // Reading Configuration
    on_reading_data = false;
    read_handler->set_timeout(0);
    read_handler->reset((char *) new Packet_Meta, sizeof(Packet_Meta));
    read_handler->read_event = [this](char *buf, ssize_t s) {

        if (on_reading_data) {
            // Current Packet is DATA Packet
            auto *connection = core->connection_a[read_meta->dispatcher];
            if (connection == nullptr) {
                // if the connection is closed, send back data
                write_buffer.push_back(Packet::generate_closed_signal(read_meta->dispatcher));
                start_writer();
                delete buf;
            } else {
                // if the connection is not close, inform the interface A
                connection->sort_buffer[read_meta->sequence] = new Packet(buf, (size_t) s);
                connection->start_writer();
            }

            // Prepare for the next packet
            on_reading_data = false;
            read_handler->reset((char *) new Packet_Meta, sizeof(Packet_Meta));

        } else {
            // Current Packet is META Packet
            read_meta = (Packet_Meta *) buf;

            if (read_meta->signal == 0) {
                // Normal data
                on_reading_data = true;
                read_handler->reset(new char[read_meta->size], read_meta->size);

            } else if (read_meta->signal == 1) {
                // Close connection
                auto *connection = core->connection_a[read_meta->dispatcher];
                if (connection != nullptr) {
                    connection->terminate();
                }

            } else if (read_meta->signal == 2) {
                // ACK
                auto *connection = core->connection_a[read_meta->dispatcher];
                if (connection != nullptr) {
                    connection->clear_read_buffer(read_meta->sequence);
                }
                read_handler->reset((char *) new Packet_Meta, sizeof(Packet_Meta));
            }
        }
        read_handler->start();
    };
    read_handler->closed_event = read_handler->failed_event = [this](char *buf, ssize_t s) {
        delete this;
    };


    // Writing Configuration
    write_handler->set_timeout(0);
    write_handler->wrote_event = [this](char *buf, ssize_t s) {

        delete write_pointer;
        write_pointer = nullptr;

        if (write_buffer.empty()) {
            on_writing = false;
        } else {
            write_pointer = write_buffer.front();
            write_buffer.pop_front();

            write_handler->reset(write_pointer->buffer, write_pointer->length);
            write_handler->start();
        }
    };
    write_handler->closed_event = write_handler->failed_event = [this](char *buf, ssize_t s) {
        delete this;
    };
    on_writing = false;
}

void Client_B::start_writer() {
    if ((!on_writing) && (!write_buffer.empty())) {
        on_writing = true;

        write_pointer = write_buffer.front();
        write_buffer.pop_front();

        write_handler->reset(write_pointer->buffer, write_pointer->length);
        write_handler->start();
    }
}

/////////////////////////////////////
Client_B::Client_B(ev::default_loop *loop, Proxy_Peer *peer, int socket_id, Client_Core *core) {
    // Copy Values
    this->loop = loop;
    this->peer = peer;
    this->socket_id = socket_id;
    this->core = core;
}

Client_B::~Client_B() {
    if (read_meta != nullptr) {
        delete read_meta;
    }
    if (write_pointer != nullptr) {
        delete write_pointer;
    }
    delete this->read_handler;
    delete this->write_handler;

    while (!write_buffer.empty()) {
        auto *buf = write_buffer.front();
        write_buffer.pop_front();
        delete buf;
    }

    for (int i = 0; i < this->core->connection_b.size(); ++i) {
        if (this->core->connection_b[i] == this) {
            this->core->connection_b.erase(this->core->connection_b.begin() + i);
            break;
        }
    }

    for (int i = 0; i < this->core->connection_b_available.size(); ++i) {
        if (this->core->connection_b_available[i] == this) {
            this->core->connection_b_available.erase(this->core->connection_b_available.begin() + i);
            break;
        }
    }
}

string Client_B::info() {
    stringstream ss;
    ss << "Client_B -\t socket ID:" << socket_id << "\t\tW:" << write_buffer.size();
    return ss.str();
}

