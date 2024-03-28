// Copyright (c) 2016 AlertAvert.com. All rights reserved.
// Created by M. Massenzio (marco@alertavert.com) on 10/8/16.

#include <algorithm>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <thread>

#include <glog/logging.h>
#include <google/protobuf/util/json_util.h>

#include "version.h"
#include <swim.pb.h>
#include <swim/GossipFailureDetector.hpp>

#include <api/rest/ApiServer.hpp>
#include <boost/program_options.hpp>
#include <utils/utils.hpp>

// TODO: find Posix equivalent of GNU `program_invocation_short_name`
#ifdef __linux__
#define PROG_NAME program_invocation_short_name
#else
#define PROG_NAME "gossip_detector_example"
#endif

using namespace std;
using namespace swim;
namespace po = boost::program_options;
namespace http = api::rest::http;

namespace {

const unsigned short kDefaultHttpPort = 30396;

} // namespace

std::shared_ptr<GossipFailureDetector> detector;

void shutdown(int signum) {
  LOG(INFO) << "Terminating server...";
  if (detector) {
    detector->StopAllBackgroundThreads();
  }
  LOG(INFO) << "... cleanup complete, exiting now";
  exit(EXIT_SUCCESS);
}

int main(int argc, const char *argv[]) {
  GOOGLE_PROTOBUF_VERIFY_VERSION;
  google::InitGoogleLogging(argv[0]);

  std::shared_ptr<api::rest::ApiServer> apiServer;

  po::options_description opts{std::string{PROG_NAME} + " options"};

  auto port = ::kDefaultPort;
  auto http_port = ::kDefaultHttpPort;
  auto http = true;
  auto ping_timeout_msec = swim::kDefaultTimeoutMsec.count();
  auto ping_interval_msec = swim::kDefaultPingIntervalMsec.count();
  auto grace_period_msec = swim::kDefaultGracePeriodMsec.count();
  auto seeds_list = std::vector<std::string>{};
  auto cors = std::string{};
  auto no_http = true;
  // clang-format off
  opts.add_options()
    ("debug", "verbose output (LOG_v = 2)")
    ("trace", "trace output (LOG_v = 3)")
    ("help", "prints this message and exits.")
    ("version", "prints the version string for this demo and third-party libraries.")
    ("http", po::bool_switch()->default_value(true),
      "whether this server should expose a REST API (true by default, use --no-http to disable.")
    ("port", po::value<unsigned short>(&port)->default_value(::kDefaultPort),
      "an integer value specifying the TCP port the server will listen on.")
    ("http-port",po::value<unsigned short>(&http_port)->default_value(::kDefaultHttpPort),
      "the HTTP port for the REST API, if server exposes it (see --http).")
    ("timeout", po::value<int64_t>(&ping_timeout_msec)->default_value(swim::kDefaultTimeoutMsec.count()),
      "in milliseconds, how long to wait for the server to respond to the ping.")
    ("grace_period", po::value<int64_t>(&grace_period_msec)->default_value(swim::kDefaultGracePeriodMsec.count()),
      "in milliseconds, how long to wait before evicting suspected servers.")
    ("ping_interval", po::value<int64_t>(&ping_interval_msec)->default_value(swim::kDefaultPingIntervalMsec.count()),
      "interval, in milliseconds, between pings to servers in the Gossip Circle.")
    ("seeds", po::value<::utils::list_option>()->multitoken(),
      "a comma-separated list of host:port of peers that this server will"
      "ininitially connect to, and part of the Gossip ring: from these "
      "\"seeds\""
      "the server will learn eventually of ALL the other servers and "
      "connect to them too.\n"
      "The `host` part may be either an IP address (such as 192.168.1.1) or "
      "the DNS-resolvable `hostname`; for example:\n"
      "192.168.1.101:8080 192.168.1.102:8081\n"
      "node1.example.com:9999 node1.example.com:9999\n"
      "Both host and port are require; the hosts may not ALL be active.")
    ("cors", po::value<std::string>(&cors))
    ;
  // clang-format on

  po::options_description hidden_opts{std::string{"hidden options"}};
  hidden_opts.add_options()(
      "no-http", po::bool_switch(&no_http)->default_value(false), "hidden");

  po::options_description visible(std::string{PROG_NAME} + " options");
  visible.add(opts);

  po::options_description all(std::string{PROG_NAME} + " options");
  all.add(opts).add(hidden_opts);

  po::variables_map vm;

  try {
    store(parse_command_line(argc, argv, all), vm);
  } catch (const std::exception &e) {
    std::cerr << visible << std::endl;
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

  if (vm.count("trace")) {
    FLAGS_v = 3;
  }

  if (vm.count("help")) {
    std::cout << visible << std::endl;
    std::cout
        << "The server will run forever in foreground, use Ctrl-C to terminate."
        << std::endl;
    return EXIT_SUCCESS;
  }

  ::utils::PrintVersion("SWIM Gossip Server Demo", RELEASE_STR);
  if (vm.count("version")) {
    return EXIT_SUCCESS;
  }

  if (port < 0 || port > 65535) {
    LOG(ERROR) << "Port number must be a positive integer, less than 65,535. "
               << "Found: " << port;
    return EXIT_FAILURE;
  }

  LOG(INFO) << "Gossip Detector (PID: " << ::getpid()
            << ") listening on incoming TCP port " << port;

  if (vm.count("seeds")) {
    seeds_list = vm["seeds"].as<::utils::list_option>().values;
  }

  // TODO: remove after demo
  cout << "PID: " << ::getpid() << endl;

  try {

    detector = std::make_shared<GossipFailureDetector>(
        std::nullopt, port, std::chrono::milliseconds{ping_interval_msec},
        std::chrono::milliseconds{grace_period_msec},
        std::chrono::milliseconds{ping_timeout_msec});

    if (seeds_list.empty()) {
      std::cout << visible << std::endl;
      LOG(ERROR) << "A list of peers (possibly just one) is required to start "
                    "the Gossip Detector";
      return EXIT_FAILURE;
    }

    LOG(INFO) << "Connecting to initial Gossip Circle: "
              << ::utils::Vec2Str(seeds_list, ", ");
    for (const std::string &name : seeds_list) {
      try {
        auto ipPort = ::utils::ParseHostPort(name);
        string ip;
        string host{std::get<0>(ipPort)};
        if (::utils::ParseIp(host)) {
          ip = host;
        } else {
          ip = ::utils::InetAddress(host);
        }
        Server server;
        server.set_hostname(host);
        server.set_port(std::get<1>(ipPort));
        server.set_ip_addr(ip);

        LOG(INFO) << "Adding neighbor: " << server;
        detector->AddNeighbor(server);

      } catch (::utils::parse_error &e) {
        LOG(ERROR) << "Could not parse '" << name << "': " << e.what();
      }
    }

    LOG(INFO) << "Waiting for server to start...";
    int waitCycles = 10;
    while (!detector->gossip_server().isRunning() && waitCycles-- > 0) {
      std::this_thread::sleep_for(seconds(1));
    }

    LOG(INFO) << "Gossip Server " << detector->gossip_server().self()
              << " is running. Starting all background threads";
    detector->InitAllBackgroundThreads();

    LOG(INFO) << "Threads started; detector process running"; // TODO: << PID?

    if (no_http) {
      http = false;
    }

    auto addCors = false;
    if (http) {
      if (vm.count("cors")) {
        addCors = true;
        LOG(INFO) << "+++++ Enabling CORS for domain(s): " << cors;
      }

      LOG(INFO) << "Enabling HTTP REST API: http://" << utils::Hostname() << ":"
                << http_port;

      apiServer = std::make_shared<api::rest::ApiServer>(http_port);
      namespace rest = api::rest;

      apiServer->addGet("report", [addCors,
                                   cors](const rest::request &request) {
        rest::response response;
        response.version(request.version());
        response.result(api::rest::http::status::ok);

        auto report = detector->gossip_server().PrepareReport();
        std::string json_body;

        ::google::protobuf::util::MessageToJsonString(report, &json_body);
        response.body() = json_body;

        if (addCors) {
          response.set(api::rest::http::field::access_control_allow_origin,
                       cors);
        }
        response.set(api::rest::http::field::content_type, "application/json");

        return response;
      });

      apiServer->addPost("server", [](const rest::request &request) {
        Server neighbor;

        auto status = ::google::protobuf::util::JsonStringToMessage(
            request.body(), &neighbor);
        if (status.ok()) {
          detector->AddNeighbor(neighbor);
          LOG(INFO) << "Added server" << neighbor;

          std::string body{R"({ "result": "added", "server": )"};
          std::string server;
          ::google::protobuf::util::MessageToJsonString(neighbor, &server);
          rest::response response;

          response.set(http::field::content_type, "application/json");
          response.result(http::status::created);
          response.set(http::field::location, "/server/" + neighbor.hostname());
          response.body() = body + server + "}";

          return response;
        }

        LOG(ERROR) << "Not valid JSON: " << request.body();

        rest::response response;

        response.result(http::status::bad_request);
        response.set(http::field::content_type, "text/plain");
        response.body() =
            "Not a valid JSON representation of a server:" + request.body();

        return response;
      });
      LOG(INFO) << "Starting API Server";
      apiServer->start();
      LOG(INFO) << ">>>Done";
    } else {
      LOG(INFO) << "REST API will not be available";
    }

    // "trap" the SIGINT (Ctrl-C) and execute a graceful exit
    signal(SIGINT, shutdown);
    while (true) {
      std::this_thread::sleep_for(milliseconds(300));
    }

  } catch (::utils::parse_error &error) {
    LOG(ERROR) << "A parsing error occurred: " << error.what();
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
