#pragma once

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <thread>

namespace api::rest {

namespace http = boost::beast::http;
using request = http::request<boost::beast::http::string_body>;
using response = http::response<boost::beast::http::string_body>;
using ApiFunctionType = std::function<response(const request)>;

class ApiServer;

class HttpConnection : public std::enable_shared_from_this<HttpConnection> {
public:
  explicit HttpConnection(ApiServer *server,
                          boost::asio::ip::tcp::socket socket);
  void doRequest();

private:
  ApiServer *server;
  boost::asio::ip::tcp::socket socket;
  boost::asio::streambuf buffer;
  request req;
  response res;

  void handleRequest(boost::beast::error_code ec);
  void respond();
};

class ApiServer : public std::enable_shared_from_this<ApiServer> {
public:
  explicit ApiServer(unsigned short port);
  virtual ~ApiServer();
  void start();
  void addGet(const std::string &target, ApiFunctionType func);
  void addPost(const std::string &target, ApiFunctionType func);
  friend class HttpConnection;

private:
  unsigned short port;
  boost::asio::io_context ioc;
  boost::asio::ip::tcp::acceptor acceptor;
  boost::asio::ip::tcp::socket socket;
  std::map<std::string, ApiFunctionType> posts;
  std::map<std::string, ApiFunctionType> gets;

  void accept();
  std::atomic<bool> stop{false};
  std::thread work;
};
} // namespace api::rest