#ifndef HTTP_PLAYSESSION_HH_INCLUDED
#define HTTP_PLAYSESSION_HH_INCLUDED

#include <iostream>
#include <istream>
#include <ostream>
#include <string>
#include <algorithm>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/chrono/include.hpp>
#include "play_session.hh"
#include "url.hpp"

using boost::asio::ip::tcp;

class HTTPPlaySession : public PlaySession {
public:
  static const int RECV_BLOCK_SIZE = 10 * 1024;
  static const int STATS_WINDOW_SIZE = 1024 * 1024;

  enum HTTPErrorCode {
    ERROR_BASE = HTTP_ERROR_BASE,
    ERROR_ON_RESOLVE,
    ERROR_ON_CONNECT,
    ERROR_ON_REQUEST,
    ERROR_ON_RECV,
    ERROR_BAD_HTTP,
    ERROR_TIMEOUT_FOR_NO_DATA,
    ERROR_MAX
  };

  HTTPPlaySession(Observable* obs,
                  boost::asio::io_service& ioServ,
                  const boost::shared_ptr<Summary>& sum,
                  const urdl::url& url,
                  int32_t timeout)
      : _observer(obs)
      , _resolver(ioServ)
      , _socket(ioServ)
      , _timer(ioServ)
      , _sum(sum)
      , _contentBytes(0)
      , _statsBytes(0)
      , _timeout(timeout)
      , _url(url.to_string()) {
    std::ostream request_stream(&_request);
    std::string path = url.query().empty() ?
                         url.path() : url.path() + "?" + url.query();
    request_stream << "GET " << path << " HTTP/1.1\r\n";
    request_stream << "User-Agent: "
                   << "Mozilla/5.0 (Windows NT 6.1; WOW64)\r\n";
    request_stream << "Host: " << url.host() << "\r\n";
    request_stream << "Accept: */*\r\n";
    request_stream << "Connection: keep-alive\r\n\r\n";

    boost::system::error_code ec;
    boost::asio::ip::address addr =
      boost::asio::ip::address::from_string(url.host().c_str(), ec);
    if (!ec) {
      tcp::endpoint endpoint = tcp::endpoint(addr,
        url.port() ? url.port() : 80);
      _checkPoint = boost::chrono::system_clock::now();
      _socket.async_connect(endpoint,
        boost::bind(&HTTPPlaySession::HandleConnectIP, this,
          boost::asio::placeholders::error));

      return;
    }

    tcp::resolver::query query(url.host(), url.protocol());
    _checkPoint = boost::chrono::system_clock::now();
    _resolver.async_resolve(query,
      boost::bind(&HTTPPlaySession::HandleResolve, this,
        boost::asio::placeholders::error,
        boost::asio::placeholders::iterator));
  }

  virtual void Disconnect() {
    if (_socket.is_open()) {
      //_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both);
      //boost::asio::socket_base::linger option(true, 0);
      //_socket.set_option(option);
      _socket.close();
    }
    _timer.cancel();
  }

  virtual std::string GetPlayURL() const {
    return _url;
  }

  const boost::shared_ptr<Summary>& GetSummary() const {
    return _sum;
  }

protected:

  void HandleResolve(const boost::system::error_code& err,
                     tcp::resolver::iterator endpoint_iterator) {
    if (!err) {
      boost::chrono::milliseconds elapsed =
        boost::chrono::duration_cast<boost::chrono::milliseconds>(
          boost::chrono::system_clock::now() - _checkPoint);
      _observer->OnResolved(this, elapsed.count());
      _checkPoint = boost::chrono::system_clock::now();

      tcp::endpoint endpoint = *endpoint_iterator;
      _socket.async_connect(endpoint,
        boost::bind(&HTTPPlaySession::HandleConnect, this,
          boost::asio::placeholders::error, ++endpoint_iterator));
    } else {
      _observer->OnError(this, ERROR_ON_RESOLVE);
    }
  }

  void HandleConnectIP(const boost::system::error_code& err) {
    if (!err) {
      boost::chrono::milliseconds elapsed =
        boost::chrono::duration_cast<boost::chrono::milliseconds>(
          boost::chrono::system_clock::now() - _checkPoint);
      _observer->OnConnected(this, elapsed.count());
      _checkPoint = boost::chrono::system_clock::now();

      boost::asio::async_write(_socket, _request,
        boost::bind(&HTTPPlaySession::HandleRequest, this,
          boost::asio::placeholders::error));

    } else {
      _observer->OnError(this, ERROR_ON_CONNECT);
    }
  }

  void HandleConnect(const boost::system::error_code& err,
                     tcp::resolver::iterator endpoint_iterator) {
    if (!err) {
      boost::chrono::milliseconds elapsed =
        boost::chrono::duration_cast<boost::chrono::milliseconds>(
          boost::chrono::system_clock::now() - _checkPoint);
      _observer->OnConnected(this, elapsed.count());

      boost::asio::async_write(_socket, _request,
        boost::bind(&HTTPPlaySession::HandleRequest, this,
          boost::asio::placeholders::error));

    } else if (endpoint_iterator != tcp::resolver::iterator()) {
      _socket.close();

      tcp::endpoint endpoint = *endpoint_iterator;
      _socket.async_connect(endpoint,
        boost::bind(&HTTPPlaySession::HandleConnect, this,
          boost::asio::placeholders::error, ++endpoint_iterator));

    } else {
      _observer->OnError(this, ERROR_ON_CONNECT);
    }
  }

