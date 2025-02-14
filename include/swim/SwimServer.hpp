// Copyright (c) 2016 AlertAvert.com. All rights reserved.
// Created by M. Massenzio (marco@alertavert.com) on 11/22/16.

#pragma once

#include <atomic>
#include <chrono>
#include <iomanip>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include "SwimClient.hpp"
#include "SwimCommon.hpp"

namespace swim {

/**
 * Used in `SwimServer::AddRecordsToBudget()` to choose which part of the report
 * to add the list of records.
 */
enum class ReportSelector { kAlive, kSuspected };

enum class RemoveType { alive, evicted };

/**
 * Time "cost function" computes the cost of adding a record to a report, once
 * the record has age `t` seconds.
 *
 * <p>It uses a quadratic function, scaled by a constant `kTimeDecayConstant`.
 * We will continue to add records to our report, until the total "cost" of
 * doing so exceeds a given budget, given by `kTimeDecayBudget`.
 *
 * @todo we may eventually decide to make both parameters configurable in a
 * later release.
 *
 * @param t time, in seconds, since the record was last updated
 * @return the associated "cost" of adding the record to the report
 */
inline double cost(long t) { return kTimeDecayConstant * t * t; }

class SwimServer {

  /**
   * @brief update the incarnation when state of the server has changed
   * currently not really tracking any state so...
   *
   */
  std::atomic<uint64_t> incarnation_;
  std::atomic<LamportTime> lamport_time_;
  unsigned short port_;
  unsigned int num_threads_;
  std::atomic<bool> stopped_;
  std::chrono::milliseconds polling_interval_;
  std::optional<ServerStatusFunc> statusCb;
  std::string host_name_;

  /**
   * The list of servers that we deem to be healthy (they responded to a ping
   * request). The timestamp is the last time we successfully pinged the server.
   */
  ServerRecordsSet alive_;

  /**
   * The list of servers we suspect to have crashed; the timestamp was the time
   * at which the server was placed on this list (and will dictate when we
   * finally determine it to have crashed, after the `grace_period_` time has
   * elapsed).
   */
  ServerRecordsSet suspected_;

  /**
   * Access to the collection of "alive" servers must be thread-safe;
   * use a std::lock_guard to protect access.
   *
   * @todo std::mutex is not re-entrant: use a std::unique_lock instead.
   */
  mutable std::mutex alive_mutex_;

  /**
   * Access to the collection of "suspected" servers must be thread-safe;
   * use a std::lock_guard to protect access.
   *
   * @todo std::mutex is not re-entrant: use a std::unique_lock instead.
   */
  mutable std::mutex suspected_mutex_;

  /**
   * Using the `::utils::cost()` function, adds to the `report` as many records
   * as the budget allows.
   *
   * @param report to which we are adding records
   * @param records the records to add, which will be sorted according to their
   *    timestamp and added from the most recent backwards, until the budget is
   * reached
   * @param which whether to add the records to the `alive` or `suspected` list
   */
  void AddRecordsToBudget(
      SwimReport &report, std::vector<ServerRecord> &records,
      const ReportSelector &which = ReportSelector::kAlive) const;

protected:
  /**
   * Invoked when the `client` sends a `SwimEnvelope::Type::STATUS_UPDATE`
   * message to this server.
   *
   * <p>This is a callback method that will be invoked by the server's loop, in
   * its own thread.
   *
   * @param client the server that just sent the message; the callee obtains
   * ownership of the pointer and is responsible for freeing the memory.
   */
  virtual void OnUpdate(Server *client);

  /**
   * Invoked when the `client` sends a `SwimEnvelope::Type::STATUS_REPORT`
   * message to this server.
   *
   * <p>This is a callback method that will be invoked by the server's loop, in
   * its own thread. Will also be used to update state.
   *
   * @param sender the server that just sent the message; the callee obtains
   * ownership of the pointer and is responsible for freeing the memory.
   * @param report the `SwimReport` that the `client` sent and can be used by
   * this server to update its membership list.
   */
  virtual void OnReport(Server *sender, SwimReport *report);

  /**
   * Invoked when the `client` sends a `SwimEnvelope::Type::STATUS_REQUEST`
   * message to this server.
   *
   * <p>This is essentially a callback method that will be invoked by the
   * server's loop, in its own thread.
   *
   * <p>This message requests this server to ping the `destination` server, on
   * behalf of the `sender`; the latter, if indeed alive, will then contact back
   * the original `sender` to update its status.
   *
   * <p>If the `destination` does not respond, no action is taken, beyond
   * marking it as a `suspected` server.
   *
   * @param sender the server that just sent the message; the callee obtains
   * ownership of the pointer and is responsible for freeing the memory.
   * @param destination the that the requestor (`sender`) is asksing this server
   * to try and reach. This method is responsible for freeing the memory.
   */
  virtual void OnForwardRequest(Server *sender, Server *destination);

public:
  /** Number of parallel threads handling ZMQ socket requests. */
  static const unsigned int kNumThreads = 5;

