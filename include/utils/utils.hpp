#pragma once

#include <glog/types.h>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include <boost/any.hpp>

namespace utils {

/********************* Exception Classes ************************************/

/**
 * Base class for all exceptions in this library.
 */
class base_error : public std::exception {
protected:
  std::string what_;

public:
  explicit base_error(std::string error) : what_(std::move(error)) {}

#if defined(__linux__)
  virtual const char *what() const _GLIBCXX_NOTHROW{
#elif defined(__APPLE__)
  virtual const char *what() const _NOEXCEPT {
#endif
      return what_.c_str();
}
}; // namespace utils

/**
 * Marker exception for unimplemented methods.
 */
class not_implemented : public base_error {
public:
  explicit not_implemented(const std::string &method_or_class)
      : base_error{method_or_class + " not implemented"} {}
};

// RegEx pattern for use when parsing IP/port strings, defined in misc.cpp.
extern const std::regex kIpPortPattern;
extern const std::regex kHostPortPattern;
extern const std::regex kIpPattern;

class parse_error : public base_error {
public:
  explicit parse_error(const std::string &error) : base_error{error} {}
};

struct list_option {
  std::vector<std::string> values;
};

void validate(boost::any &v, const std::vector<std::string> &tokens,
              list_option *, int);

/**
 * Splits an ip:port string into its component parts, also ensuring that they
 * are correctly formed.
 *
 * @param ipPort a string of the form `IP:port`, as in " "192.168.51.123:8084".
 * @return the parsed `{ip, port}` tuple.
 * @throws parse_error if the string is not correctly formatted.
 */
std::tuple<std::string, unsigned int> ParseIpPort(const std::string &ipPort);

/**
 * Splits a `host:port` string into its component parts.
 *
 * @param hostPort a string of the form `host:port`, as in "
 * "host.example.com:8084".
 * @return the parsed `{host, port}` tuple.
 * @throws parse_error if the string is not correctly formatted.
 */
std::tuple<std::string, unsigned int>
ParseHostPort(const std::string &hostPort);

/**
 * Parses a string and checks whether it is a valid IP address.
 *
 * param ip
 * @return `true` if `ip` is an IP address.
 * @deprecated Bad method name, use `IsValidIp()` instead
 */
inline bool ParseIp(const std::string &ip) {
  return std::regex_match(ip, kIpPattern);
}

/**
 * Removes leading and trailing spaces from the string.
 *
 * @param str
 * @return the `str` without leading and trailing spaces, if any
 */
std::string trim(const std::string &str);

/**
 * Splits a `sep`-separated string into its components part.
 *
 * <p>For example using the default ',' comma separator, it would return
 * `"one,two,three"` as a three-element vector `{"one", "two", "three"}`.
 *
 * It can optionaly skip empty values (by default, they are returned as empty
 * strings) and remove leading and trailing spaces around the values (the
 * default behavior).
 *
 * Spaces inside the values (unless space is used as `sep`) are preserved.
 *
 * @param values the string containing 0 or more string separated by `sep`
 * @param sep the separator; by defaul the comma (`,`) but can be any string
 * @param trim_spaces if `true` (the default) spaces around the values are
 * trimmed
 * @param preserve_empty if `true` (the default) consecutive `sep` strings will
 * be considered as a single separator (in other words, `"one,two,,,,three"`
 * will return a three-elements vector
 *
 * @return a vector of string containing the values, possibly empty.
 */
std::vector<std::string> split(const std::string &values,
                               const std::string &sep = ",",
                               bool trim_spaces = true,
                               bool preserve_empty = true);

/**
 * Convenience method, can be used by projects using this library to emit their
 * version string, alongside the library version.
 *
 * @param server_name the name for the server that uses this library
 * @param version the server's version
 * @param out the stream to write out the version information (by default,
 * `stdout`)
 * @return the same stream that was passed in, for ease of chaining
 */
std::ostream &PrintVersion(const std::string &server_name,
                           const std::string &version,
                           std::ostream &out = std::cout);

/**
 * Returns the current time in a format suitable for use as a timestamp field
 * in a Protobuf.
 *
 * @return the current time (via `std::time()`).
 */
google::uint64 CurrentTime();

/**
 * Returns the IP address of the host whose name is `hostname`; if `hostname` is
 * empty, the IP address of this server will be returned.
 *
 * @param hostname the host whose IP address we want to resolve.
 * @return the IP address, in human-readable format.
 */
std::string InetAddress(const std::string &host = "");

/**
 * Tries to find this node's hostname, as returned by the OS.
 *
 * @return the hostname, if available.
 */
std::string Hostname();

/**
 * Given a port number will return a suitable socket address in string
 * format for the server to be listening on (e.g., "tcp://0.0.0.0:5000").
 *
 * @param port the host port the server would like to listen to
 * @return a string suitable for use with ZMQ `socket()` call.
 *      Or an empty string, if no suitable socket is available.
 */
std::string SocketAddress(unsigned int port);

/**
 * Splits an ip:port string into its component parts, also ensuring that they
 * are correctly formed.
 *
 * @param ipPort a string of the form `IP:port`, as in " "192.168.51.123:8084".
 * @return the parsed `{ip, port}` tuple.
 * @throws parse_error if the string is not correctly formatted.
 */
std::tuple<std::string, unsigned int> ParseIpPort(const std::string &ipPort);

/**
 * Splits a `host:port` string into its component parts.
 *
 * @param hostPort a string of the form `host:port`, as in "
 * "host.example.com:8084".
 * @return the parsed `{host, port}` tuple.
 * @throws parse_error if the string is not correctly formatted.
 */
std::tuple<std::string, unsigned int>
ParseHostPort(const std::string &hostPort);

/**
 * Converts a vector to a string, the concatenation of its element, separated by
 * `sep`. Only really useful for debugging purposes.
 *
 * @tparam T the type of the elements, MUST support emitting to an `ostream` via
 * the `<<` operator
 * @param vec the vector whose element we are emitting
 * @param sep a separator string, by default a newline
 * @return the string resulting from the concatenation of all elements
 */
template <typename T>
inline std::string Vec2Str(const std::vector<T> &vec,
                           const std::string &sep = "\n") {
  bool first = true;
  std::ostringstream out;

  for (auto t : vec) {
    if (!first)
      out << sep;
    else
      first = false;
    out << t;
  }
  return out.str();
}

} // namespace utils
