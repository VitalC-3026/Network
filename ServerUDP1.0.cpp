#include <iostream>
#include <fstream>
#include <WinSock2.h>
#include <windows.h>
#include <stdio.h>
#include <exception>
#include <ctime>
#pragma comment(lib, "ws2_32.lib")

using namespace std;

const int maxLen = 256;
const int headerLen = 4;
clock_t start, finish;
int totalLen = 0;

struct datagramHeader {
    unsigned char checksum[2] = { 0 };
    unsigned char flags = 0; // 只需要用6个bit N/FN/E/A/P/R/S/F
    unsigned char dataLen = 0;
    void printInfo();

    bool getACK();
    bool getSequenceNumber();
    bool getConnection();
    bool getIsFileName();
    bool getACKConnection();
    bool getFinishConnection();
    bool getACKFinishConnection();

    bool setACK();
    bool setSequenceNumber(bool);
    bool setConnection();
    bool setIsFileName(bool);
    bool setACKConnection();
    bool setACKFinishConnection();
    bool setFinishConnection();

    void clearFlags() {
        flags = 0;
    }
};

void datagramHeader::printInfo() {
    cout << "checksum: " << (USHORT)this->checksum[0] % 256 << (USHORT)this->checksum[1] << endl;
    cout << "flags: " << (USHORT)this->flags % 256 << endl;
    cout << "dataLen: " << (USHORT)this->dataLen % 256 << endl;
}


bool datagramHeader::getACK() {
    if ((flags >> 4) % 2) {
        return true;
    }
    return false;
}

bool datagramHeader::getIsFileName() {
    if ((flags >> 6) % 2) {
        return true;
    }
    return false;
}

bool datagramHeader::getACKConnection() {
    if (flags == 18) {
        return true;
    }
    return false;
}

bool datagramHeader::getSequenceNumber() {
    if (((USHORT)flags % 256) / 128) {
        return true;
    }
    return false;
}

bool datagramHeader::getConnection() {
    if ((USHORT)flags % 256 == 2) {
        return true;
    }
    return false;
}

bool datagramHeader::getFinishConnection() {
    if (((USHORT)flags % 256) % 2) {
        return true;
    }
    return false;
}

bool datagramHeader::getACKFinishConnection() {
    if (flags == 17) {
        return true;
    }
    return false;
}

bool datagramHeader::setACK() {
    flags = 16;
    return true;
}

bool datagramHeader::setIsFileName(bool isFile) {
    if (isFile) {
        flags |= 1 << 6;
    }
    return true;
}

bool datagramHeader::setSequenceNumber(bool num) {
    flags |= num << 7;
    return true;
}

bool datagramHeader::setConnection() {
    flags = 2;
    return true;
}

bool datagramHeader::setACKConnection() {
    flags = 18;
    return true;
}

bool datagramHeader::setFinishConnection() {
    flags = 1;
    return true;
}

bool datagramHeader::setACKFinishConnection() {
    flags = 17;
    return true;
}

void packageData(char* d, const char* content, datagramHeader header) {
    d[0] = header.checksum[0];
    d[1] = header.checksum[1];
    d[2] = header.flags;
    d[3] = 0;
    /*for (int i = 0; i < 5; i++) {
        d[8 + i] = content[i];
    }
    d[strlen(content) + 8] = '\0';*/
}

int unpackageData(char* content, datagramHeader& header, char* s) {
    header.checksum[0] = s[0];
    header.checksum[1] = s[1];
    header.flags = s[2];
    header.dataLen = s[3];
    int len = (USHORT)s[3] % 256;
    if (len == 0 && (header.flags == 0 || header.flags == 128)) {
        len = 256;
    }
    for (int i = 0; i < len; i++) {
        content[i] = s[i + headerLen];
    }
    return len + headerLen;
}

void printDatagram(const char* buffer, datagramHeader header) {
    cout << "header: " << endl;
    header.printInfo();
    cout << "data content: " << endl;
    cout << buffer << endl;
}

bool checksum(char* data, int len) {
    USHORT sum = 0;
    USHORT checksum1 = (USHORT)data[0] % 256 << 8;
    USHORT checksum2 = ((USHORT)data[1] % 256);
    sum += checksum1;
    sum += checksum2;
    for (int i = 2; i < len; i++) {
        USHORT tmp = (USHORT)data[i] % 256;
        if (sum + tmp < sum) {
            sum = sum + tmp + 1;
        }
        else {
            sum = sum + tmp;
        }
    }
    if (sum == (USHORT)~0) {
        return true;
    }
    return false;
}

string setOutputFileName(string in) {
    int rDot = in.rfind(".");
    string type = in.substr(rDot);
    int rSlash = in.rfind("\\");
    string fileName = in.substr(rSlash + 1, rDot - rSlash - 1);
    fileName += (string)"out";
    fileName += type;
    return fileName;
}

