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

class ApiServer {
public:
  explicit ApiServer(unsigned short port);
  void start();
  void addGet(const std::string &target, ApiFunctionType func);
  void addPost(const std::string &target, ApiFunctionType func);

private:
  unsigned short port;
  boost::asio::io_context ioc;
  boost::asio::ip::tcp::acceptor acceptor;
  std::map<std::string, ApiFunctionType> posts;
  std::map<std::string, ApiFunctionType> gets;
  response handleRequest(request &&req);
  void doRequest(boost::asio::ip::tcp::socket &socket);
};
} // namespace api::rest