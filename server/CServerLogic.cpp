/**
   Maman 14
   @CServerLogic handles server logic invoked by each thread.
   @author Roman Koifman
 */

#include "CServerLogic.h"
#include <sstream> 
#include <algorithm>
#include <fstream>
#include <thread>

/**
   @brief generate a random string of given length.
          based on https://stackoverflow.com/questions/440133/how-do-i-create-a-random-alpha-numeric-string-in-c
   @param length the string's length to generate.
   @return the generated string.
 */
std::string CServerLogic::randString(const uint32_t length) const
{
	if (length == 0)
        return "";
	auto randChar = []() -> char
	{
		const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
		const size_t maxIndex = (sizeof(charset) - 1);
		return charset[rand() % maxIndex];
	};
	std::string str(length, 0);
	std::generate_n(str.begin(), length, randChar);
	return str;
}

/**
   @brief check if a given user has any files.
   @param userId the user's id.
   @return true if user has at least one file. false otherwise.
 */
bool CServerLogic::userHasFiles(const uint32_t userId)
{
	if (userId == 0)
		return false;
	std::stringstream ss;
	ss << BACKUP_FOLDER << userId;
	auto userFolder = ss.str();
	std::set<std::string> userFiles;

	if (!_fileHandler.getFilesList(userFolder, userFiles))
		return false;
	return (!userFiles.empty());
}


/**
   @brief try to parse a given filename as a bytes array.
   @param filenameLength the length of given filename
   @param filename the given filename as a bytes array.
   @param parsedFilename the parsed file name will be saved in this object.
   @return true if filename was parsed successfully.
 */
bool CServerLogic::parseFilename(const uint16_t filenameLength, const uint8_t* filename, std::string& parsedFilename)
{
	if (filenameLength == 0 || filenameLength > FILENAME_MAX || filename == nullptr)
		return false;
	try
	{
		char* str = new char[filenameLength + 1];   // +1 for '\0'
		(void)memcpy(str, filename, filenameLength);
		str[filenameLength] = '\0';
		parsedFilename = str;   // content copy.
		delete[] str;
	}
	catch (std::bad_alloc&)
	{
		return false;
	}
	return true;
}

/***
   @brief Copy a filename from request to response. the calling function is responsible for freeing the allocated memory.
   @param request the source of the filename
   @param response the destination for the filename copy
 */
void CServerLogic::copyFilename(const SRequest& request, SResponse& response)
{
	if (request.nameLen == 0)
		return;  // invalid
	response.nameLen = request.nameLen;
	response.filename = new uint8_t[request.nameLen];
	memcpy(response.filename, request.filename, request.nameLen);
}


/**
   @brief thread's entry point function.
   @param sock the socket a client connected to.
   @param err description error string stream for debugging.
   @return true if operation succeeded. false otherwise.
 */
bool CServerLogic::handleSocketFromThread(boost::asio::ip::tcp::socket& sock, std::stringstream& err)
{
	try
	{
		uint8_t buffer[PACKET_SIZE];
		SRequest* request = nullptr;    // allocated in deserializeRequest()
		SResponse* response = nullptr;  // allocated in handleRequest()
		bool responseSent = false;      // response was sent ?

		if (!_socketHandler.receive(sock, buffer))
		{
			err << "CServerLogic::handleSocketFromThread: Failed to receive first message from socket!" << std::endl;
			return false;
		}
		request = deserializeRequest(buffer, PACKET_SIZE);
		while (lock(*request) == false)  // If server is handling already exact user's ID request
		{
			std::this_thread::sleep_for(std::chrono::seconds(3));
		}
		const bool success = handleRequest(*request, response, responseSent, sock, err);

		// Free allocated memory.
		if (!responseSent)
		{
			serializeResponse(*response, buffer);
			if (!_socketHandler.send(sock, buffer))
			{
				err << "Response sending on socket failed!" << std::endl;
				destroy(response);
				return false;
			}
			destroy(response);
			sock.close();
		}
		
		unlock(*request);  // release lock on user id
		destroy(request);
		
		return success;
	}
	catch (std::exception& e)
	{
		err << "Exception in thread: " << e.what() << "\n";
		return false;
	}
}


/**
   @brief Handle a client request. While SResponse is allocated in this function,
          the calling function is responsible for deallocating SResponse.
   @param request the request to handle.
   @param response the response to the request which is allocated in this function.
   @param sock connected socket
   @param responseSent indicates whether a response was sent.
   @param err description error. applicable only if function returns false.
   @return true if no error occurred. false, otherwise.
 */
