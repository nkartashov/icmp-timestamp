#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <istream>
#include <iostream>
#include <ostream>
#include <vector>

#include "icmp_header.hpp"
#include "ipv4_header.hpp"

using boost::asio::ip::icmp;
using boost::asio::deadline_timer;
namespace posix_time = boost::posix_time;

class TimeSynchronizer
{
public:
  typedef std::vector<char> DataVector;
  typedef unsigned char uchar_t;
  const size_t cIntSizeInBytes = 4;
  const char cByte = 0xFF;

  TimeSynchronizer(boost::asio::io_service& ioService, const char* destination)
    : mResolver(ioService), mSocket(ioService, icmp::v4()),
      mTimer(ioService), mSequenceNumber(0), mReplyNumber(0) {
    icmp::resolver::query query(icmp::v4(), destination, "");
    mDestination = *mResolver.resolve(query);

    StartSend();
    StartReceive();
  }

private:
  void printMillisecondsAsTime(uint32_t millis) {
    posix_time::time_duration millisTimeOfDay = posix_time::milliseconds(millis);
    std::cout << millisTimeOfDay.hours() << "h " << millisTimeOfDay.minutes() << "m " << millisTimeOfDay.seconds() << "s " << "UTC" << std::endl;
  }

  //Gets milliseconds after midnight UTC
  uint32_t now() {
    auto now = posix_time::microsec_clock::universal_time().
      time_of_day();
    return static_cast<uint32_t>(now.total_milliseconds());
  }

  void addTimestamp(DataVector& buffer, uint32_t time) {
    time = htonl(time);
    for (size_t i = 0; i != cIntSizeInBytes; ++i) {
      buffer.push_back(((char*) &time)[i]);
    }
  }

  uint32_t dataVectorToTimestamp(DataVector& buffer) {
    uint32_t resultTime = 0;
    for (size_t i = 0; i != cIntSizeInBytes; ++i) {
      ((char*)(&resultTime))[i] = buffer[i];
    }
    return ntohl(resultTime);
  }

  void dumpDataVector(DataVector& vector, std::ostream& out) {
    for (auto it = vector.begin(); it != vector.end(); ++it) {
      out << *it;
    }
  }

  void readIntoDataVector(DataVector& vector, std::istream& in) {
    char ch;
    for (size_t i = 0; i < cIntSizeInBytes; ++i) {
      in.get(ch);
      vector.push_back(ch);
    }
  }

  void StartSend() {
    icmp_header timestampRequest;
    timestampRequest.type(icmp_header::timestamp_request);
    timestampRequest.code(0);
    timestampRequest.identifier(GetIdentifier());
    timestampRequest.sequence_number(++mSequenceNumber);


    // Encode the request packet.
    boost::asio::streambuf requestBuffer;
    std::ostream os(&requestBuffer);
    DataVector body;
    addTimestamp(body, now());
    addTimestamp(body, now());
    addTimestamp(body, now());
    compute_checksum(timestampRequest, body.begin(), body.end());
    os << timestampRequest;
    dumpDataVector(body, os);

    std::cout << "Old time: ";
    printMillisecondsAsTime(now());

    // Send the request.
    mTimeSent = posix_time::microsec_clock::universal_time();
    mSocket.send_to(requestBuffer.data(), mDestination);

    // Wait up to five seconds for a reply.
    mReplyNumber = 0;
    mTimer.expires_at(mTimeSent + posix_time::seconds(5));
    mTimer.async_wait(boost::bind(&TimeSynchronizer::HandleTimeout, this));
  }

  void HandleTimeout() {
    if (mReplyNumber == 0)
      std::cout << "Request timed out" << std::endl;

    // Requests must be sent no less than one second apart.
    mTimer.expires_at(mTimeSent + posix_time::seconds(1));
    mTimer.async_wait(boost::bind(&TimeSynchronizer::StartSend, this));
  }

  void StartReceive() {
    // Discard any data already in the buffer.
    mReplyBuffer.consume(mReplyBuffer.size());

    // Wait for a reply. We prepare the buffer to receive up to 64KB.
    mSocket.async_receive(mReplyBuffer.prepare(65536),
        boost::bind(&TimeSynchronizer::HandleReceive, this, _2));
  }

  void HandleReceive(std::size_t length) {
    // The actual number of bytes received is committed to the buffer so that we
    // can extract it using a std::istream object.
    mReplyBuffer.commit(length);


    // Decode the reply packet.
    std::istream is(&mReplyBuffer);
    ipv4_header ipv4Header;
    icmp_header icmpHeader;
    is >> ipv4Header >> icmpHeader;

    if (is &&
        icmpHeader.type() == icmp_header::timestamp_reply &&
        icmpHeader.identifier() == GetIdentifier() &&
        icmpHeader.sequence_number() == mSequenceNumber) {
      if (mReplyNumber++ == 0)
        mTimer.cancel();

      DataVector originate, receive, transmit;
      readIntoDataVector(originate, is);
      readIntoDataVector(receive, is);
      readIntoDataVector(transmit, is);
      uint32_t orig, recv, trans;
      orig = dataVectorToTimestamp(originate);
      recv = dataVectorToTimestamp(receive);
      trans = dataVectorToTimestamp(transmit);
      uint32_t receivedNowTime = trans + (recv - orig);
      posix_time::ptime now = posix_time::microsec_clock::universal_time();
      std::cout
        << "Time between request and response: " << (now - mTimeSent).total_milliseconds() << " ms" << std::endl << "New time: ";
      printMillisecondsAsTime(receivedNowTime);
      throw std::exception();
    } else {
      StartReceive();
    }
  }

  static unsigned short GetIdentifier()
  {
#if defined(BOOST_WINDOWS)
    return static_cast<unsigned short>(::GetCurrentProcessId());
#else
    return static_cast<unsigned short>(::getpid());
#endif
  }

  icmp::resolver mResolver;
  icmp::endpoint mDestination;
  icmp::socket mSocket;
  deadline_timer mTimer;
  unsigned short mSequenceNumber;
  posix_time::ptime mTimeSent;
  boost::asio::streambuf mReplyBuffer;
  std::size_t mReplyNumber;
};

int main(int argc, char* argv[])
{
  try
  {
    if (argc != 2)
    {
      std::cerr << "Usage: timestamp <host>" << std::endl;
#if !defined(BOOST_WINDOWS)
      std::cerr << "(You may need to run this program as root.)" << std::endl;
#endif
      return 1;
    }

    boost::asio::io_service ioService;
    TimeSynchronizer p(ioService, argv[1]);
    ioService.run();
  }
  catch (std::exception& e) {
    // Just stop, quick and dirty
    // Exception driven, I know
  }
}
