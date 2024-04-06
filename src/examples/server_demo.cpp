// Copyright (c) 2016 AlertAvert.com. All rights reserved.
// Created by M. Massenzio (marco@alertavert.com) on 10/8/16.

#include <chrono>
#include <iostream>
#include <thread>

#include <atomic>
#include <boost/program_options.hpp>
#include <glog/logging.h>
#include <zmq.hpp>

#include <utils/utils.hpp>

#include "swim/SwimClient.hpp"
#include "swim/SwimServer.hpp"
#include "version.h"

// TODO: find Posix equivalent of GNU `program_invocation_short_name`
#ifdef __linux__
#define PROG_NAME program_invocation_short_name
#else
#define PROG_NAME "swim_server_demo"
#endif

using namespace zmq;
using namespace swim;

namespace po = boost::program_options;

namespace {

std::atomic<bool> stopped(false);

} // namespace

void start_timer(unsigned long duration_sec) {
  std::thread t([=] {
    std::this_thread::sleep_for(std::chrono::seconds(duration_sec));
    stopped.store(true);
  });
  t.detach();
}

int runClient(const std::string &host, int port, const std::string &name,
              unsigned long timeout, unsigned long duration) {
  LOG(INFO) << "Running for " << duration << " seconds; timeout: " << timeout
            << " msec.";

  auto server = MakeServer(host, port);
  SwimClient client(0, *server, 0, std::chrono::milliseconds{timeout});
  auto client_svr = MakeServer(name, client.self().port());
  client.setSelf(*client_svr);

  std::chrono::milliseconds wait(1500);
  start_timer(duration);
  while (!stopped.load()) {
    if (!client.Ping()) {
      LOG(ERROR) << "Could not ping server " << host;
      return EXIT_FAILURE;
    }
    std::this_thread::sleep_for(wait);
  }
  return EXIT_SUCCESS;
}

int main(int argc, const char *argv[]) {
  GOOGLE_PROTOBUF_VERIFY_VERSION;

  google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = true;

  po::options_description opts{std::string{PROG_NAME} + " options"};

  int port = 6060;
  unsigned long timeout = 200;
  unsigned long duration = 5;
  std::string action = "";
  std::string host = "";
  std::string name = "";

  // clang-format off
  opts.add_options()("debug", "verbose output (LOG_v = 2)")
    ("help", "prints this message and exits.")
    ("version", "prints the version string for this demo and third-party libraries.")
    ("port", po::value<int>(&port)->default_value(6060),
      "an int specifying the port the server will listen on (`receive`), or connect to (`send`).")
    ("host", po::value<std::string>(&host),
      "the hostname/IP to send the status to (e.g., h123.example.org or 192.168.1.1).  Required for sending, ignored otherwise.")
    ("timeout", po::value<unsigned long>(&timeout)->default_value(200),
      "in milliseconds, how long to wait for the server to respond to the ping.")
    ("duration", po::value<unsigned long>(&duration)->default_value(5),
      "in seconds, how long the client (`send`) should run.")
    ("action", po::value<std::string>(&action),
      "one of `send` or `receive`; if the former, also specifiy the host to send the data to.")
    ("name", po::value<std::string>(&name)->default_value("client"), "name of the client.")
  ;
  // clang-format on

  po::variables_map vm;

  try {
    store(parse_command_line(argc, argv, opts), vm);
  } catch (const std::exception &e) {
    std::cerr << opts << std::endl;
    std::cerr << PROG_NAME << " :Error parsing command line: " << e.what()
              << std::endl;

    std::cout << "\nOptions given:" << std::endl;
    for (int idx = 1; idx < argc; idx++) {
      std::cout << " " << argv[idx] << std::endl;
    }
    std::cout << "\n" << std::endl;
    return EXIT_FAILURE;
  }

  notify(vm);

  if (vm.count("debug")) {
    FLAGS_v = 2;
  }

  if (vm.count("help")) {
    std::cout << opts << std::endl;
    return EXIT_SUCCESS;
  }

  ::utils::PrintVersion("Server Demo", RELEASE_STR);
  if (vm.count("version")) {
    return EXIT_SUCCESS;
  }

  if (port < 0 || port > 65535) {
    LOG(ERROR) << "Port number must be a positive integer, less than 65,535. "
               << "Found: " << port;
    return EXIT_FAILURE;
  }

  if (!vm.count("action")) {
    LOG(ERROR) << "Please specify an ACTION ('send' or 'receive')";
    return EXIT_FAILURE;
  }

  if (action == "send") {
    if (!vm.count("host") || host.empty()) {
      LOG(ERROR) << "Missing required --host option. Please specify a server "
                    "to send the status to";
      return EXIT_FAILURE;
    }
    runClient(host, port, name, timeout, duration);
  } else if (action == "receive") {
    SwimServer server(port);
    // TODO: this should run in a separate thread instead, and we just join() on
    // the timer thread.
    server.start();
  } else {
    std::cerr << opts << std::endl;
    LOG(ERROR) << "One of {send, recevive} expected; instead we got: '"
               << action << "'";
    return EXIT_FAILURE;
  }

  LOG(INFO) << "done";
  return EXIT_SUCCESS;
}
