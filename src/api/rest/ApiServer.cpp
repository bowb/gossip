#include <api/rest/ApiServer.hpp>
#include <map>
#include <memory>
#include <string>
#include <thread>

using tcp = boost::asio::ip::tcp;
namespace http = boost::beast::http;

namespace api::rest {

void fail(boost::beast::error_code ec, char const *what) {
  std::cerr << what << ": " << ec.message() << "\n";
}

ApiServer::ApiServer(unsigned short port)
    : port(port), ioc{1}, acceptor{ioc, {tcp::v4(), port}}, posts(), gets() {}

// Handles an HTTP server connection
void ApiServer::doRequest(tcp::socket &socket) {
  boost::beast::error_code ec;
  boost::beast::flat_buffer buffer;
  request req;

  http::read(socket, buffer, req, ec);

  if (ec) {
    return fail(ec, "read");
  }

  auto response = handleRequest(std::move(req));

  // Send the response
  http::write(socket, std::move(response), ec);

  if (ec) {
    return fail(ec, "write");
  }

  socket.shutdown(tcp::socket::shutdown_send, ec);

  if (ec) {
    fail(ec, "shutdown");
  }
}

void ApiServer::start() {

  boost::beast::error_code ec;

  for (;;) {
    auto socket = tcp::socket{ioc};
    acceptor.accept(socket);
    std::thread{std::bind(&ApiServer::doRequest, this, std::move(socket))}
        .detach();
  }
}

response ApiServer::handleRequest(request &&request) {

  boost::beast::error_code ec;

  const auto target = std::string{request.target().substr(1)};
  const auto verb = request.method();

  response response;
  response.version(request.version());
  response.result(http::status::bad_request);

  switch (verb) {
  case http::verb::post: {
    auto it = posts.find(target);
    if (it != posts.end()) {
      auto [key, value] = *it;
      auto r = value(request);
      response = r;
    }
  } break;
  case http::verb::get: {
    auto it = gets.find(target);
    if (it != gets.end()) {
      auto [key, value] = *it;
      auto r = value(request);
      response = r;
    }
  } break;
  default:
    break;
  }

  response.set(http::field::server, "apierver");
  response.prepare_payload();

  return response;
}

void ApiServer::addGet(const std::string &target, ApiFunctionType func) {
  gets[target] = func;
}

void ApiServer::addPost(const std::string &target, ApiFunctionType func) {
  posts[target] = func;
}

} // namespace api::rest