bool CServerLogic::handleRequest(const SRequest& request, SResponse*& response, bool& responseSent, boost::asio::ip::tcp::socket& sock, std::stringstream& err)
{
	responseSent = false;
	response = new SResponse;
	if (request.header.userId == 0) // invalid ID.
	{
		err << "Invalid User ID #" << +request.header.userId << std::endl;
		response->status = SResponse::ERROR_GENERIC;
		return false;
	}
	
	// Common validation for FILE_RESTORE | FILE_REMOVE | FILE_DIR requests.
	if ( (request.header.op & (SRequest::FILE_RESTORE | SRequest::FILE_REMOVE | SRequest::FILE_DIR)) == request.header.op)
	{
		if (!userHasFiles(request.header.userId))
		{
			err << "User #" << +request.header.userId << " has no files!" << std::endl;
			response->status = SResponse::ERROR_NO_FILES;
			return false;
		}
	}

	// Common validation for FILE_BACKUP | FILE_RESTORE | FILE_REMOVE requests.
	std::string parsedFileName; // will be used as parsed filename string.
	if ((request.header.op & (SRequest::FILE_BACKUP | SRequest::FILE_RESTORE | SRequest::FILE_REMOVE)) == request.header.op)
	{
		if (!parseFilename(request.nameLen, request.filename, parsedFileName))
		{
			err << "Request Error for user ID #" << +request.header.userId << ": Invalid filename!" << std::endl;
			response->status = SResponse::ERROR_GENERIC;
			return false;
		}
		copyFilename(request, *response);
	}

	std::stringstream userPathSS;
	std::stringstream filepathSS;
	userPathSS << BACKUP_FOLDER << request.header.userId << "/";
	filepathSS << userPathSS.str() << parsedFileName;
	const std::string filepath = filepathSS.str();
	
	// Common validation for FILE_RESTORE | FILE_REMOVE requests.
	if ((request.header.op & (SRequest::FILE_RESTORE | SRequest::FILE_REMOVE)) == request.header.op)
	{
		if (!_fileHandler.fileExists(filepath))
		{
			err << "Request Error for user ID #" << +request.header.userId << ": File not exists!" << std::endl;
			response->status = SResponse::ERROR_NOT_EXIST;
			return false;
		}
	}

	// Specifics
	response->status = SResponse::ERROR_GENERIC;  // until proven otherwise..
	uint8_t buffer[PACKET_SIZE];
	switch (request.header.op)
	{
	/**
	   save file to disk. do not close socket on failure. response handled outside.
	 */
	case SRequest::FILE_BACKUP:
	{
		std::fstream fs;
		if (!_fileHandler.fileOpen(filepath, fs, true))
		{
			err << "user ID #" << +request.header.userId << ": File " << parsedFileName << " failed to open." << std::endl;
			return false;
		}
		uint32_t bytes = (PACKET_SIZE - request.sizeWithoutPayload());
		if (request.payload.size < bytes)
			bytes = request.payload.size;
		if (!_fileHandler.fileWrite(fs, request.payload.payload, bytes))
		{
			err << "user ID #" << +request.header.userId << ": Write to file " << parsedFileName << " failed." << std::endl;
			fs.close();
			return false;
		}

		while(bytes < request.payload.size)
		{
			if (!_socketHandler.receive(sock, buffer))
			{
				err << "user ID #" << +request.header.userId << ": receive file data from socket failed." << std::endl;
				fs.close();
				return false;
			}
			uint32_t length = PACKET_SIZE;
			if (bytes + PACKET_SIZE > request.payload.size)
				length = request.payload.size - bytes;
			if (!_fileHandler.fileWrite(fs, buffer, length))
			{
				err << "user ID #" << +request.header.userId << ": Write to file " << parsedFileName << " failed." << std::endl;
				fs.close();
				return false;
			}
			bytes += length;
		}
		fs.close();
		response->status = SResponse::SUCCESS_BACKUP_DELETE;
		return true;
	}

	/**
	   Restore file from disk. close socket on failure. specific socket logic.
	 */
	case SRequest::FILE_RESTORE:
	{
		std::fstream fs;
		if (!_fileHandler.fileOpen(filepath, fs))
		{
			err << "user ID #" << +request.header.userId << ": File " << parsedFileName << " failed to open." << std::endl;
			return false;
		}
		uint32_t fileSize = _fileHandler.fileSize(fs);
		if (fileSize == 0)
		{
			err << "user ID #" << +request.header.userId << ": File " << parsedFileName << " has 0 zero." << std::endl;
			fs.close();
			return false;
		}
		response->payload.size = fileSize;
		uint32_t bytes = (PACKET_SIZE - response->sizeWithoutPayload());
		response->payload.payload = new uint8_t[bytes];
		if (!_fileHandler.fileRead(fs, response->payload.payload, bytes))
		{
			err << "user ID #" << +request.header.userId << ": File " << parsedFileName << " reading failed." << std::endl;
			fs.close();
			return false;
		}

		// send first packet
		responseSent = true;
		response->status = SResponse::SUCCESS_RESTORE;
		serializeResponse(*response, buffer);
		if (!_socketHandler.send(sock, buffer))
		{
			err << "Response sending on socket failed! user ID #" << +request.header.userId << std::endl;
			fs.close();
			sock.close();
			return false;
		}
			
		while(bytes < fileSize)
		{
			if (!_fileHandler.fileRead(fs, buffer, PACKET_SIZE) || !_socketHandler.send(sock, buffer))
			{
				err << "Payload data failure for user ID #" << +request.header.userId << std::endl;
				fs.close();
				sock.close();
				return false;
			}
			bytes += PACKET_SIZE;
		}

		destroy(response);
		fs.close();
		sock.close();
		return true;
	}

	/**
	   Remove file from disk. response handled outside.
	 */
	case SRequest::FILE_REMOVE:
	{
		if (!_fileHandler.fileRemove(filepath))
		{
			err << "Request Error for user ID #" << +request.header.userId << ": File deletion failed!" << std::endl;
			return false;
		}
		response->status = SResponse::SUCCESS_BACKUP_DELETE;
		return true;
	}

	
	/**
	   Read file list from disk, separate to packets if file names size exceeding PACKET_SIZE, send to client.
	   Specific socket logic. close socket on failure.
	*/
	case SRequest::FILE_DIR:
	{
		std::set<std::string> userFiles;
		std::string userFolder(userPathSS.str());
		if (!_fileHandler.getFilesList(userFolder, userFiles))
		{
			err << "Request Error for user ID #" << +request.header.userId << ": FILE_DIR generic failure." << std::endl;
			response->status = SResponse::ERROR_GENERIC;  // can be only generic error. empty files were validated before.
			return false;
		}
		const size_t filenameLen = 32;  // random string length, as required.
		response->filename = new uint8_t[filenameLen];
		response->nameLen = filenameLen;
		memcpy(response->filename, randString(filenameLen).c_str(), filenameLen);
		response->status = SResponse::SUCCESS_DIR;
		
		// list's size calculation.
		size_t listSize = 0;
		for (const auto& fn : userFiles)
			listSize += fn.size() + 1;  // +1 for '\n' to represent filename ending.
		response->payload.size = listSize;
		auto const listPtr = new uint8_t[listSize];           // assumption: listSize will not exceed RAM. (mentioned in forum).
		auto ptr = listPtr;
		for (const auto& fn : userFiles)
		{
			memcpy(ptr, fn.c_str(), fn.size());
			ptr += fn.size();
			*ptr = '\n';
			ptr += 1;
		}
		if (response->sizeWithoutPayload() + listSize <= PACKET_SIZE)  // file names do not exceed PACKET_SIZE.
		{
			response->payload.payload = listPtr;  // will be de-allocated by outer logic.
			return true;
		}

		// file names exceed PACKET_SIZE. Split Message.
		ptr = listPtr;
		responseSent = true;  // specific sending logic. no need to send after function end.
		uint32_t bytes = PACKET_SIZE - response->sizeWithoutPayload();  // leftover bytes
		response->payload.payload = new uint8_t[bytes];
		memcpy(response->payload.payload, ptr, bytes);
		ptr += bytes;
		
		// send first packet
		serializeResponse(*response, buffer);
		if (!_socketHandler.send(sock, buffer))
		{
			err << "Response sending on socket failed! user ID #" << +request.header.userId << std::endl;
			destroy(response);
			sock.close();
			return false;
		}
		
		bytes = PACKET_SIZE;  // bytes sent.
		while (bytes < (response->sizeWithoutPayload() + listSize))
		{
			memcpy(buffer, ptr, PACKET_SIZE);
			ptr += PACKET_SIZE;
			bytes += PACKET_SIZE;
			if (!_socketHandler.send(sock, buffer))
			{
				err << "Payload data failure for user ID #" << +request.header.userId << std::endl;
				destroy(response);
				sock.close();
				return false;
			}
		}
			
		destroy(response);
		sock.close();
		return true;
	}
	default:  // response handled outside.
	{
		err << "Request Error for user ID #" << +request.header.userId << ": Invalid request code: " << +request.header.op << std::endl;
		return true;
	}
	} // end of switch
}  // end of resolve()


