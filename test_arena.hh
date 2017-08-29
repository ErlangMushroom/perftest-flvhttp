#ifndef TEST_ARENA_HH_INCLUDED
#define TEST_ARENA_HH_INCLUDED

#include <memory>
#include <sstream>
#include <boost/thread/thread.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/unordered_map.hpp>
#include <unistd.h>
#include "http_play_session.hh"
#include "test_config.hh"
#include "url.hpp"


template <class D, class N>
struct Average {
  BOOST_STATIC_ASSERT(boost::is_arithmetic<D>::value);
  BOOST_STATIC_ASSERT(boost::is_arithmetic<N>::value);

  typedef typeof(D()/N()) T;

  Average()
    : _den(0)
    , _num(0)
    , _max(std::numeric_limits<T>::min())
    , _min(std::numeric_limits<T>::max())
    , _updated(false) {
  }

  D _den;
  N _num;
  T _max;
  T _min;
  bool _updated;

  void Update(const D& dvalue, const N& nvalue) {
    _den += dvalue;
    _num += nvalue;
    T tmp = nvalue / dvalue;
    if (tmp > _max) _max = tmp;
    if (tmp < _min) _min = tmp;
    _updated = true;
  }

  std::string Value() const {
    if (!_updated) {
      return std::string("-");
    }
    std::stringstream stream;
    if (_num == 0) {
      stream << 0;
    } else if (_den == 0) {
      stream << std::numeric_limits<T>::max();
    } else {
      stream << _num / _den;
    }
    return stream.str();
  }

  std::string Min() const {
    if (!_updated) {
      return std::string("-");
    }
    std::stringstream stream;
    stream << _min;
    return stream.str();
  }

  std::string Max() const {
    if (!_updated) {
      return std::string("-");
    }
    std::stringstream stream;
    stream << _max;
    return stream.str();
  }
};

struct CsvRecord {
  CsvRecord(const std::string& name)
    : _name(name) {
  }
  void AddValue(int32_t value) {
    _values.push_back(value);
  }
  bool Empty() const {
    return _values.empty();
  }
  size_t Size() const {
    return _values.size();
  }
  std::string Name() const {
    return _name;
  }
  std::string GetValue(size_t index) const {
    std::stringstream stream;
    if (index < _values.size()) {
      stream << _values[index];
    } else {
      stream << "";
    }
    return stream.str();
  }
  std::string _name;
  std::deque<int32_t> _values;
};

struct Summary {
  static const int MAX_ERROR_COUNT =
    HTTPPlaySession::ERROR_MAX - HTTPPlaySession::ERROR_BASE;

  Summary()
    : _resolve("resolve cost (ms)")
    , _connect("connect cost (ms)")
    , _recvhdr("recvhdr cost (ms)")
    , _1stchunk("1stchunk cost (ms)") {
    memset(_errors, 0, sizeof(_errors));
  }

  Average<size_t, int32_t> _resolving;
  Average<size_t, int32_t> _connecting;
  Average<size_t, int32_t> _recvHeader;
  Average<size_t, int32_t> _firstChunk;
  Average<size_t, int64_t> _kBytesPerSec;

  CsvRecord _resolve;
  CsvRecord _connect;
  CsvRecord _recvhdr;
  CsvRecord _1stchunk;

  size_t _errors[MAX_ERROR_COUNT];

  void UpdateResolving(int32_t dur,
                       bool record = false) {
    _resolving.Update(1, dur);
    if (record) {
      _resolve.AddValue(dur);
    }
  }

  void UpdateConnecting(int32_t dur,
                        bool record = false) {
    _connecting.Update(1, dur);
    if (record) {
      _connect.AddValue(dur);
    }
  }

  void UpdateRecvHeader(int32_t dur,
                        bool record = false) {
    _recvHeader.Update(1, dur);
    if (record) {
      _recvhdr.AddValue(dur);
    }
  }

  void UpdateFirstChunk(int32_t dur,
                        bool record = false) {
    _firstChunk.Update(1, dur);
    if (record) {
      _1stchunk.AddValue(dur);
    }
  }

  void UpdateKBytesPerSec(int64_t bytes, int32_t dur) {
    _kBytesPerSec.Update(dur, bytes);
  }

  void UpdateError(uint32_t err) {
    if (err > PlaySession::HTTP_ERROR_BASE &&
        err < PlaySession::RTMP_ERROR_BASE) {
      _errors[err - HTTPPlaySession::ERROR_BASE]++;
    }
  }

