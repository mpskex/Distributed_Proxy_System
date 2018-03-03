//
// Created by mc on 2/23/18.
//

#include "Peer_B.hpp"

/////////////////////////////////////
void Peer_B::start(){

    fcntl(this->socket_id, F_SETFL, fcntl(this->socket_id, F_GETFL) | O_NONBLOCK);

    // write
    this->wf = new W_Filter(loop, &this->write_buffer, this->socket_id, &this->wf);
    this->wf->set_keep(false);

    //Read
    this->rf = new R_Filter(loop, &this->read_buffer, this->socket_id, &this->rf, this->dispatcher_id);
    this->rf->hook_recv = [this](R_Filter * rf){
        // TODO: Append meta information to package
    };

    // start everything
    this->rf->start();
    this->wf->start();
}


/////////////////////////////////////
Peer_B::Peer_B(ev::default_loop *loop, struct Proxy_Peer * peer, int dispatcher_id) {
    this->loop = loop;
    this->socket_id = peer->descriptor;
    this->dispatcher_id = dispatcher_id;
    this->active = false;

    this->dispatcher_id = ++unique_id;
    Peer_B::interface_list[this->dispatcher_id] = this;
    Peer_B::available_list.push_back(this);
}


Peer_B::~Peer_B() {
    Peer_B::interface_list.erase(this->dispatcher_id);
}

string Peer_B::info() {
    stringstream ss;
    ss << "Peer_B -\t" << socket_id << "\t" << dispatcher_id;
    ss << "\t" << "\tR:" << read_buffer.size() << "\tW:" << write_buffer.size();
    ss << "\t" << (active ? "ON" : "OFF");
    return ss.str();
}


int Peer_B::unique_id = 0;
map<int, Peer_B *> Peer_B::interface_list = map<int, Peer_B *>();
vector<Peer_B *> Peer_B::available_list = vector<Peer_B *>();
function<void (Peer_B *, struct Data_Package *)> Peer_B::hook_core_recv = nullptr;