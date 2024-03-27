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

HttpConnection::HttpConnection(ApiServer *server, tcp::socket socket)
    : server(server), socket(std::move(socket)) {}

void HttpConnection::doRequest() {

  auto lifetime = shared_from_this();

  http::async_read(socket, buffer, req,
                   [this, lifetime](boost::beast::error_code ec, std::size_t) {
                     if (ec) {
                       fail(ec, "read");
                     }
                     handleRequest(ec);
                   });
}

void HttpConnection::handleRequest(boost::beast::error_code ec) {

  auto prefix = false;
  if (req.target().rfind("/api/v1") == 0) {
    prefix = true;
  }

  const auto target =
      prefix ? std::string{req.target().substr(std::string{"/api/v1/"}.size())}
             : std::string{req.target().substr(1)};
  const auto verb = req.method();

  res.version(req.version());
  res.result(http::status::not_found);
  res.body() = "Unknown API endpoint";
  if (ec) {
    res.result(http::status::bad_request);
    res.body() = ec.what();
  }

  if (!ec) {
    switch (verb) {
    case http::verb::post: {
      auto it = server->posts.find(target);
      if (it != server->posts.end()) {
        auto [key, value] = *it;
        auto r = value(req);
        res = r;
      }
    } break;
    case http::verb::get: {
      auto it = server->gets.find(target);
      if (it != server->gets.end()) {
        auto [key, value] = *it;
        auto r = value(req);
        res = r;
      }
    } break;
    default:
      break;
    }
  }

  res.set(http::field::server, "apierver");
  res.prepare_payload();

  respond();
}

void HttpConnection::respond() {
  auto lifetime = shared_from_this();

  http::async_write(socket, res,
                    [this, lifetime](boost::beast::error_code ec, std::size_t) {
                      if (ec) {
                        fail(ec, "write");
                      }
                      socket.shutdown(tcp::socket::shutdown_send, ec);
                      if (ec) {
                        fail(ec, "shutdown");
                      }
                    });
}

ApiServer::ApiServer(unsigned short port)
    : port(port), ioc{1}, acceptor{ioc, {tcp::v4(), port}}, socket(ioc),
      posts(), gets() {}

ApiServer::~ApiServer() {
  stop = true;
  ioc.stop();
  acceptor.close();
  work.join();
}

void ApiServer::accept() {

  acceptor.async_accept(socket, [this](boost::beast::error_code ec) {
    if (!ec) {
      std::make_shared<HttpConnection>(this, std::move(socket))->doRequest();
    }
    accept();
  });
}

void ApiServer::start() {

  work = std::thread([this] {
    accept();

    ioc.run();
  });
}

void ApiServer::addGet(const std::string &target, ApiFunctionType func) {
  gets[target] = func;
}

void ApiServer::addPost(const std::string &target, ApiFunctionType func) {
  posts[target] = func;
}

} // namespace api::rest