int main()
{
    WORD wVersionRequested;
    WSADATA wsaData;
    int err;

    wVersionRequested = MAKEWORD(2, 2);

    // 启动版本检查
    err = WSAStartup(wVersionRequested, &wsaData);
    if (err != 0) {
        printf("Call WSAStartup error. Any key return. \n");
        getchar();
        return 0;
    }

    // 判断版本是否符合要求
    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
        WSACleanup();
        printf("Socket doesn't support Version 2.2. Any key return. \n");
        getchar();
        return 0;
    }

    // 建立套接字
    SOCKET sockServer = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockServer == INVALID_SOCKET) {
        WSACleanup();
        printf("Socket open error. Any key return. \n");
        getchar();
        return 0;
    }

    printf("Hello receiver!\n");

    SOCKADDR_IN addrServer;
    SOCKADDR_IN addrClient;
    addrClient.sin_family = AF_INET;
    addrServer.sin_family = AF_INET;
    addrServer.sin_port = htons(1001);
    addrServer.sin_addr.S_un.S_addr = inet_addr("127.0.0.1");

    // 服务器端绑定端口
    if (bind(sockServer, (SOCKADDR*)&addrServer, sizeof(addrServer)) == SOCKET_ERROR) {
        closesocket(sockServer);
        WSACleanup();
        printf("Socket bind error. Any key return. \n");
        getchar();
        return 0;
    }

    char* buffer = new char[maxLen + 1];
    int datagramLen = maxLen + headerLen;
    char* rdatagram = new char[datagramLen + 1];
    char* sdatagram = new char[datagramLen + 1];
    datagramHeader rdatagramHeader;
    datagramHeader sdatagramHeader;
    int fromLen = sizeof(addrClient);
    bool curSeq = false;
    string inFile = "";
    string outFile = "";
    ofstream fw;
    int count = 0;
    // 等待消息
    while (true) {
        memset(rdatagram, 0, datagramLen);
        memset(sdatagram, 0, datagramLen);
        memset(buffer, 0, maxLen + 1);
        sdatagramHeader.clearFlags();
        int msg = recvfrom(sockServer, rdatagram, datagramLen, 0, (SOCKADDR*)&addrClient, &fromLen);
        if (msg > 0) {
            int len = unpackageData(buffer, rdatagramHeader, rdatagram);
            /*sdatagramHeader.setDstPort(ntohs(addrClient.sin_port));
            sdatagramHeader.setRscPort(1000);*/
            // cout << "receive message length: " << len << endl;
            if (rdatagramHeader.getACKConnection()) {
                printf("3 rounds of shaking hands finished!\n");
            }
            else if (rdatagramHeader.getConnection()) {
                printf("1 round of shaking hands!\n");
                sdatagramHeader.clearFlags();
                sdatagramHeader.setACKConnection();
                memset(buffer, 0, maxLen + 1);
                packageData(sdatagram, buffer, sdatagramHeader);
                sendto(sockServer, sdatagram, headerLen, 0, (SOCKADDR*)&addrClient, sizeof(addrClient));
            }
            else if (rdatagramHeader.getACK() && rdatagramHeader.getIsFileName()) {
                finish = clock();
                cout << inFile.c_str() << " finish!" << endl;
                cout << "transfer time: " << (finish - start) / CLOCKS_PER_SEC << "s" << endl;
                cout << "throughput rate: " << totalLen * CLOCKS_PER_SEC / (finish - start) << " byte(s)/s" << endl;
            }
            else if (rdatagramHeader.getIsFileName()) {
                inFile = (string)buffer;
                if (checksum(rdatagram, len)) {
                    start = clock();
                    totalLen = 0;
                    buffer[strlen(buffer)] = '\0';
                    cout << "successfully receive fileName> " << buffer << endl;
                    outFile = setOutputFileName(inFile);
                    sdatagramHeader.setACK();
                    sdatagramHeader.setIsFileName(true);
                    sdatagramHeader.setSequenceNumber(false);
                    memset(buffer, 0, maxLen + 1);
                    packageData(sdatagram, buffer, sdatagramHeader);
                    sendto(sockServer, sdatagram, headerLen, 0, (SOCKADDR*)&addrClient, sizeof(addrClient));
                }
                else {
                    sdatagramHeader.setACK();
                    sdatagramHeader.setIsFileName(true);
                    sdatagramHeader.setSequenceNumber(true);
                    memset(buffer, 0, maxLen + 1);
                    packageData(sdatagram, buffer, sdatagramHeader);
                    sendto(sockServer, sdatagram, headerLen, 0, (SOCKADDR*)&addrClient, sizeof(addrClient));
                }
            }
            else if (rdatagramHeader.getFinishConnection()) {
                try {
                    sdatagramHeader.setACKFinishConnection();
                    packageData(sdatagram, buffer, sdatagramHeader);
                    sendto(sockServer, sdatagram, headerLen + 5, 0, (SOCKADDR*)&addrClient, sizeof(addrClient));
                    printf("Agree to stop the connection from sender to receiver.\n");
                    memset(sdatagram, 0, datagramLen);
                    packageData(sdatagram, buffer, sdatagramHeader);
                    sdatagramHeader.clearFlags();
                    sdatagramHeader.setFinishConnection();
                    sendto(sockServer, sdatagram, headerLen + 5, 0, (SOCKADDR*)&addrClient, sizeof(addrClient));
                }
                catch (std::exception& e) {
                    e.what();
                }
                break;
            }
            else {
                // cout << "pack" << count++ << endl;
                bool sequence = rdatagramHeader.getSequenceNumber();
                if (curSeq != sequence) {
                    cout << "Sequence Number Error! Send ACK again!" << endl;
                    sdatagramHeader.clearFlags();
                    sdatagramHeader.setACK();
                    sdatagramHeader.setSequenceNumber(!sequence);
                    memset(buffer, 0, maxLen + 1);
                    packageData(sdatagram, buffer, sdatagramHeader);
                    sendto(sockServer, sdatagram, headerLen, 0, (SOCKADDR*)&addrClient, sizeof(addrClient));
                    continue;
                }
                // cout << "current sequence number" << curSeq << endl;
                if (checksum(rdatagram, len)) {
                    curSeq = !curSeq;
                    // cout << "Next expected sequence number" << curSeq << endl;
                    // cout << "dataLen: " << len;
                    totalLen += len - headerLen;
                    if (inFile != "" && outFile != "") {
                        fw.open(outFile.c_str(), ios::binary | ios::app);
                        fw.write(buffer, len - headerLen);
                        fw.close();
                    }
                    else {
                        cout << "No target file to write!" << endl;
                    }
                    // send ACK back
                    sdatagramHeader.clearFlags();
                    sdatagramHeader.setACK();
                    sdatagramHeader.setSequenceNumber(sequence);
                    memset(buffer, 0, maxLen + 1);
                    packageData(sdatagram, buffer, sdatagramHeader);
                    // sdatagram[8] = '\0';
                    sendto(sockServer, sdatagram, headerLen, 0, (SOCKADDR*)&addrClient, sizeof(addrClient));
                }
                else {
                    cout << "checksum error" << endl;
                    sdatagramHeader.setACK();
                    sdatagramHeader.setSequenceNumber(!sequence);
                    memset(buffer, 0, maxLen + 1);
                    packageData(sdatagram, buffer, sdatagramHeader);
                    // sdatagram[8] = '\0';
                    sendto(sockServer, sdatagram, headerLen + 5, 0, (SOCKADDR*)&addrClient, sizeof(addrClient));
                    continue;
                }
            }
        }
        else if (msg == 0) {
            printf("Connection stop. Any key to return.\n");
            getchar();
            return 0;
        }
        else if (msg == SOCKET_ERROR) {
            err = WSAGetLastError();
            switch (err) {
            case(WSAENETDOWN):
                printf("WINDOWS套接口实现检测到网络子系统失效.\n");
                break;
            case(WSAEFAULT):
                printf("fromlen参数非法；from缓冲区大小无法装入端地址.\n");
                break;
            case(WSAEINTR):
                printf("阻塞进程被WSACancelBlockingCall()取消.\n");
                break;
            case(WSAEINPROGRESS):
                printf("一个阻塞的WINDOWS套接口调用正在运行中.\n");
                break;
            case(WSAEMSGSIZE):
                printf("数据报太大无法全部装入缓冲区，故被剪切.\n");
                break;
            case(WSAECONNABORTED):
                printf("由于超时或其他原因，虚电路失效.\n");
                break;
            }
            printf("Any key to return.\n");
            getchar();
            return -1;
        }
        //sendto(sockServer, buffer, strlen(buffer), 0, (SOCKADDR*)&addrClient, fromLen);
    }
    try {
        int msg = recvfrom(sockServer, rdatagram, datagramLen, 0, (SOCKADDR*)&addrClient, &fromLen);
        unpackageData(buffer, rdatagramHeader, rdatagram);
        if (msg > 0 && rdatagramHeader.getACKFinishConnection()) {
            printf("Successfully stop the connection from receiver to sender.\n");
        }
    }
    catch (std::exception& e) {
        e.what();
    }

    closesocket(sockServer);
    WSACleanup();
    return 0;
}
