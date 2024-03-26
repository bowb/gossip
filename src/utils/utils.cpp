#include <utils/utils.hpp>

#include "version.h"
#include <algorithm>
#include <arpa/inet.h>
#include <glog/logging.h>
#include <memory>
#include <netdb.h>
#include <unistd.h>
#include <vector>

#include <boost/any.hpp>
#include <boost/program_options.hpp>
#include <boost/tokenizer.hpp>

namespace utils {

void validate(boost::any &v, const std::vector<std::string> &tokens,
              list_option *, int) {
  if (v.empty()) {
    v = boost::any(list_option());
  }

  list_option *p = boost::any_cast<list_option>(&v);

  BOOST_ASSERT(p);
  boost::char_separator<char> sep(" ");
  for (std::string const &t : tokens) {
    if (t.find(",")) {
      // tokenize values and push them back onto p->values
      boost::tokenizer<boost::char_separator<char>> tok(t, sep);
      std::copy(tok.begin(), tok.end(), std::back_inserter(p->values));
    } else {
      // store value as is
      p->values.push_back(t);
    }
  }
}

// RegEx pattern for use when parsing IP/port strings, defined in misc.cpp.
const std::regex kIpPortPattern{R"((\d{1,3}(\.\d{1,3}){3}):(\d+))"};
const std::regex kHostPortPattern{R"((\S+(\.\S+)*):(\d+))"};
const std::regex kIpPattern{R"(\d{1,3}(\.\d{1,3}){3})"};

std::tuple<std::string, unsigned int> ParseIpPort(const std::string &ipPort) {

  std::smatch matches;
  if (std::regex_match(ipPort, matches, kIpPortPattern)) {
    return std::make_tuple(matches[1], atoi(matches[3].str().c_str()));
  }

  throw parse_error("Not a valid IP:port string: " + ipPort);
}

std::tuple<std::string, unsigned int>
ParseHostPort(const std::string &hostPort) {

  std::smatch matches;
  if (std::regex_match(hostPort, matches, kHostPortPattern)) {
    return std::make_tuple(matches[1], atoi(matches[3].str().c_str()));
  }
  throw parse_error("Not a valid host:port string: " + hostPort);
}

std::string trim(const std::string &str) {

  const auto strBegin = str.find_first_not_of(' ');

  if (strBegin == std::string::npos)
    return ""; // no content

  const auto strEnd = str.find_last_not_of(' ');
  const auto strRange = strEnd - strBegin + 1;

  return str.substr(strBegin, strRange);
}

std::vector<std::string> split(const std::string &values,
                               const std::string &sep, bool trim_spaces,
                               bool preserve_empty) {
  auto remains = values;
  std::vector<std::string> results;
  std::string::size_type pos;

  while ((pos = remains.find(sep)) != std::string::npos) {
    std::string value = remains.substr(0, pos);
    if (trim_spaces) {
      value = trim(value);
    }
    if (!value.empty() || preserve_empty) {
      results.push_back(value);
    }
    remains = remains.substr(pos + 1);
  }

  if (!remains.empty()) {
    results.push_back(remains);
  }
  return results;
}

std::ostream &PrintVersion(const std::string &server_name,
                           const std::string &version, std::ostream &out) {
  out << server_name << " Ver. " << version << " (libdist ver. " << RELEASE_STR
      << ")" << std::endl;
  return out;
}

google::uint64 CurrentTime() {
  return static_cast<google::uint64>(std::time(nullptr));
}

std::string Hostname() {
  char name[NI_MAXHOST];
  if (gethostname(name, NI_MAXHOST) != 0) {
    LOG(ERROR) << "Could not determine hostname";
    return "";
  }
  return std::string(name);
}

std::string InetAddress(const std::string &host) {
  char ip_addr[INET6_ADDRSTRLEN];
  struct addrinfo hints;
  struct addrinfo *result;
  std::string retval;

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = AF_UNSPEC;
  hints.ai_flags = AI_ADDRCONFIG | AI_NUMERICSERV;

  int resp = getaddrinfo(host.empty() ? Hostname().c_str() : host.c_str(),
                         nullptr, &hints, &result);
  if (resp) {
    LOG(ERROR) << "Cannot find IP address for '" << host
               << "': " << gai_strerror(resp);
  } else {
    // Iterate the addr_info fields, until we find an IPv4 field
    for (auto rp = result; rp != nullptr; rp = rp->ai_next) {
      if (rp->ai_family == AF_INET) {
        if (inet_ntop(AF_INET, &((struct sockaddr_in *)rp->ai_addr)->sin_addr,
                      ip_addr, INET6_ADDRSTRLEN)) {
          retval = ip_addr;
          break;
        }
      }
    }
  }

  if (result != nullptr) {
    freeaddrinfo(result);
  }
  return retval;
}

std::string SocketAddress(unsigned int port) {
  struct addrinfo hints;
  struct addrinfo *result;

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = AF_INET;
  hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;

  std::string retval;
  std::string port_str = std::to_string(port);

  if (!getaddrinfo(nullptr, port_str.c_str(), &hints, &result) &&
      result != nullptr) {
    // Simply return the first available socket.
    char host[NI_MAXHOST];
    if (getnameinfo(result->ai_addr, result->ai_addrlen, host, NI_MAXHOST,
                    nullptr, 0, NI_NUMERICHOST) == 0) {
      std::ostringstream ostream;
      ostream << "tcp://" << host << ":" << port;
      retval = ostream.str();
    }
    freeaddrinfo(result);
  }

  return retval;
}

} // namespace utils
