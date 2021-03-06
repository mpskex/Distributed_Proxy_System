//
// Created by mc on 3/7/18.
//

#include "Async_Read.hpp"

void Async_Read::stop_watchers() {
    this->timeout_watcher.stop();
    this->read_io_watcher.stop();
}

void Async_Read::read_callback(ev::io &w, int r) {
    auto s = recv(this->descriptor, this->buffer + this->position, (size_t) this->length - this->position, 0);

    if (this->undefined_length){
        this->length = s;
    }
    if (s > 0) {
        this->position += s;
        if (this->position == this->length) {
            this->stop_watchers();
            if (use_recv){
                this->recv_event(this->buffer, this->position);
            } else {
                this->read_event(this->buffer, this->position);
            }
        } else if (use_recv){
            this->recv_event(this->buffer, this->position);
        }
    } else if (s == 0) {
        this->stop_watchers();
        this->closed_event(this->buffer, this->position);
    } else {
        if (!(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            this->stop_watchers();
            this->failed_event(this->buffer, this->position);
        }
    }
}

void Async_Read::timeout_callback(ev::timer &w, int r) {
    this->stop_watchers();
    this->failed_event(this->buffer, this->length);
}

void Async_Read::set_undefined_length(bool b) {
    this->undefined_length = b;
}

void Async_Read::set_use_recv(bool b){
    this->use_recv = b;
}

void Async_Read::set_timeout(int i) {
    this->timeout = i;
}

void Async_Read::start() {
    this->position = 0;
    if (timeout != 0){
        this->timeout_watcher.start(timeout, timeout);
    }
    this->read_io_watcher.start();
}

void Async_Read::reset(){
    this->position = 0;
}

void Async_Read::reset(char *buffer, ssize_t length){
    this->buffer = buffer;
    this->length = length;
    this->position = 0;
}

Async_Read::Async_Read(ev::default_loop *loop, int descriptor) {
    // Copy values
    this->descriptor = descriptor;
    this->timeout = 0;
    this->undefined_length = false;
    this->use_recv = false;

    // Set watchers
    this->read_io_watcher.set(*loop);
    this->read_io_watcher.set<Async_Read, &Async_Read::read_callback>(this);
    this->read_io_watcher.set(this->descriptor, ev::READ);

    this->timeout_watcher.set(*loop);
    this->timeout_watcher.set<Async_Read, &Async_Read::timeout_callback>(this);
}

Async_Read::Async_Read(ev::default_loop *loop, int descriptor, char *buffer, ssize_t length) {
    // Copy values
    this->descriptor = descriptor;
    this->buffer = buffer;
    this->position = 0;
    this->length = length;
    this->timeout = 0;
    this->undefined_length = false;
    this->use_recv = false;

    // Set watchers
    this->read_io_watcher.set(*loop);
    this->read_io_watcher.set<Async_Read, &Async_Read::read_callback>(this);
    this->read_io_watcher.set(this->descriptor, ev::READ);

    this->timeout_watcher.set(*loop);
    this->timeout_watcher.set<Async_Read, &Async_Read::timeout_callback>(this);
}

Async_Read::~Async_Read() {
    this->stop_watchers();
    this->read_event = nullptr;
    this->closed_event = nullptr;
    this->failed_event = nullptr;
    this->recv_event = nullptr;
}