  explicit SwimServer(
      const std::string &host_name, const unsigned short port,
      std::optional<ServerStatusFunc> statusCb = std::nullopt,
      unsigned int threads = kNumThreads,
      std::chrono::milliseconds polling_interval = kDefaultPollingIntervalMsec);

  virtual ~SwimServer();

  /**
   * Run the server forever until `stop()` is called.
   *
   * <p>If you want to run it asynchronously, the easiest way is to override
   * this virtual function and run it inside its own thread.
   */
  virtual void start();

  void stop() { stopped_.store(true); }

  bool isRunning() const { return !stopped_; }

  unsigned short port() const { return port_; }

  /**
   *
   * @return the coordinates of this server (host and port; optionally, the IP
   * address too)
   */
  const Server self() const;

  /**
   * This is thread-safe to access.
   * @return the number of currently detected `alive` neighbors.
   */
  unsigned long alive_size() const {
    unsigned long num;
    {
      mutex_guard lock(alive_mutex_);
      num = alive_.size();
    }
    return num;
  }

  /**
   * This is thread-safe to access.
   * @return the number of currently detected `suspected` neighbors.
   */
  unsigned long suspected_size() const {
    mutex_guard lock(suspected_mutex_);
    return suspected_.size();
  }

  /**
   * This is thread-safe to access.
   * @return whether the set of `alive` servers is empty.
   */
  bool alive_empty() const { return alive_size() == 0; }

  /**
   * This is thread-safe to access.
   * @return whether the set of `suspected` servers is emtpy.
   */
  bool suspected_empty() const { return suspected_size() == 0; }

  /**
   * Prepares a report that can then be sent to neighbors to gossip about.
   *
   * @return a list of all known alive and suspected servers, including only
   * those that have been added since the last time a report was sent (i.e., the
   * ones we haven't gossiped about yet).
   */
  SwimReport PrepareReport() const;

  /**
   * @return From the set of `alive` neighbors, it will return a randomly chosen
   * one.
   * @throws `swim::empty_set` if the set of alive neighbors is empty.
   */
  Server GetRandomNeighbor() const;

  /**
   * @brief Get the Neighbor By Index o
   *
   * @param index
   * @return Server
   * @throws `swim::empty_set` if the set of alive neighbors is empty.
   */
  Server GetNeighborByIndex(unsigned long index) const;

  /**
   * @brief Get a random suspected server
   *
   * @param index
   * @return Server
   * @throws `swim::empty_set` if the set of suspected neighbors is empty.
   */
  Server GetRandomSuspect() const;

  /**
   * Used to report a non-responding server.
   *
   * @param server that failed to respond to a ping request and thus is
   * suspected of being in a failed state
   * @param timestamp an optional timestamp for when this server was reported as
   * suspected; defaults to `now()`
   * @return whether adding `server` to the `suspected_` set was successful
   */
  bool ReportSuspected(const Server &server, uint64_t timestamp);

  /**
   * Use this for either a newly discovered neighbor, or for a `suspected_`
   * server that was in fact found to be in a healthy state.
   *
   * @param server that will be added to the `alive_` set (and, if necessary,
   * removed from the `suspected_` one).
   * @param timestamp an optional timestamp for when this server was reported as
   * suspected; defaults to `now()`
   * @return whether adding `server` to `alive_` set was successful
   */
  bool AddAlive(const Server &server, uint64_t timestamp);

  /**
   * Removes the given server from the suspected set, thus marking it as
   * terminally unreachable (unless subsequently added back using
   * `AddAlive(const Server&)`).
   *
   * <p>Typically used when the `grace_period()` expires and the `server` has
   * not been successfully contacted, by either this `SwimServer` or other
   * "forwarders."
   *
   * @param server to remove from the set
   */
  void RemoveSuspected(const Server &, const RemoveType removeTypte);

  /**
   * @brief update the local lamport time when an event is processed
   *
   * @param time
   */
  void UpdateLamportTime(const LamportTime time) {
    lamport_time_ = (std::max(lamport_time_.load(), time) + 1);
  }

  LamportTime GetLamportTime() const { return lamport_time_.load(); }
};

/**
 * Factory method for `SwimServer` implementation classes.
 *
 * <p>Users of this library will have to provide their own implementation of
 * this method and, possibly, also a class implementation for the server to be
 * returned.
 *
 * <p>Otherwise any one of the provided default implementations can be returned,
 * properly initialized.
 *
 * @param port the port the server will be listening on; use -1 for a randomly
 * chosen one.
 * @return an implementation of the `SwimServer` server.
 */
std::unique_ptr<SwimServer> CreateServer(unsigned short port);

} // namespace swim
