// Copyright (c) 2016 AlertAvert.com. All rights reserved.
// Created by M. Massenzio (marco@alertavert.com) on 11/24/16.
#include <glog/logging.h>
#include <chrono>
#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include "../include/swim/SwimServer.hpp"
#include <utils/utils.hpp>

#include "tests.h"

using namespace swim;

Server parse(void *data, int size) {
  Server s2;
  s2.ParseFromArray(data, size);
  return s2;
}

TEST(SwimServerProtoTests, allocations) {
  Server server;
  server.set_hostname("fakehost");
  server.set_port(9999);
  auto bufSize = server.ByteSizeLong();

  auto data = new char[bufSize];
  ASSERT_NE(nullptr, data);
  memset(data, 0, bufSize);

  server.SerializeToArray(data, bufSize);
  Server svr2 = parse(data, bufSize);

  delete[](data);

  ASSERT_STREQ("fakehost", svr2.hostname().c_str());
  ASSERT_EQ(9999, svr2.port());
}

class TestServer : public SwimServer {

  bool wasUpdated_;

public:
  explicit TestServer(unsigned short port)
      : SwimServer(port, std::nullopt, 1), wasUpdated_(false) {}
  virtual ~TestServer() = default;

  void OnUpdate(Server *client) override {
    if (client != nullptr) {
      VLOG(2) << "TestServer::OnUpdate " << client->hostname() << ":"
              << client->port();
      wasUpdated_ = true;
      delete (client);
    }
  }

  bool wasUpdated() { return wasUpdated_; }
};

class SwimServerTests : public ::testing::Test {

protected:
  std::shared_ptr<SwimServer> server_;
  std::unique_ptr<std::thread> thread_;
  int aliveReceivedCount;
  int suspectReceivedCount;
  int suspectRemovedCount;

  SwimServerTests() {
    aliveReceivedCount = 0;
    suspectReceivedCount = 0;
    suspectRemovedCount = 0;

    unsigned short port = tests::RandomPort();
    VLOG(2) << "TestFixture: creating server on port " << port;

    server_.reset(new SwimServer(
        port, [this](std::shared_ptr<ServerRecord>, ServerStatus status) {
          if (ServerStatus::alive == status) {
            aliveReceivedCount++;
          }

          if (ServerStatus::suspect == status) {
            suspectReceivedCount++;
          }

          if (ServerStatus::removed == status) {
            suspectRemovedCount++;
          }
        }));
  }

  void TearDown() override {
    VLOG(2) << "Tearing down...";
    if (server_) {
      VLOG(2) << "TearDown: stopping server...";
      server_->stop();
    }
    if (thread_) {
      VLOG(2) << "TearDown: joining thread";
      if (thread_->joinable()) {
        thread_->join();
        VLOG(2) << "TearDown: server thread terminated";
      }
    }
  }

  virtual void runServer() {
    if (server_) {
      ASSERT_FALSE(server_->isRunning());
      thread_.reset(new std::thread([this] { server_->start(); }));
      // Wait a beat to allow the socket connection to be established.
      tests::WaitAtMostFor([this]() -> bool { return server_->isRunning(); },
                           std::chrono::milliseconds(500));
      ASSERT_TRUE(server_->isRunning());
    } else {
      FAIL() << "server_ has not been allocated";
    }
  }
};

TEST_F(SwimServerTests, canCreate) {
  ASSERT_NE(nullptr, server_);
  ASSERT_FALSE(server_->isRunning());
}

TEST_F(SwimServerTests, noServerNoPing) {
  auto svr = MakeServer("localhost", server_->port());
  SwimClient client(*svr);
  ASSERT_FALSE(server_->isRunning());
  ASSERT_FALSE(client.Ping());
}

TEST_F(SwimServerTests, canStartAndConnect) {
  unsigned short port = server_->port();
  ASSERT_TRUE(port >= tests::kMinPort && port < tests::kMaxPort);

  std::unique_ptr<Server> localhost = MakeServer("localhost", port);
  SwimClient client(*localhost);

  EXPECT_FALSE(server_->isRunning());
  ASSERT_NO_FATAL_FAILURE(runServer()) << "Could not get the server started";

  ASSERT_TRUE(server_->isRunning());
  EXPECT_TRUE(client.Ping());

  server_->stop();
  EXPECT_FALSE(server_->isRunning());
}

