/**
  Maman 14
  @CFileHandler handle files on the file system.
  @author Roman Koifman
 */

#pragma once
#include <set>
#include <string>

class CFileHandler
{
public:
    bool fileOpen(const std::string& filepath, std::fstream& fs, bool write=false);
    bool fileClose(std::fstream& fs);
    bool fileWrite(std::fstream& fs, const uint8_t* const file, const uint32_t bytes);
    bool fileRead(std::fstream& fs, uint8_t* const file, uint32_t bytes);
    uint32_t fileSize(std::fstream& fs);
	
    bool getFilesList(std::string& filepath, std::set<std::string>& filesList);
    bool fileExists(const std::string& filepath);
    bool fileRemove(const std::string& filepath);
};

