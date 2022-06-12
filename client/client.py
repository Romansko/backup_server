# client.py
# author Roman Koifman
import os
import random
import socket
import struct
import sys
from enum import Enum


def stopClient(err):
    """ Print err and stop script execution """
    print("\nFatal Error!", err, "Script execution will stop.", sep="\n")
    exit(1)


def generateRandomID():
    max_uint32 = 0xFFFFFFFF
    return random.randint(1, max_uint32)


def initializeSocket():
    """
    Initialize a TCP/IP Socket with parsed server parameters.
    Calling function is responsible for closing the socket.
    """
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect((server, port))
        return s
    except Exception as e:
        stopClient(f"initializeSocket Exception: {e}!")


def socketSend(s, buffer):
    """ make sure socket sending is sized PACKET_SIZE """
    bytes_size = len(buffer)
    if bytes_size < PACKET_SIZE:
        buffer += bytearray(PACKET_SIZE-bytes_size)
    elif bytes_size > PACKET_SIZE:
        buffer = buffer[:PACKET_SIZE]
    s.send(buffer)


class CPayload:
    def __init__(self):
        self.size = 0  # payload size
        self.payload = b""


class CRequest:
    def __init__(self):
        self.userId = userID  # User ID
        self.version = CLIENT_VER  # Client Version
        self.op = 0  # Request Code
        self.nameLen = 0  # filename length
        self.filename = b""  # filename
        self.payload = CPayload()

    def sizeWithoutPayload(self):
        return 12 + self.nameLen  # userId(4), version(1), op(1) , nameLen(2), payload size(4), filename(..)

    def pack(self):
        """ Little Endian pack the Request """
        leftover = PACKET_SIZE - self.sizeWithoutPayload()
        if self.payload.size < leftover:
            leftover = self.payload.size
        return struct.pack(f"<IBBH{self.nameLen}sL{leftover}s",
                           self.userId, self.version, self.op,
                           self.nameLen, self.filename,
                           self.payload.size, self.payload.payload[:leftover])

    @staticmethod
    def getRequest(op, filename=""):
        """ Initialize a request with OP and filename """
        request = CRequest()
        request.op = op.value
        request.filename = bytes(filename, 'utf-8')
        request.nameLen = len(request.filename)  # shouldn't exceed max filename length.
        if request.nameLen > MAX_NAME_LEN:
            stopClient(f"Filename exceeding length {MAX_NAME_LEN}! Filename: {filename}")
        return request

    class EOp(Enum):  # Request codes
        FILE_BACKUP = 100  # Save file backup. All fields should be valid.
        FILE_RESTORE = 200  # Restore a file. size, payload unused.
        FILE_DELETE = 201  # Delete a file. size, payload unused.
        FILE_DIR = 202  # List all client's files. name_len, filename, size, payload unused.


class CResponse:
    def __init__(self, data):
        self.version = 0
        self.status = 0
        self.nameLen = 0
        self.filename = None  # filename
        self.payload = CPayload()

        try:  # [0] for unpacking tuples when required.
            self.version, self.status, self.nameLen = struct.unpack("<BHH", data[:5])
            offset = 5
            self.filename = struct.unpack(f"<{self.nameLen}s", data[offset:offset + self.nameLen])
            self.filename = self.filename[0].decode('utf-8')
            offset += self.nameLen
            self.payload.size = struct.unpack("<I", data[offset:offset + 4])
            self.payload.size = self.payload.size[0]
            offset += 4
            leftover = PACKET_SIZE - offset
            if self.payload.size < leftover:
                leftover = self.payload.size
            self.payload.payload = struct.unpack(f"<{leftover}s", data[offset:offset + leftover])
            self.payload.payload = self.payload.payload[0]
        except Exception as e:
            print(e)

    def validate(self, expected_status):
        """ Validate response status """
        valid = False
        if self.status is None:
            print("Invalid response received!")
        elif self.status == self.EStatus.ERROR_GENERIC.value:
            print(f"Generic Error received! status code {self.status}.")
        elif self.status == self.EStatus.ERROR_NO_FILES.value:
            print(f"Client has no files! status code {self.status}.")
        elif self.status == self.EStatus.ERROR_NOT_EXIST.value:
            tmp_str = "" if (self.filename is None or self.filename == "") else f"'{self.filename}'"
            print(f"Requested File {tmp_str} doesn't exists! status code {self.status}.")
        elif expected_status.value != self.status:
            print(f"Unexpected server response {self.status}!")
        else:
            valid = True
        return valid

    class EStatus(Enum):  # Response Codes
        SUCCESS_RESTORE = 210  # File was found and restored. all fields are valid.
        SUCCESS_DIR = 211  # Files listing returned successfully. all fields are valid.
        SUCCESS_BACKUP_REMOVE = 212  # File was successfully backed up or deleted. size, payload are invalid.
        ERROR_NOT_EXIST = 1001  # File doesn't exist. size, payload are invalid.
        ERROR_NO_FILES = 1002  # Client has no files. Only status & version are valid.
        ERROR_GENERIC = 1003  # Generic server error. Only status & version are valid.


def parseServerInfo(server_info):
    """ parse server.info for server info. return IP String, port. Stop script if error occurred. """
    try:
        info = open(server_info, "r")
        values = info.readline().strip().split(":")
        info.close()
        return values[0], int(values[1])
    except Exception as e:
        stopClient(f"parseServerInfo Exception: {e}!")