TEST_F(SwimServerTests, canOverrideOnUpdate) {
  unsigned short port = tests::RandomPort();
  ASSERT_TRUE(port >= tests::kMinPort && port < tests::kMaxPort);

  TestServer server(port);
  EXPECT_FALSE(server.isRunning());
  std::thread t([&] {
    VLOG(2) << "Test server thread started";
    server.start();
    VLOG(2) << "Test server thread exiting";
  });
  tests::WaitAtMostFor([&]() -> bool { return server.isRunning(); },
                       std::chrono::milliseconds(200));

  ASSERT_TRUE(server.isRunning());

  auto dest = MakeServer("localhost", port);
  SwimClient client(*dest);
  ASSERT_TRUE(client.Ping());
  ASSERT_TRUE(server.wasUpdated());

  server.stop();
  ASSERT_FALSE(server.isRunning());

  if (t.joinable()) {
    VLOG(2) << "Waiting for server to shutdown";
    t.join();
  }
}

TEST_F(SwimServerTests, destructorStopsServer) {
  unsigned short port = 55234;

  auto server = MakeServer("localhost", port);
  std::unique_ptr<SwimClient> client(new SwimClient(*server));
  {
    TestServer testServer(port);
    std::thread t([&] { testServer.start(); });
    t.detach();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    ASSERT_TRUE(testServer.isRunning());
    ASSERT_TRUE(client->Ping());
  }
  // It may take a bit for the server to stop, but not that long.
  // NOTE: we must pass the std::unique_ptr by ref or the compiler gets upset.
  ASSERT_TRUE(tests::WaitAtMostFor([&]() -> bool { return !client->Ping(); },
                                   std::chrono::milliseconds(500)));
}

TEST_F(SwimServerTests, receiveReport) {
  ASSERT_NO_FATAL_FAILURE(runServer()) << "Could not get the server started";
  ASSERT_TRUE(server_->isRunning());
  auto svr = MakeServer("localhost", server_->port());
  SwimClient client(*svr, 9200);

  SwimReport report;

  Server *sender = report.mutable_sender();
  sender->set_hostname("test.host.1");
  sender->set_port(9099);
  sender->set_ip_addr("192.168.1.1");

  auto alives = report.mutable_alive();
  ServerRecord *one = alives->Add();
  one->set_timestamp(utils::CurrentTime());
  one->mutable_server()->set_hostname("host1");
  one->mutable_server()->set_port(1234);

  auto suspected = report.mutable_suspected();
  ServerRecord *two = suspected->Add();
  two->set_timestamp(utils::CurrentTime());
  two->mutable_server()->set_hostname("host_susp");
  two->mutable_server()->set_port(9876);

  ASSERT_TRUE(client.Send(report));
  ASSERT_EQ(2, server_->alive_size());
  ASSERT_EQ(1, server_->suspected_size());
  ASSERT_EQ(2, SwimServerTests::aliveReceivedCount);
  ASSERT_EQ(1, SwimServerTests::suspectReceivedCount);
  ASSERT_EQ(0, suspectRemovedCount);
}

TEST_F(SwimServerTests, receiveReportMany) {
  ASSERT_NO_FATAL_FAILURE(runServer()) << "Could not get the server started";
  ASSERT_TRUE(server_->isRunning());
  auto svr = MakeServer("localhost", server_->port());
  SwimClient client(*svr, 9200);

  SwimReport report;
  report.mutable_sender()->CopyFrom(client.self());

  auto alives = report.mutable_alive();
  for (int i = 0; i < 10; ++i) {
    std::string host = "host-" + std::to_string(i);

    ServerRecord *one = alives->Add();
    one->set_timestamp(utils::CurrentTime());
    one->mutable_server()->set_hostname(host);
    one->mutable_server()->set_port(9900 + i);
  }

  auto suspected = report.mutable_suspected();
  for (int i = 0; i < 5; ++i) {
    std::string host = "dead-host-" + std::to_string(i);

    ServerRecord *two = suspected->Add();
    two->set_timestamp(utils::CurrentTime());
    two->mutable_server()->set_hostname(host);
    two->mutable_server()->set_port(5500 + i);
  }

  ASSERT_TRUE(client.Send(report));
  ASSERT_EQ(11, server_->alive_size());
  ASSERT_EQ(5, server_->suspected_size());

  ASSERT_EQ(11, aliveReceivedCount);
  ASSERT_EQ(5, suspectReceivedCount);
  ASSERT_EQ(0, suspectRemovedCount);
}

