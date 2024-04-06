// Copyright (c) 2016 AlertAvert.com. All rights reserved.
// Created by M. Massenzio (marco@alertavert.com) on 10/8/16.

#include <future>
#include <glog/logging.h>
#include <iostream>
#include <utils/utils.hpp>
#include <zmq.hpp>

#include "swim/SwimClient.hpp"

using namespace zmq;

namespace swim {

std::default_random_engine random_engine{};

SwimClient::SwimClient(const std::string &hostname, const LamportTime time,
                       const Server &dest, int self_port,
                       std::chrono::milliseconds timeout)
    : lamport_time_(time), dest_(dest), timeout_(timeout) {
  auto hname = hostname.size() == 0 ? utils::Hostname() : hostname;
  self_.set_hostname(hname);
  self_.set_port(self_port);
  self_.set_ip_addr(utils::InetAddress(hname));
}

bool SwimClient::Ping() {
  SwimEnvelope message;
  message.set_type(SwimEnvelope::Type::SwimEnvelope_Type_STATUS_UPDATE);

  if (!postMessage(&message)) {
    LOG(ERROR) << "Failed to receive reply from server";
    return false;
  }
  return true;
}

bool SwimClient::Send(const SwimReport &report) {
  SwimEnvelope message;

  message.set_type(SwimEnvelope_Type_STATUS_REPORT);
  message.mutable_report()->CopyFrom(report);

  auto ret = postMessage(&message);

  return ret;
}

bool SwimClient::RequestPing(const Server *other) {
  SwimEnvelope message;
  message.set_type(SwimEnvelope_Type_STATUS_REQUEST);
  message.set_allocated_destination_server(const_cast<Server *>(other));

  return postMessage(&message);
}

std::chrono::milliseconds SwimClient::timeout() const { return timeout_; }

void SwimClient::set_timeout(std::chrono::milliseconds timeout_) {
  SwimClient::timeout_ = timeout_;
}

unsigned int SwimClient::max_allowed_reports() const {
  return max_allowed_reports_;
}

void SwimClient::set_max_allowed_reports(unsigned int max_allowed_reports_) {
  SwimClient::max_allowed_reports_ = max_allowed_reports_;
}

bool SwimClient::postMessage(SwimEnvelope *envelope) {
  lamport_time_++;

  envelope->mutable_sender()->CopyFrom(self_);
  envelope->set_lamport_time(lamport_time_);

  std::string msgAsStr = envelope->SerializeAsString();

  size_t msgSize = msgAsStr.size();
  char buf[msgSize];
  memcpy(buf, msgAsStr.data(), msgSize);

  // zmq can hang while trying to cleanup so wrap in a promise
  std::promise<bool> prom;
  std::future<bool> fut = prom.get_future();

  // Start a thread to set the value
  std::thread([=, &prom, &buf] {
    context_t ctx(1);
    ctx.set(zmq::ctxopt::blocky, false);
    socket_t socket(ctx, ZMQ_REQ);
    // socket.set(zmq::sockopt::linger, kDefaultSocketLingerMsec);

    socket.connect(destinationUri().c_str());

    message_t msg(buf, msgSize, nullptr);

    VLOG(2) << self_ << ": Connecting to " << destinationUri();
    if (!socket.send(msg, zmq::send_flags::none)) {
      LOG(ERROR) << self_ << ": Failed to send message to " << destinationUri();
      prom.set_value(false);
      return;
    }

    zmq_pollitem_t items[] = {{socket, 0, ZMQ_POLLIN, 0}};
    poll(items, 1, timeout_);

    if (!items[0].revents & ZMQ_POLLIN) {
      LOG(ERROR) << self_ << ": Timed out waiting for response from "
                 << destinationUri();
      prom.set_value(false);
      return;
    }

    message_t reply;
    if (!socket.recv(reply, zmq::recv_flags::none)) {
      LOG(ERROR) << self_ << ": Failed to receive reply from server"
                 << destinationUri();
      prom.set_value(false);
      return;
    }

    char response[reply.size() + 1];
    VLOG(2) << self_ << ": Received: " << reply.size() << " bytes from "
            << destinationUri();
    memset(response, 0, reply.size() + 1);
    memcpy(response, reply.data(), reply.size());
    VLOG(2) << self_ << ": Response: " << response;

    prom.set_value(strcmp(response, "OK") == 0);
  }).detach();

  return fut.get();
}

} // namespace swim
