// Copyright (c) 2017 AlertAvert.com. All rights reserved.
// Created by M. Massenzio (marco@alertavert.com) on 2/12/17.

#include <glog/logging.h>

#include "swim/GossipFailureDetector.hpp"
#include <utils/utils.hpp>

#include <memory>

namespace swim {

bool operator<(const ServerRecord &lhs, const ServerRecord &rhs) {
  return lhs.server() < rhs.server();
}

bool operator<(const Server &lhs, const Server &rhs) {
  if (lhs.hostname() == rhs.hostname()) {
    return lhs.port() < rhs.port();
  }
  return lhs.hostname() < rhs.hostname();
}

bool operator==(const Server &lhs, const Server &rhs) {
  return !(lhs < rhs) && !(rhs < lhs);
}

void GossipFailureDetector::InitAllBackgroundThreads() {

  if (!gossip_server().isRunning()) {
    LOG(ERROR) << "SWIM Gossip Server is not running, please start() it before "
                  "running the "
                  "detector's background threads";
    return;
  }

  threads_.push_back(std::make_unique<std::thread>([this]() {
    while (gossip_server().isRunning()) {
      SendReport();
      std::this_thread::sleep_for(update_round_interval_);
    }
  }));

  threads_.push_back(std::make_unique<std::thread>([this]() {
    while (gossip_server().isRunning()) {
      SendReport(ReportType::SINGLE);
      std::this_thread::sleep_for(update_round_interval_);
    }
  }));

  threads_.push_back(std::make_unique<std::thread>([this]() {
    while (gossip_server().isRunning()) {
      GarbageCollectSuspected();
      std::this_thread::sleep_for(update_round_interval_);
    }
  }));

  LOG(INFO) << "All Gossiping threads for the SWIM Detector started";
}

std::set<Server> GossipFailureDetector::GetUniqueNeighbors(unsigned int k,
                                                           bool roundRobin) {
  std::set<Server> others;
  unsigned int collisions = 0;
  const unsigned int kMaxCollisions = 3;

  // round robin has found to be more performant
  int n = std::min(k, static_cast<unsigned int>(gossip_server_->alive_size()));

  if (roundRobin) {
    for (int idx = 0; idx < n;) {
      if (round_robin_index_ > gossip_server_->alive_size() - 1) {
        round_robin_index_ = 0;
      }
      const Server other =
          gossip_server_->GetNeighborByIndex(round_robin_index_++);
      if (other == gossip_server_->self()) {
        // get a different neighbor
        continue;
      }

      others.insert(other);
      idx++;
    }
  } else {
    for (int i = 0; i < n; ++i) {
      const Server other = gossip_server_->GetRandomNeighbor();
      auto inserted = others.insert(other);
      if (!inserted.second && ++collisions > kMaxCollisions) {
        // We are hitting too many already randomly-picked neighbors, clearly
        // the set is exhausted.
        break;
      }
    }
  }
  return others;
}

bool GossipFailureDetector::SendReport(SwimClient &client,
                                       const SwimReport &report,
                                       const Server &other) {
  bool ret = client.Send(report);
  if (!ret) {
    // We managed to pick an unresponsive server; let's add to suspects.
    LOG(WARNING) << "Report sending failed; adding " << other << " to suspects";
    gossip_server_->ReportSuspected(other, ::utils::CurrentTime());
    auto forwards = GetUniqueNeighbors(num_forwards_);
    const auto lamportTime = gossip_server_->GetLamportTime();
    for (const auto &fwd : forwards) {
      VLOG(2) << "Requesting " << fwd << " to ping " << other
              << " on our behalf";
      client = SwimClient(lamportTime, fwd, gossip_server_->port());

      // This is required, as `RequestPing` takes ownership of the pointer and
      // will dispose of it.
      auto ps = new Server();
      ps->CopyFrom(other);
      client.RequestPing(ps);
    }
  }

  return ret;
}

void GossipFailureDetector::SendReport(ReportType type) {
  if (gossip_server_->alive_empty()) {
    VLOG(2) << "No neighbors, skip sending report";
    return;
  }

  auto report = gossip_server_->PrepareReport();
  VLOG(2) << "Sending report, alive: " << report.alive_size()
          << "; suspected: " << report.suspected_size();

  switch (type) {
  case ReportType::FULL: {
    for (const auto &other : GetUniqueNeighbors(num_reports_)) {
      auto client = SwimClient(gossip_server_->GetLamportTime(), other,
                               gossip_server_->port());
      VLOG(2) << "Sending report to " << other;

      if (SendReport(client, report, other)) {
        gossip_server_->AddAlive(other, ::utils::CurrentTime());
      }
    }
  } break;
  case ReportType::SINGLE: {
    const Server other = gossip_server_->GetRandomNeighbor();
    auto client = SwimClient(gossip_server_->GetLamportTime(), other,
                             gossip_server_->port());
    if (SendReport(client, report, other)) {
      gossip_server_->AddAlive(other, ::utils::CurrentTime());
    }
  } break;
  }
}

void GossipFailureDetector::GarbageCollectSuspected() const {
  SwimReport report = gossip_server_->PrepareReport();

  long expiredTime = ::utils::CurrentTime() - grace_period().count();
  VLOG(2) << "Evicting suspects last seen before "
          << std::put_time(std::gmtime(&expiredTime), "%c %Z");

  for (const auto &suspectRecord : report.suspected()) {
    if (suspectRecord.timestamp() < expiredTime) {
      long ts = suspectRecord.timestamp();
      VLOG(2) << "Server " << suspectRecord.server()
              << " last seen at: " << std::put_time(std::gmtime(&ts), "%c %Z")
              << " exceeded grace period, presumed dead";
      gossip_server_->RemoveSuspected(suspectRecord.server());
    }
  }
}

void GossipFailureDetector::StopAllBackgroundThreads() {

  LOG(WARNING)
      << "Stopping background threads for SWIM protocol; the server will be "
      << "briefly stopped, then restarted, so that it continues to respond to "
      << "pings and forwarding requests; and receiving SWIM reports.";
  bool server_was_stopped = false;

  if (gossip_server_->isRunning()) {
    VLOG(2)
        << "Temporarily stopping server to allow threads to drain gracefully";
    gossip_server_->stop();
    server_was_stopped = true;
  }

  // A brief wait to allow the news that the server is stopped to spread.
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  VLOG(2) << "Waiting for threads to stop";
  for (auto const &thread : threads_) {
    if (thread->joinable()) {
      thread->join();
    }
  }

  if (server_was_stopped) {
    VLOG(2) << "Restarting server " << gossip_server().self();
    std::thread t([this] { gossip_server_->start(); });
    t.detach();
    // A brief wait to allow the news that the server is starting to spread.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (!gossip_server_->isRunning()) {
      LOG(FATAL) << "Failed to restart the server, terminating";
    }
  }
  LOG(WARNING) << "All Gossiping threads for the SWIM Detector terminated; "
                  "this detector is "
               << "no longer participating in Gossip.";
}

void GossipFailureDetector::AddNeighbor(const Server &host) {
  if (!gossip_server_->AddAlive(host, ::utils::CurrentTime())) {
    LOG(WARNING) << "Failed to add host " << host << " to neighbors sets";
  }
}

} // namespace swim