TEST_F(SwimServerTests, reconcileReports) {
  ASSERT_NO_FATAL_FAILURE(runServer()) << "Could not get the server started";
  ASSERT_TRUE(server_->isRunning());

  auto svr = MakeServer("localhost", server_->port());
  SwimClient client(*svr, 9200);

  SwimReport report;
  report.mutable_sender()->CopyFrom(client.self());

  auto suspected = report.mutable_suspected();
  ServerRecord *two = suspected->Add();
  two->mutable_server()->set_hostname("host-suspect");
  two->set_timestamp(utils::CurrentTime() - 10);
  two->mutable_server()->set_port(5500);

  ASSERT_TRUE(client.Send(report));
  ASSERT_EQ(1, server_->alive_size());
  ASSERT_EQ(1, server_->suspected_size());

  ASSERT_EQ(1, aliveReceivedCount);
  ASSERT_EQ(1, suspectReceivedCount);

  // Make some time pass so that timestamps genuinely differ.
  std::this_thread::sleep_for(seconds(1));

  report.clear_suspected();

  two = report.mutable_alive()->Add();
  two->mutable_server()->set_hostname("host-suspect");
  two->set_timestamp(utils::CurrentTime());
  two->mutable_server()->set_port(5500);

  ASSERT_TRUE(client.Send(report));
  ASSERT_EQ(2, server_->alive_size());
  ASSERT_EQ(0, server_->suspected_size());

  ASSERT_EQ(2, aliveReceivedCount);
  ASSERT_EQ(1, suspectReceivedCount);
  ASSERT_EQ(1, suspectRemovedCount);
}

TEST_F(SwimServerTests, ignoreStaleReports) {
  ASSERT_NO_FATAL_FAILURE(runServer()) << "Could not get the server started";
  ASSERT_TRUE(server_->isRunning());

  auto svr = MakeServer("localhost", server_->port());
  SwimClient client(*svr, 9200);

  SwimReport report;
  report.mutable_sender()->CopyFrom(client.self());

  auto suspected = report.mutable_suspected();
  ServerRecord *two = suspected->Add();
  two->mutable_server()->set_hostname("host-suspect");
  two->set_timestamp(utils::CurrentTime());
  two->mutable_server()->set_port(5500);

  ASSERT_TRUE(client.Send(report));
  ASSERT_EQ(1, server_->alive_size());
  ASSERT_EQ(1, server_->suspected_size());

  ASSERT_EQ(1, aliveReceivedCount);
  ASSERT_EQ(1, suspectReceivedCount);

  report.clear_suspected();

  two = report.mutable_alive()->Add();
  two->mutable_server()->set_hostname("host-suspect");
  // The information that this server was alive 10 minutes ago is
  // irrelevant and should be ignored.
  two->set_timestamp(utils::CurrentTime() - 600);
  two->mutable_server()->set_port(5500);

  ASSERT_TRUE(client.Send(report));
  ASSERT_EQ(1, server_->alive_size());
  ASSERT_EQ(1, server_->suspected_size());

  ASSERT_EQ(1, aliveReceivedCount);
  ASSERT_EQ(1, suspectReceivedCount);
  ASSERT_EQ(0, suspectRemovedCount);
}

TEST_F(SwimServerTests, ignoreStaleReports2) {
  ASSERT_NO_FATAL_FAILURE(runServer()) << "Could not get the server started";
  ASSERT_TRUE(server_->isRunning());

  auto svr = MakeServer("localhost", server_->port());
  SwimClient client(*svr, 9200);

  SwimReport report;
  report.mutable_sender()->CopyFrom(client.self());

  auto alive = report.mutable_alive();
  ServerRecord *two = alive->Add();
  two->mutable_server()->set_hostname("host-alive");
  two->mutable_server()->set_port(5500);
  two->set_timestamp(utils::CurrentTime());

  ASSERT_TRUE(client.Send(report));
  ASSERT_EQ(2, server_->alive_size());
  ASSERT_EQ(0, server_->suspected_size());

  ASSERT_EQ(2, aliveReceivedCount);
  ASSERT_EQ(0, suspectReceivedCount);
  ASSERT_EQ(0, suspectRemovedCount);

  report.clear_suspected();

  two = report.mutable_suspected()->Add();
  two->mutable_server()->set_hostname("host-alive");
  // The information that this server was suspected 10 minutes ago is
  // irrelevant and should be ignored.
  two->set_timestamp(utils::CurrentTime() - 600);
  two->mutable_server()->set_port(5500);

  ASSERT_TRUE(client.Send(report));
  ASSERT_EQ(2, server_->alive_size());
  ASSERT_EQ(0, server_->suspected_size());

  ASSERT_EQ(2, aliveReceivedCount);
  ASSERT_EQ(0, suspectReceivedCount);
  ASSERT_EQ(0, suspectRemovedCount);
}

