/**
  Maman 14
  @CSocketHandler handle a boost asio socket. e.g. read/write.
  @author Roman Koifman
 */

#include "CSocketHandler.h"
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>


/**
   @brief receive (blocking) PACKET_SIZE bytes from socket.
   @param sock the socket to receive from.
   @param buffer an array of size PACKET_SIZE. The data will be copied to the array.
   @return number of bytes actually received.
 */
bool CSocketHandler::receive(boost::asio::ip::tcp::socket& sock, uint8_t(&buffer)[PACKET_SIZE])
{
	try
	{
		memset(buffer, 0, PACKET_SIZE);  // reset array before copying.
		sock.non_blocking(false);             // make sure socket is blocking.
		(void) boost::asio::read(sock, boost::asio::buffer(buffer, PACKET_SIZE));
		return true;
	}
	catch(boost::system::system_error&)
	{
		return false;
	}
}


/**
   @brief send (blocking) PACKET_SIZE bytes to socket.
   @param sock the socket to send to.
   @param buffer an array of size PACKET_SIZE. The data to send will be read from the array.
   @return true if successfuly sent. false otherwise.
 */
bool CSocketHandler::send(boost::asio::ip::tcp::socket& sock, const uint8_t(&buffer)[PACKET_SIZE])
{
	try
	{
		sock.non_blocking(false);  // make sure socket is blocking.
		(void) boost::asio::write(sock, boost::asio::buffer(buffer, PACKET_SIZE));
		return true;
	}
	catch (boost::system::system_error&)
	{
		return false;
	}
}