  void WriteToCSV(std::ofstream& fs) {
#define WRITE_LINE(x1,x2,x3,x4) fs<<(x1)<<","<<(x2)<<","<<(x3)<<","<<(x4)<<"\n"
    WRITE_LINE(_resolve.Name(), _connect.Name(), _recvhdr.Name(), _1stchunk.Name());
    size_t cnt_array[4] = { _resolve.Size(), _connect.Size(),
                            _recvhdr.Size(), _1stchunk.Size() };
    size_t* cnt = std::max_element(cnt_array, cnt_array + 4);
    for (size_t i = 0; i < *cnt; i++) {
      WRITE_LINE(_resolve.GetValue(i),
                 _connect.GetValue(i),
                 _recvhdr.GetValue(i),
                 _1stchunk.GetValue(i));
    }
#undef WRITE_LINE
  }
};

class TestArena
  : public PlaySession::Observable {
public:
  typedef boost::asio::io_service io_service;

  TestArena()
    : _overall(new Summary())
    , _interrupted(false) {
  }

  boost::shared_ptr<Summary> GetSummary(const std::string& url) {
    if (_sums.find(url) == _sums.end()) {
      _sums.insert(std::make_pair(
        url, boost::shared_ptr<Summary>(new Summary())));
    }
    return _sums[url];
  }

  virtual void OnResolved(PlaySession* sess,
                          int32_t dur) {
    sess->GetSummary()->UpdateResolving(dur, _cfg.Detailed());
    _overall->UpdateResolving(dur);
  }

  virtual void OnConnected(PlaySession* sess,
                           int32_t dur) {
    sess->GetSummary()->UpdateConnecting(dur, _cfg.Detailed());
    _overall->UpdateConnecting(dur);
  }

  virtual void OnRecvHeader(PlaySession* sess,
                            int32_t dur) {
    sess->GetSummary()->UpdateRecvHeader(dur, _cfg.Detailed());
    _overall->UpdateRecvHeader(dur);
  }

  virtual void OnFirstChunk(PlaySession* sess,
                            int32_t dur) {
    sess->GetSummary()->UpdateFirstChunk(dur, _cfg.Detailed());
    _overall->UpdateFirstChunk(dur);
  }

  virtual void OnContent(PlaySession* sess,
                         size_t bytes,
                         int32_t dur_in_ms) {
    sess->GetSummary()->UpdateKBytesPerSec(bytes, dur_in_ms);
    _overall->UpdateKBytesPerSec(bytes, dur_in_ms);
  }

  virtual void OnTotalBytes(PlaySession* sess,
                            size_t totalbytes) {
    if (totalbytes >= _cfg.MaxRecvLength()) {
      sess->Disconnect();
      if (--_clients == 0) {
        _ioServ.stop();
      }
    }
  }

  virtual void OnFinished(PlaySession* sess) {
    sess->GetSummary()->UpdateError(HTTPPlaySession::ERROR_EARLY_EOF);
    _overall->UpdateError(HTTPPlaySession::ERROR_EARLY_EOF);
    sess->Disconnect();
    if (--_clients == 0) {
      _ioServ.stop();
    }
  }

  virtual void OnError(PlaySession* sess,
                       uint32_t ec) {
    sess->GetSummary()->UpdateError(ec);
    _overall->UpdateError(ec);
    sess->Disconnect();
    if (--_clients == 0) {
      _ioServ.stop();
    }
  }

  void SetConfig(const TestConfig& cfg) {
    _cfg = cfg;
  }

  void Run() {
    if (!_cfg.IsReady()) {
      return;
    }

    boost::asio::signal_set signals(_ioServ, SIGINT, SIGTERM);
    signals.async_wait(boost::bind(&TestArena::SignalHandler, this));

    boost::shared_ptr<io_service::work> workKeeper(
      new io_service::work(_ioServ));
    boost::thread workThread(boost::bind(&io_service::run, &_ioServ));

    int interval = _cfg.Interval();
    int connects = _cfg.Clients();
    _clients = connects;
    TestConfig::URLIterator it = _cfg.GetURLIterator();
    boost::chrono::time_point<boost::chrono::high_resolution_clock> start
      = boost::chrono::high_resolution_clock::now();

    for (int i = 0; i < connects && !_interrupted; ) {
      boost::chrono::microseconds elapsed =
        boost::chrono::duration_cast<boost::chrono::microseconds>(
          boost::chrono::high_resolution_clock::now() - start);
      int naptime = interval * i - elapsed.count();
      if (naptime > 0) {
        usleep(naptime);
      } else {
        std::string url = _cfg.GetNextURL(it++);
        CreateSession(url);
        i++;
      }
    }
    std::cout << "please wait ...\n";
    workKeeper.reset();
    workThread.join();
  }

  void PrintResult() const {
    boost::unordered_map<std::string, boost::shared_ptr<Summary> >::const_iterator it;
    for (it = _sums.begin(); it != _sums.end(); it++) {
      const std::string& url = it->first;
      const boost::shared_ptr<Summary>& sum = it->second;
      std::cout << "Result for " << url << ":\n";
      PrintOneItem(sum.get());
    }

    std::cout << "Result for all:\n";
    PrintOneItem(_overall.get());

    if (_cfg.Detailed()) {
      boost::unordered_map<std::string, boost::shared_ptr<Summary> >::const_iterator it;
      for (it = _sums.begin(); it != _sums.end(); it++) {
        std::string name = it->first + ".csv";
        std::string converted = std::string(name.begin(),
          std::unique(name.begin(), name.end(), Unique));
        std::replace_if(converted.begin(), converted.end(), IsForbidden, '-');
        const boost::shared_ptr<Summary>& sum = it->second;
        std::ofstream fs(converted.c_str());
        sum->WriteToCSV(fs);
      }
    }
  }

protected:

  void SignalHandler() {
    std::cout << "\nInterrupting test loop\n";
    _ioServ.stop();
    _interrupted = true;
  }

  static bool IsForbidden(char c) {
    static std::string forbiddenChars("\\/:?\"<>|");
    return std::string::npos != forbiddenChars.find(c);
  }

  static bool Unique(char l, char r) {
    return IsForbidden(l) && IsForbidden(r);
  }

  static void PrintOneItem(const Summary* sum) {
    std::cout << "  resolve (avg/max/min): "
      << sum->_resolving.Value() << "/"
      << sum->_resolving.Max() << "/"
      << sum->_resolving.Min() << " (ms)"
    << "  connect (avg/max/min): "
      << sum->_connecting.Value() << "/"
      << sum->_connecting.Max() << "/"
      << sum->_connecting.Min() << " (ms)"
    << "  recvhdr (avg/max/min): "
      << sum->_recvHeader.Value() << "/"
      << sum->_recvHeader.Max() << "/"
      << sum->_recvHeader.Min() << " (ms)"
    << "  first_chunk (avg/max/min): "
      << sum->_firstChunk.Value() << "/"
      << sum->_firstChunk.Max() << "/"
      << sum->_firstChunk.Min() << " (ms)"
    << "  bps (avg/max/min): "
      << sum->_kBytesPerSec.Value() << "/"
      << sum->_kBytesPerSec.Max() << "/"
      << sum->_kBytesPerSec.Min() << " (KB/s)"
    << "  err (resolve/connect/request/recv/bad_http/timeout/early_eof): "
#define ERRORCOUNT(x) sum->_errors[(x) - HTTPPlaySession::ERROR_BASE]
      << ERRORCOUNT(HTTPPlaySession::ERROR_ON_RESOLVE) << "/"
      << ERRORCOUNT(HTTPPlaySession::ERROR_ON_CONNECT) << "/"
      << ERRORCOUNT(HTTPPlaySession::ERROR_ON_REQUEST) << "/"
      << ERRORCOUNT(HTTPPlaySession::ERROR_ON_RECV) << "/"
      << ERRORCOUNT(HTTPPlaySession::ERROR_BAD_HTTP) << "/"
      << ERRORCOUNT(HTTPPlaySession::ERROR_TIMEOUT_FOR_NO_DATA) << "/"
      << ERRORCOUNT(HTTPPlaySession::ERROR_EARLY_EOF)
#undef ERRORCOUNT
    << std::endl;
  }

  PlaySession* CreateSession(const std::string& u) {
    urdl::url url(u);
    if (url.protocol() == "rtmp") {
      //return new RTMPPlaySession(&_ioServ);
      return NULL;
    } else if (url.protocol() == "http") {
      return new HTTPPlaySession(this, _ioServ, GetSummary(u), url, _cfg.Timeout());
    }
    return NULL;
  }

private:
  boost::shared_ptr<Summary> _overall;
  boost::unordered_map<std::string, boost::shared_ptr<Summary> > _sums;
  io_service _ioServ;
  TestConfig _cfg;
  bool _interrupted;
  int _clients;
};

#endif // TEST_ARENA_HH_INCLUDED