TEST_F(SwimServerTests, servesPingRequests) {
  ASSERT_NO_FATAL_FAILURE(runServer()) << "Could not get the server started";
  ASSERT_TRUE(server_->isRunning());

  auto svr = MakeServer("localhost", server_->port());
  SwimClient client(*svr, 9200);

  auto other = MakeServer("fakeserver", 9098);

  // Note we need to release the unique_ptr here, as RequestPing takes
  // ownership.
  ASSERT_TRUE(client.RequestPing(other.release()));

  // This is the sender (us).
  ASSERT_EQ(1, server_->alive_size());

  // We need to wait a bit for the ping to time out.
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  SwimReport report = server_->PrepareReport();

  // This is the fake server.
  ASSERT_EQ(1, report.suspected_size());
  const ServerRecord &record = report.suspected(0);

  ASSERT_EQ(*MakeServer("fakeserver", 9098), record.server());

  ASSERT_EQ(1, aliveReceivedCount);
  ASSERT_EQ(1, suspectReceivedCount);
  ASSERT_EQ(0, suspectRemovedCount);
}

TEST_F(SwimServerTests, canRestart) {
  ASSERT_NO_FATAL_FAILURE(runServer()) << "Could not get the server started";

  ASSERT_TRUE(server_->isRunning());
  auto svr = MakeServer("localhost", server_->port());
  SwimClient client(*svr);
  ASSERT_TRUE(client.Ping());

  server_->stop();

  tests::WaitAtMostFor([&]() -> bool { return !server_->isRunning(); },
                       std::chrono::milliseconds(300));
  ASSERT_FALSE(server_->isRunning());
  std::this_thread::yield();
  thread_->join();

  ASSERT_NO_FATAL_FAILURE(runServer()) << "Could not get the server started";
  ASSERT_TRUE(server_->isRunning());
  ASSERT_TRUE(client.Ping());
}

TEST_F(SwimServerTests, testBudget) {
  ASSERT_NO_FATAL_FAILURE(runServer()) << "Could not get the server started";
  ASSERT_TRUE(server_->isRunning());

  auto svr = MakeServer(server_->self().hostname(), server_->port());

  SwimClient client(*svr, 33456);

  SwimReport report;
  report.mutable_sender()->CopyFrom(client.self());

  auto suspected = report.mutable_suspected();
  for (int i = 0; i < 100; ++i) {
    ServerRecord *two = suspected->Add();
    two->mutable_server()->set_hostname("host-suspect-" + std::to_string(i));
    two->set_timestamp(utils::CurrentTime() - 2000 * i);
    two->mutable_server()->set_port(5500 + i);
  }

  ASSERT_TRUE(client.Send(report));

  // With the current reporting budget, it should cut-off at around two minutes,
  // i.e., less than 30 servers.
  auto returned_report = server_->PrepareReport();
  ASSERT_GT(30, returned_report.suspected_size());
}

