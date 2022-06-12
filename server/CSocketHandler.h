/**
  Maman 14
  @CSocketHandler handle a boost asio socket. e.g. read/write.
  @author Roman Koifman
 */

#pragma once
#include <boost/asio/ip/tcp.hpp>

class CSocketHandler
{
#define PACKET_SIZE  1024
public:
	bool receive(boost::asio::ip::tcp::socket& sock, uint8_t (&buffer)[PACKET_SIZE]);
	bool send(boost::asio::ip::tcp::socket& sock, const uint8_t(&buffer)[PACKET_SIZE]);
};