  void HandleRequest(const boost::system::error_code& err) {
    if (!err) {
      _checkPoint = boost::chrono::system_clock::now();
      _timer.expires_from_now(boost::posix_time::seconds(_timeout));
      _timer.async_wait(boost::bind(&HTTPPlaySession::HandleTimeout, this, _contentBytes));

      boost::asio::async_read_until(_socket, _response, "\r\n\r\n",
        boost::bind(&HTTPPlaySession::HandleRecvHeader, this,
          boost::asio::placeholders::error));

    } else if (_socket.is_open()) {
      _observer->OnError(this, ERROR_ON_REQUEST);
    }
  }

  void HandleRecvHeader(const boost::system::error_code& err) {
    if (!err) {
      std::istream stream(&_response);
      std::string httpVersion;
      stream >> httpVersion;
      uint32_t statusCode;
      stream >> statusCode;

      boost::chrono::milliseconds elapsed =
        boost::chrono::duration_cast<boost::chrono::milliseconds>(
          boost::chrono::system_clock::now() - _checkPoint);
      _observer->OnRecvHeader(this, elapsed.count());

      if (!stream || httpVersion.substr(0, 5) != "HTTP/") {
        _observer->OnError(this, ERROR_BAD_HTTP);
        return;
      }

      if (statusCode != 200) {
        std::cout << "http resp code: " << statusCode << std::endl;
        _observer->OnError(this, ERROR_BAD_HTTP);
        return;
      }

      size_t blocksize = _response.size();
      _response.consume(blocksize);

      boost::asio::async_read(_socket, _response,
        boost::asio::transfer_exactly(16),
        boost::bind(&HTTPPlaySession::HandleFirstChunk, this,
          boost::asio::placeholders::error));

    } else if (_socket.is_open()) {
      _observer->OnError(this, ERROR_ON_RECV);
    }
  }

  void HandleFirstChunk(const boost::system::error_code& err) {
    if (!err) {
      boost::chrono::milliseconds elapsed =
        boost::chrono::duration_cast<boost::chrono::milliseconds>(
          boost::chrono::system_clock::now() - _checkPoint);
      _observer->OnFirstChunk(this, elapsed.count());

      size_t blocksize = _response.size();
      _response.consume(blocksize);

      if (blocksize) {
        _contentBytes += blocksize;
        _observer->OnTotalBytes(this, _contentBytes);
      }
      _checkPoint = boost::chrono::system_clock::now();

      boost::asio::async_read(_socket, _response,
        boost::asio::transfer_exactly(RECV_BLOCK_SIZE),
        boost::bind(&HTTPPlaySession::HandleContent, this,
          boost::asio::placeholders::error));

    } else if (err == boost::asio::error::eof) {
      size_t blocksize = _response.size();
      _response.consume(blocksize);
      _observer->OnFinished(this);

    } else if (_socket.is_open()) {
      _observer->OnError(this, ERROR_ON_RECV);
    }
  }

  void HandleContent(const boost::system::error_code& err) {
    if (!err) {
      size_t blocksize = _response.size();
      _response.consume(blocksize);

      if (blocksize) {
        _contentBytes += blocksize;
        _statsBytes += blocksize;

        if (_statsBytes > STATS_WINDOW_SIZE) {
          boost::chrono::milliseconds duration =
            boost::chrono::duration_cast<boost::chrono::milliseconds>(
              boost::chrono::system_clock::now() - _checkPoint);

          _observer->OnContent(this, _statsBytes,
                               std::max(1LL, (long long)duration.count()));

          _checkPoint = boost::chrono::system_clock::now();
          _statsBytes = 0;
        }
      }

      _observer->OnTotalBytes(this, _contentBytes);

      boost::asio::async_read(_socket, _response,
        boost::asio::transfer_exactly(RECV_BLOCK_SIZE),
        boost::bind(&HTTPPlaySession::HandleContent, this,
          boost::asio::placeholders::error));

    } else if (err == boost::asio::error::eof) {
      size_t blocksize = _response.size();
      _response.consume(blocksize);

      if (blocksize) {
        _contentBytes += blocksize;
        _statsBytes += blocksize;

        if (_statsBytes > STATS_WINDOW_SIZE) {
          boost::chrono::milliseconds duration =
            boost::chrono::duration_cast<boost::chrono::milliseconds>(
              boost::chrono::system_clock::now() - _checkPoint);

          _observer->OnContent(this, _statsBytes,
                               std::max(1LL, (long long)duration.count()));

          _checkPoint = boost::chrono::system_clock::now();
          _statsBytes = 0;
        }
      }
      _observer->OnFinished(this);

    } else if (_socket.is_open()) {
      _observer->OnError(this, ERROR_ON_RECV);
    }
  }

  void HandleTimeout(size_t bytes) {
    if (!_socket.is_open()) {
      return;
    }

    if (_contentBytes == bytes) {
      _observer->OnError(this, ERROR_TIMEOUT_FOR_NO_DATA);
      Disconnect();
    } else {
      _timer.expires_from_now(boost::posix_time::seconds(_timeout));
      _timer.async_wait(boost::bind(&HTTPPlaySession::HandleTimeout, this, _contentBytes));
    }
  }

private:
  Observable* _observer;
  boost::shared_ptr<Summary> _sum;
  tcp::resolver _resolver;
  tcp::socket _socket;
  boost::asio::deadline_timer _timer;
  boost::asio::streambuf _request;
  boost::asio::streambuf _response;
  boost::chrono::time_point<boost::chrono::system_clock> _checkPoint;
  size_t _contentBytes;
  size_t _statsBytes;
  int32_t _timeout;
  std::string _url;
};

#endif // HTTP_PLAYSESSION_HH_INCLUDED