TEST(SwimProtocolTests, testForwarding) {
  auto sender_port = ::tests::RandomPort(),
       forwarder_port = ::tests::RandomPort(),
       suspected_port = ::tests::RandomPort();

  int senderAliveReceivedCount = 0;
  int senderSuspectReceivedCount = 0;
  int senderRemovedReceivedCount = 0;
  SwimServer sender(sender_port,
                    [&](std::shared_ptr<ServerRecord>, ServerStatus status) {
                      if (ServerStatus::alive == status) {
                        senderAliveReceivedCount++;
                      }

                      if (ServerStatus::suspect == status) {
                        senderSuspectReceivedCount++;
                      }

                      if (ServerStatus::removed == status) {
                        senderRemovedReceivedCount++;
                      }
                    });

  int forwarderAliveReceivedCount = 0;
  int forwarderSuspectReceivedCount = 0;
  int forwarderRemovedReceivedCount = 0;
  SwimServer forwarder(forwarder_port,
                       [&](std::shared_ptr<ServerRecord>, ServerStatus status) {
                         if (ServerStatus::alive == status) {
                           forwarderAliveReceivedCount++;
                         }

                         if (ServerStatus::suspect == status) {
                           forwarderSuspectReceivedCount++;
                         }

                         if (ServerStatus::removed == status) {
                           forwarderRemovedReceivedCount++;
                         }
                       });

  int suspectedAliveReceivedCount = 0;
  int suspectedSuspectReceivedCount = 0;
  int suspectedRemovedReceivedCount = 0;
  SwimServer suspected(suspected_port,
                       [&](std::shared_ptr<ServerRecord>, ServerStatus status) {
                         if (ServerStatus::alive == status) {
                           suspectedAliveReceivedCount++;
                         }

                         if (ServerStatus::suspect == status) {
                           suspectedSuspectReceivedCount++;
                         }

                         if (ServerStatus::removed == status) {
                           suspectedRemovedReceivedCount++;
                         }
                       });

  for (auto server : {&sender, &forwarder, &suspected}) {
    std::thread t([&server]() { server->start(); });
    t.detach();
    ::tests::WaitAtMostFor([=]() { return server->isRunning(); },
                           std::chrono::milliseconds(200));
  }
  sender.ReportSuspected(suspected.self(), ::utils::CurrentTime());

  // This is necessary, as `set_allocated_xxx()` will cause PB to take ownership
  // of the pointer, and deallocate it when done (which is what will happen when
  // we call `RequestPing()`).
  auto dest = new Server();
  dest->CopyFrom(suspected.self());

  // Before this call, suspected must be in sender's suspected set.
  SwimReport report = sender.PrepareReport();
  ASSERT_EQ(1, report.suspected_size());
  ASSERT_EQ(suspected.self(), report.suspected(0).server());

  ASSERT_EQ(0, senderAliveReceivedCount);
  ASSERT_EQ(1, senderSuspectReceivedCount);
  ASSERT_EQ(0, senderRemovedReceivedCount);

  ASSERT_EQ(0, forwarderAliveReceivedCount);
  ASSERT_EQ(0, forwarderSuspectReceivedCount);
  ASSERT_EQ(0, forwarderRemovedReceivedCount);

  ASSERT_EQ(0, suspectedAliveReceivedCount);
  ASSERT_EQ(0, suspectedSuspectReceivedCount);
  ASSERT_EQ(0, suspectedRemovedReceivedCount);

  SwimClient client(forwarder.self(), sender_port);
  ASSERT_TRUE(client.RequestPing(dest));

  ASSERT_EQ(0, senderAliveReceivedCount);
  ASSERT_EQ(1, senderSuspectReceivedCount);
  ASSERT_EQ(0, senderRemovedReceivedCount);

  ASSERT_EQ(1, forwarderAliveReceivedCount);
  ASSERT_EQ(0, forwarderSuspectReceivedCount);
  ASSERT_EQ(0, forwarderRemovedReceivedCount);

  ASSERT_EQ(0, suspectedAliveReceivedCount);
  ASSERT_EQ(0, suspectedSuspectReceivedCount);
  ASSERT_EQ(0, suspectedRemovedReceivedCount);

  // Upon the "suspected" hearing about the unfair assessment, it should set the
  // record straight.
  ::tests::WaitAtMostFor(
      [&]() {
        report = sender.PrepareReport();
        return report.suspected_size() == 0 && report.alive_size() == 1;
      },
      std::chrono::milliseconds(250));

  ASSERT_EQ(1, senderAliveReceivedCount);
  ASSERT_EQ(1, senderSuspectReceivedCount);
  ASSERT_EQ(1, senderRemovedReceivedCount);

  ASSERT_EQ(1, forwarderAliveReceivedCount);
  ASSERT_EQ(0, forwarderSuspectReceivedCount);
  ASSERT_EQ(0, forwarderRemovedReceivedCount);

  ASSERT_EQ(1, suspectedAliveReceivedCount);
  ASSERT_EQ(0, suspectedSuspectReceivedCount);
  ASSERT_EQ(0, suspectedRemovedReceivedCount);
}

