#ifndef PLAYSESSION_HH_INCLUDED
#define PLAYSESSION_HH_INCLUDED

#include <string>
#include <stdint.h>

struct PlaySession {
  enum ErrorCode {
    HTTP_ERROR_BASE = 0x0000,
    RTMP_ERROR_BASE = 0x0F00,
  };

  struct Observable {
    virtual void OnResolved(PlaySession* sess, int32_t dur_in_ms) = 0;
    virtual void OnConnected(PlaySession* sess, int32_t dur_in_ms) = 0;
    virtual void OnRecvHeader(PlaySession* sess, int32_t dur_in_ms) = 0;
    virtual void OnFirstChunk(PlaySession* sess, int32_t dur_in_ms) = 0;
    virtual void OnContent(PlaySession* sess, size_t bytes, int32_t dur_in_ms) = 0;
    virtual void OnTotalBytes(PlaySession* sess, size_t totalbytes) = 0;
    virtual void OnFinished(PlaySession* sess) = 0;
    virtual void OnError(PlaySession* sess, uint32_t ec) = 0;
  };

  virtual ~PlaySession() {}
  virtual void Disconnect() = 0;
  virtual std::string GetPlayURL() const = 0;
};

#endif // PLAYSESSION_HH_INCLUDED