/**
   @brief deserialize raw data into request.
 */
CServerLogic::SRequest* CServerLogic::deserializeRequest(const uint8_t* const buffer, const uint32_t size)
{
	uint32_t bytesRead = 0;
	const uint8_t* ptr = buffer;
	if (size < sizeof(SRequest::SRequestHeader))
		return nullptr; // invalid minimal size.

	auto const request = new SRequest;
	
	// Fill minimal header
	memcpy(request, ptr, sizeof(SRequest::SRequestHeader));
	bytesRead += sizeof(SRequest::SRequestHeader);
	ptr += sizeof(SRequest::SRequestHeader);
	if (bytesRead + sizeof(uint16_t) > size)
		return request;  // return the request with minimal header.
	
	// Copy name length
	memcpy(&(request->nameLen), ptr, sizeof(uint16_t));
	bytesRead += sizeof(uint16_t);
	ptr += sizeof(uint16_t);
	if ((request->nameLen == 0) || ((bytesRead + request->nameLen) > size))
		return request;  // name length invalid.

	request->filename = new uint8_t[request->nameLen + 1];
	memcpy(request->filename, ptr, request->nameLen);
	request->filename[request->nameLen] = '\0';
	bytesRead += request->nameLen;
	ptr += request->nameLen;
	
	if (bytesRead + sizeof(uint32_t) > size)
		return request;

	// copy payload size
	memcpy(&(request->payload.size), ptr, sizeof(uint32_t));
	bytesRead += sizeof(uint32_t);
	ptr += sizeof(uint32_t);
	if (request->payload.size == 0)
		return request;  // name length invalid.

	// copy payload until size limit.
	uint32_t leftover = size - bytesRead;
	if (request->payload.size < leftover)
		leftover = request->payload.size;
	request->payload.payload = new uint8_t[leftover];
	memcpy(request->payload.payload, ptr, leftover);
	
	return request;
}