TEST(SwimProtocolTests, testForwardingStaysSuspected) {
  auto sender_port = ::tests::RandomPort(),
       forwarder_port = ::tests::RandomPort(),
       suspected_port = ::tests::RandomPort();

  int senderAliveReceivedCount = 0;
  int senderSuspectReceivedCount = 0;
  int senderRemovedReceivedCount = 0;
  SwimServer sender(sender_port,
                    [&](std::shared_ptr<ServerRecord>, ServerStatus status) {
                      if (ServerStatus::alive == status) {
                        senderAliveReceivedCount++;
                      }

                      if (ServerStatus::suspect == status) {
                        senderSuspectReceivedCount++;
                      }

                      if (ServerStatus::removed == status) {
                        senderRemovedReceivedCount++;
                      }
                    });

  int forwarderAliveReceivedCount = 0;
  int forwarderSuspectReceivedCount = 0;
  int forwarderRemovedReceivedCount = 0;
  SwimServer forwarder(forwarder_port,
                       [&](std::shared_ptr<ServerRecord>, ServerStatus status) {
                         if (ServerStatus::alive == status) {
                           forwarderAliveReceivedCount++;
                         }

                         if (ServerStatus::suspect == status) {
                           forwarderSuspectReceivedCount++;
                         }

                         if (ServerStatus::removed == status) {
                           forwarderRemovedReceivedCount++;
                         }
                       });

  int suspectedAliveReceivedCount = 0;
  int suspectedSuspectReceivedCount = 0;
  int suspectedRemovedReceivedCount = 0;
  SwimServer suspected(suspected_port,
                       [&](std::shared_ptr<ServerRecord>, ServerStatus status) {
                         if (ServerStatus::alive == status) {
                           suspectedAliveReceivedCount++;
                         }

                         if (ServerStatus::suspect == status) {
                           suspectedSuspectReceivedCount++;
                         }

                         if (ServerStatus::removed == status) {
                           suspectedRemovedReceivedCount++;
                         }
                       });

  // We are not starting the "suspected"; that one's a-goner.
  for (auto server : {&sender, &forwarder}) {
    std::thread t([&server]() { server->start(); });
    t.detach();
    ::tests::WaitAtMostFor([=]() { return server->isRunning(); },
                           std::chrono::milliseconds(200));
  }
  sender.ReportSuspected(suspected.self(), ::utils::CurrentTime());

  ASSERT_EQ(0, senderAliveReceivedCount);
  ASSERT_EQ(1, senderSuspectReceivedCount);
  ASSERT_EQ(0, senderRemovedReceivedCount);

  ASSERT_EQ(0, forwarderAliveReceivedCount);
  ASSERT_EQ(0, forwarderSuspectReceivedCount);
  ASSERT_EQ(0, forwarderRemovedReceivedCount);

  ASSERT_EQ(0, suspectedAliveReceivedCount);
  ASSERT_EQ(0, suspectedSuspectReceivedCount);
  ASSERT_EQ(0, suspectedRemovedReceivedCount);

  // This is necessary, as `set_allocated_xxx()` will cause PB to take ownership
  // of the pointer, and deallocate it when done (which is what will happen when
  // we call `RequestPing()`).
  auto dest = new Server();
  dest->CopyFrom(suspected.self());

  // Before this call, suspected must be in sender's suspected set.
  SwimReport report = sender.PrepareReport();
  ASSERT_EQ(1, report.suspected_size());
  ASSERT_EQ(suspected.self(), report.suspected(0).server());

  SwimClient client(forwarder.self(), sender_port);
  ASSERT_TRUE(client.RequestPing(dest));

  ASSERT_EQ(0, senderAliveReceivedCount);
  ASSERT_EQ(1, senderSuspectReceivedCount);
  ASSERT_EQ(0, senderRemovedReceivedCount);

  ASSERT_EQ(1, forwarderAliveReceivedCount);
  ASSERT_EQ(0, forwarderSuspectReceivedCount);
  ASSERT_EQ(0, forwarderRemovedReceivedCount);

  ASSERT_EQ(0, suspectedAliveReceivedCount);
  ASSERT_EQ(0, suspectedSuspectReceivedCount);
  ASSERT_EQ(0, suspectedRemovedReceivedCount);

  // Now, the news has spread to the "forwarder," and stays like that at the
  // "sender."
  ::tests::WaitAtMostFor(
      [&]() {
        SwimReport report = sender.PrepareReport();
        SwimReport report2 = forwarder.PrepareReport();
        return report.suspected_size() == 1 && report2.suspected_size() == 1;
      },
      std::chrono::milliseconds(250));

  ASSERT_EQ(0, senderAliveReceivedCount);
  ASSERT_EQ(1, senderSuspectReceivedCount);
  ASSERT_EQ(0, senderRemovedReceivedCount);

  ASSERT_EQ(1, forwarderAliveReceivedCount);
  ASSERT_EQ(1, forwarderSuspectReceivedCount);
  ASSERT_EQ(0, forwarderRemovedReceivedCount);

  ASSERT_EQ(0, suspectedAliveReceivedCount);
  ASSERT_EQ(0, suspectedSuspectReceivedCount);
  ASSERT_EQ(0, suspectedRemovedReceivedCount);
}
