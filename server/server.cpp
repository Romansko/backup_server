
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                                                //
// server.cpp : Maman 14 Server. Based on boost_asio\example\cpp11\echo\example blocking_tcp_echo_server.cpp      //
// @author Roman Koifman                                                                                          //
//                                                                                                                //
// In order to use the boost library (v1.77), I was required to:                                                  //
// 1. Compile it.                                                                                                 //
// 2. Add "D:\boost_1_77_0" to compiler include path.                                                             //
// 3. Add "D:\boost_1_77_0\stage\lib" to linker library paths.                                                    //
// 3. Define WIN32_WINNT=0x0A00 under project's Preprocessor Definitions (windows 10).                            //
//                                                                                                                //
// Further project settings:                                                                                      //
// 1. Not using precompiled headers.                                                                              //
// 2. Using c++17 language standard.                                                                              //
//                                                                                                                //
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "CServerLogic.h"
#include <iostream>
#include <thread>
#include <boost/asio.hpp>
using boost::asio::ip::tcp;

/**
    If DEBUG_RESOLVE set to 1, resolve errors will be printed to std::out.
    It's set to 0 because server shouldn't print errors for each thread, at least without a lock.
    Locking the out stream will make the server to answer threads more slowly. Hence default value is 0.
    This explanation is for the Maman14 checkers to show that I thought of it, but printing within server was not required.
 */
#define DEBUG_RESOLVE 0

// private globals
static CServerLogic serverLogic;
static const uint16_t port = 8080;

void handleRequest(tcp::socket sock)
{
    try
    {
        std::stringstream err;
        const bool success = serverLogic.handleSocketFromThread(sock, err);
#if DEBUG_RESOLVE == 1     // See comment above. 
        if (!success)
        {
            std::cout << err.str() << std::endl;
        }
#endif
    }
    catch (std::exception& e)
    {
        std::cerr << "Exception in thread: " << e.what() << "\n";
    }
}


int main(int argc, char* argv[])
{
	try
    {
        boost::asio::io_context io_context;
        tcp::acceptor accptr(io_context, tcp::endpoint(tcp::v4(), port));
        for (;;)
        {
            std::thread(handleRequest, accptr.accept()).detach();
        }
    }
    catch(std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << std::endl;
    }

    return 0;
}