void CServerLogic::serializeResponse(const SResponse& response, uint8_t* buffer)
{
	uint8_t* ptr = buffer;
	uint32_t size = (PACKET_SIZE - response.sizeWithoutPayload());
	if (response.payload.size < size)
		size = response.payload.size;	

	memcpy(ptr, &(response.version), sizeof(response.version));
	ptr += sizeof(response.version);
	memcpy(ptr, &(response.status), sizeof(response.status));
	ptr += sizeof(response.status);
	memcpy(ptr, &(response.nameLen), sizeof(response.nameLen));
	ptr += sizeof(response.nameLen);
	memcpy(ptr, (response.filename), response.nameLen);
	ptr += response.nameLen;
	memcpy(ptr, &(response.payload.size), sizeof(response.payload.size));
	ptr += sizeof(response.payload.size);
	memcpy(ptr, (response.payload.payload), size);
}

void CServerLogic::destroy(uint8_t* ptr)
{
	if (ptr != nullptr)
	{
		delete[] ptr;
		ptr = nullptr;
	}
}

void CServerLogic::destroy(SRequest* request)
{
	if (request != nullptr)
	{
		destroy(request->filename);
		destroy(request->payload.payload);
		delete request;
		request = nullptr;
	}
}

void CServerLogic::destroy(SResponse* response)
{
	if (response != nullptr)
	{
		destroy(response->filename);
		destroy(response->payload.payload);
		delete response;
		response = nullptr;
	}
}

bool CServerLogic::lock(const SRequest& request)
{
	if (request.header.userId == 0)
		return true;
	if (_userHandling[request.header.userId])
		return false;
	_userHandling[request.header.userId] = true;
	return true;
}

void CServerLogic::unlock(const SRequest& request)
{
	if (request.header.userId == 0)
		return;
	_userHandling[request.header.userId] = false;
}