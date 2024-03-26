// Copyright (c) 2016 AlertAvert.com. All rights reserved.
// Created by M. Massenzio (marco@alertavert.com) on 10/8/16.

#include <glog/logging.h>
#include <iostream>
#include <utils/utils.hpp>
#include <zmq.hpp>

#include "swim/SwimClient.hpp"

using namespace zmq;

namespace swim {

std::default_random_engine random_engine{};

SwimClient::SwimClient(const Server &dest, int self_port,
                       std::chrono::milliseconds timeout)
    : dest_(dest), timeout_(timeout) {
  self_.set_hostname(utils::Hostname());
  self_.set_port(self_port);
}

bool SwimClient::Ping() const {
  SwimEnvelope message;
  message.set_type(SwimEnvelope::Type::SwimEnvelope_Type_STATUS_UPDATE);

  if (!postMessage(&message)) {
    LOG(ERROR) << "Failed to receive reply from server";
    return false;
  }
  return true;
}

bool SwimClient::Send(const SwimReport &report) const {
  SwimEnvelope message;

  message.set_type(SwimEnvelope_Type_STATUS_REPORT);
  message.mutable_report()->CopyFrom(report);

  return postMessage(&message);
}

bool SwimClient::RequestPing(const Server *other) const {
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

bool SwimClient::postMessage(SwimEnvelope *envelope) const {
  envelope->mutable_sender()->CopyFrom(self_);
  std::string msgAsStr = envelope->SerializeAsString();

  size_t msgSize = msgAsStr.size();
  char buf[msgSize];
  memcpy(buf, msgAsStr.data(), msgSize);

  context_t ctx(1);
  socket_t socket(ctx, ZMQ_REQ);
  socket.set(zmq::sockopt::linger, swim::kDefaultSocketLingerMsec);
  socket.connect(destinationUri().c_str());

  message_t msg(buf, msgSize, nullptr);

  VLOG(2) << self_ << ": Connecting to " << destinationUri();
  if (!socket.send(msg, zmq::send_flags::none)) {
    LOG(ERROR) << self_ << ": Failed to send message to " << destinationUri();
    return false;
  }

  zmq_pollitem_t items[] = {{socket, 0, ZMQ_POLLIN, 0}};
  poll(items, 1, timeout());

  if (!items[0].revents & ZMQ_POLLIN) {
    LOG(ERROR) << self_ << ": Timed out waiting for response from "
               << destinationUri();
    return false;
  }

  message_t reply;
  if (!socket.recv(reply, zmq::recv_flags::none)) {
    LOG(ERROR) << self_ << ": Failed to receive reply from server"
               << destinationUri();
    return false;
  }

  char response[reply.size() + 1];
  VLOG(2) << self_ << ": Received: " << reply.size() << " bytes from "
          << destinationUri();
  memset(response, 0, reply.size() + 1);
  memcpy(response, reply.data(), reply.size());
  VLOG(2) << self_ << ": Response: " << response;

  return strcmp(response, "OK") == 0;
}

} // namespace swim