def parseFileList(backup_info):
    """ parse backup.info for files list to backup. Stop script on failure """
    try:
        info = open(backup_info, "r")
        files_list = [line.strip() for line in info]
        for filename in files_list:
            if len(filename) > MAX_NAME_LEN:
                info.close()
                stopClient(f"filename exceeding length {MAX_NAME_LEN} was found in {backup_info}!")
        if len(files_list) < 2:
            info.close()
            stopClient(f"Unfulfilled requirement to define at least two filenames in {backup_info}!")
        info.close()
        return files_list
    except Exception as e:
        stopClient(f"parseFileList Exception: {e}!")


def requestFilesList():
    """ Prepare and send a request for file list from server """
    try:
        request = CRequest.getRequest(CRequest.EOp.FILE_DIR)
        s = initializeSocket()
        socketSend(s, request.pack())
        response = CResponse(s.recv(PACKET_SIZE))
        if response.validate(CResponse.EStatus.SUCCESS_DIR):
            bytes_read = len(response.payload.payload)
            buffer = response.payload.payload
            while bytes_read < response.payload.size:
                buffer = buffer + s.recv(PACKET_SIZE)
            s.close()
            files = [file.strip() for file in buffer.decode('utf-8').split('\n')] # \n seperates filenames..
            files.remove("")  # remove empty entries
            if files:
                print(f"Received file list for status code {response.status}:")
                for file in files:
                    print(f"\t{file}")
            else:
                print(f"Invalid response: status code {response.status} but file list is empty!")
    except Exception as e:
        print(f"requestFilesList Exception: {e}!")


def requestFileBackup(filename):
    """ Request to backup a file to server. Large files supported. """
    try:
        request = CRequest.getRequest(CRequest.EOp.FILE_BACKUP, filename)
        request.payload.size = os.path.getsize(filename)
        file = open(filename, "rb")
        request.payload.payload = file.read(PACKET_SIZE - request.sizeWithoutPayload())
        s = initializeSocket()
        socketSend(s, request.pack())
        payload = file.read(PACKET_SIZE)
        while payload:
            socketSend(s, payload)
            payload = file.read(PACKET_SIZE)
        file.close()
        response = CResponse(s.recv(PACKET_SIZE))
        s.close()
        if response.validate(CResponse.EStatus.SUCCESS_BACKUP_REMOVE):
            print(f"File '{filename}' successfully backed-up. status code {response.status}.")
    except Exception as e:
        print(f"backupFile Exception: {e}!")


def requestFileRestore(filename, restore_to=""):
    """ request to restore a file from server """
    try:
        request = CRequest.getRequest(CRequest.EOp.FILE_RESTORE, filename)
        s = initializeSocket()
        socketSend(s, request.pack())
        response = CResponse(s.recv(PACKET_SIZE))
        if response.validate(CResponse.EStatus.SUCCESS_RESTORE):
            if restore_to is None:
                restore_to = response.filename
            if response.filename is None:
                print(f"Restore Error. Invalid filename. yet status code {response.status}.")
            else:
                file = open(restore_to, "wb")
                bytes_read = len(response.payload.payload)
                file.write(response.payload.payload)
                while bytes_read < response.payload.size:
                    data = s.recv(PACKET_SIZE)
                    data_len = len(data)
                    if data_len + bytes_read > response.payload.size:
                        data_len = response.payload.size - bytes_read
                    file.write(data[:data_len])
                    bytes_read += data_len
                file.close()
                print(f"File '{response.filename}' successfully restored within {restore_to}. status code {response.status}.")
            s.close()  # close socket at the end
    except Exception as e:
        print(f"requestFileRestore error! Exception: {e}")


def requestFileRemoval(filename):
    """ request to remove a file from server """
    try:
        request = CRequest.getRequest(CRequest.EOp.FILE_DELETE, filename)
        s = initializeSocket()
        socketSend(s, request.pack())
        response = CResponse(s.recv(PACKET_SIZE))
        s.close()
        if response.validate(CResponse.EStatus.SUCCESS_BACKUP_REMOVE):
            print(f"File '{filename}' successfully removed. status code {response.status}.")
    except Exception as e:
        print(f"requestFileRemoval error! Exception: {e}")


if __name__ == '__main__':
    MAX_NAME_LEN = 256  # max filename len
    PACKET_SIZE = 1024
    CLIENT_VER = 1
    SERVER_INFO = "server.info"
    BACKUP_INFO = "backup.info"
    userID = generateRandomID()
    server, port = parseServerInfo(SERVER_INFO)
    print(f"Client & Server info:\n\tUserId: {userID}\n\tServer: {server},\tPort: {port}")
    backupList = parseFileList(BACKUP_INFO)
    requestFilesList()  # Request file list from server.
    requestFileBackup(backupList[0])  # Backup first file.
    requestFileBackup(backupList[1])  # Backup second file.
    requestFilesList()  # Request file list from server after backing-up two first files.
    requestFileRestore(backupList[0], "tmp")  # Restore first file from server to 'tmp'.
    requestFileRemoval(backupList[0])  # Remove first file from server
    requestFileRestore(backupList[0])  # Restore 1st file from server. Expected error because file is removed.
