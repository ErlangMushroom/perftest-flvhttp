#ifndef TEST_CONFIG_HH_INCLUDED
#define TEST_CONFIG_HH_INCLUDED

#include <string>
#include <boost/foreach.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/program_options.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

using namespace boost::program_options;

class TestConfig {
public:
  static const int DEFAULT_RECV_LENGTH = 8 * 1024 * 1024;

  TestConfig()
    : _ready(false)
    , _clients(1)
    , _recvLen(DEFAULT_RECV_LENGTH)
    , _interval(0)
    , _timeout(10)
    , _detail(false) {
  }

  TestConfig(int argc, char* argv[])
    : _ready(false)
    , _clients(1)
    , _recvLen(DEFAULT_RECV_LENGTH)
    , _interval(0)
    , _timeout(10)
    , _detail(false) {
      Prepare(argc, argv);
  }

  bool IsReady() const {
    return _ready;
  }

  void PrintHelp() const {
    std::cout << _helpMessage << std::endl;
  }

  size_t Clients() const {
    return _clients;
  }

  size_t MaxRecvLength() const {
    return _recvLen;
  }

  int32_t Interval() const {
    return _interval;
  }

  int32_t Timeout() const {
    return _timeout;
  }

  bool Detailed() const {
    return _detail;
  }

  class URLIterator {
  public:
    URLIterator(size_t total) : _totalURL(total) {}

    URLIterator& operator++(int) {
      _counter ++;
      return *this;
    }

    operator size_t() const {
      return _counter % _totalURL;
    }

  private:
    size_t _counter;
    size_t _totalURL;
  };

  URLIterator GetURLIterator() const {
    return URLIterator(_urlVec.size());
  }

  std::string GetNextURL(const URLIterator& it) const {
    size_t i = it;
    size_t total = _urlVec.size();
    if (!total)
      return std::string();
    return _urlVec[i % total];
  }

protected:
  void Prepare(int argc, char* argv[]) {
    options_description opt;
    opt.add_options()
      ("clients,n", value<size_t>(), "number of testing clients")
      ("recvlen,r", value<size_t>(), "max content length should be received (bytes)")
      ("interval,i", value<int32_t>(), "interval of connection (us)")
      ("urls,u", value<std::string>(), "testing url")
      ("timeout,t", value<int32_t>(), "max timeout for no-data-duration (s)")
      ("config,c", value<std::string>(), "input json config")
      ("detail,d", "produce detailed statistic data (in csv format)");

    std::stringstream ss;
    ss << "perftest [OPTION]...\n";
    ss << opt;
    _helpMessage = ss.str();

    variables_map vmap;
    try {
      store(parse_command_line(argc, argv, opt), vmap);
      notify(vmap);
    } catch (std::exception& ex) {
      return;
    }

    std::vector<std::string> urlVec1, urlVec2;
    if (vmap.count("config")) {
      std::string cfgFile = vmap["config"].as<std::string>();
      try {
        boost::property_tree::ptree root;
        boost::property_tree::json_parser::read_json(cfgFile, root);
        if (root.find("clients") != root.not_found()) {
          _clients = root.get<size_t>("clients");
        }
        if (root.find("recvlen") != root.not_found()) {
          _recvLen = root.get<size_t>("recvlen");
        }
        if (root.find("interval") != root.not_found()) {
          _interval = root.get<int32_t>("interval");
        }
        if (root.find("urls") != root.not_found()) {
          BOOST_FOREACH (boost::property_tree::ptree::value_type& url
               , root.get_child("urls")) {
            urlVec1.push_back(url.second.data());
          }
        }
        if (root.find("timeout") != root.not_found()) {
          _timeout = root.get<int32_t>("timeout");
        }
        if (root.find("detail") != root.not_found()) {
          _detail = root.get<bool>("detail");
        }
      } catch (boost::property_tree::json_parser::json_parser_error& err) {
        std::cout << "error when parsing " << cfgFile << "\n";
      }
    }
    if (vmap.count("clients")) {
      _clients = vmap["clients"].as<size_t>();
    }
    if (vmap.count("recvlen")) {
      _recvLen = vmap["recvlen"].as<size_t>();
    }
    if (vmap.count("interval")) {
      _interval = vmap["interval"].as<int32_t>();
    }
    if (vmap.count("urls")) {
      std::string urls = vmap["urls"].as<std::string>();
      boost::split(urlVec2, urls, boost::is_any_of(",  \n\t"),
                   boost::token_compress_on);
    }
    if (vmap.count("timeout")) {
      _timeout = vmap["timeout"].as<int32_t>();
    }
    if (vmap.count("detail")) {
      _detail = true;
    }

    std::merge(urlVec1.begin(), urlVec1.end(),
               urlVec2.begin(), urlVec2.end(), std::back_inserter(_urlVec));

    _ready = !_urlVec.empty();
  }

private:
  std::string _helpMessage;
  std::vector<std::string> _urlVec;
  bool _ready;
  size_t _clients;
  size_t _recvLen;
  int32_t _interval;
  int32_t _timeout;
  bool _detail;
};

#endif // TEST_CONFIG_HH_INCLUDED
