#include <iostream>
#include <fstream>
#include <WinSock2.h>
#include <windows.h>
#include <stdio.h>
#include <exception>
#include <vector>
#pragma comment(lib, "ws2_32.lib")

using namespace std;

const int maxLen = 1024;
const int headerLen = 5;
const int windowSize = 256; // 接收端窗口256
vector<char*> recvBuffer;
int lastSequence = -1;

struct datagramHeader {
    unsigned char checksum[2] = { 0 };
    unsigned char flagsDataLen[2] = { 0 }; // FILE/ACK/SYN/FIN/FileEnd/0/10位DataLen
    unsigned char sequenceNum = '\0'; // 8位序列号                                                                                                                                                                      
    
    void clearFlags() {
        flagsDataLen[0] &= 3;
    }

    void setConnection() {
        flagsDataLen[0] = 32; // 00100000
    }
    void setACK() {
        flagsDataLen[0] |= 1 << 6; // 01000000
    }
    void setACKConnection() {
        flagsDataLen[0] = 96; // 01100000
    }
    void setACKFinishConnection() {
        flagsDataLen[0] = 80; // 01010000
    }
    void setIsFileName(bool isFile) {
        if (isFile) {
            flagsDataLen[0] |= 128;
        }
    }
    void setACKFile() {
        flagsDataLen[0] = 192;
    }
    void setACKFileEnd() {
        flagsDataLen[0] = 72;
    }
    void setFinishConnection() {
        flagsDataLen[0] = 16; // 00010000
    }
    void setSequenceNumber(char num) {
        sequenceNum = num;
    }
    void setDataLen(int len) {
        if (len == 0) {
            flagsDataLen[1] = '\0';
            flagsDataLen[0] &= 248;
            return;
        }
        if (len > 256) {
            flagsDataLen[0] &= 0b1111100;
            flagsDataLen[0] |= len >> 8;
        }
        flagsDataLen[1] = len % 256;

    }
    void setFileEnd() {
        flagsDataLen[0] = 8; // 00001000
    }

    bool getConnection() {
        if (flagsDataLen[0] == 32) { return true; }
        return false;
    }
    char getACK() {
        if ((flagsDataLen[0] >> 6) % 2) { return true; }
        return false;
    }
    bool getACKConnection() {
        if (flagsDataLen[0] == 96) { return true; }
        return false;
    }
    bool getFinishConnection() {
        if (flagsDataLen[0] == 16) { return true; }
        return false;
    }
    bool getIsFileName() {
        if ((flagsDataLen[0] >> 7) % 2) { return true; }
        return false;
    }
    bool getACKFile() {
        if (flagsDataLen[0] == 72) { return true; }
        return false;
    }
    bool getACKFinishConnection() {
        if (flagsDataLen[0] == 80) { return true; }
        return false;
    }
    bool getIsFileEnd() {
        if (flagsDataLen[0] == 8) { return true; }
        return false;
    }
    bool getACKFileEnd() {
        if (flagsDataLen[0] == 72) { return true; }
        return false;
    }
    USHORT getDataLen() {
        USHORT len = (USHORT)flagsDataLen[1] % 256;
        len += (flagsDataLen[0] & 3) << 8;
        if (len == 0) {
            if ((flagsDataLen[0] & 0b11111000) >> 3 == 0) {
                len = 1024;
            }
        }
        return len;
    }
    char getSequenceNumber() { return sequenceNum; }
};

void packageData(char* s, datagramHeader header) {
    s[0] = header.checksum[0];
    s[1] = header.checksum[1];
    s[2] = header.flagsDataLen[0];
    s[3] = header.flagsDataLen[1];
    s[4] = header.sequenceNum;
}

int unpackageData(char* data, datagramHeader &header, const char* r) {
    header.checksum[0] = r[0];
    header.checksum[1] = r[1];
    header.flagsDataLen[0] = r[2];
    header.flagsDataLen[1] = r[3];
    header.sequenceNum = r[4];
    USHORT len = (USHORT)header.getDataLen();
    for (int i = 0; i < len; i++) {
        data[i] = r[i + headerLen];
    }
    return len;
}

void printDatagram(char* data, datagramHeader header) {

}

bool checksum(char* data, int len) {
    // cout << "checksum" << len << endl;
    USHORT sum = 0;
    USHORT checksum1 = (USHORT)data[0] % 256 << 8;
    // cout << 0 << ":" << checksum1 << endl;
    USHORT checksum2 = ((USHORT)data[1] % 256);
    // cout << 1 << ":" << checksum2 << endl;
    for (int i = 2; i < len; i++) {
        USHORT tmp = (USHORT)data[i] % 256;
        // cout << i << ":" << tmp << endl;
        if (sum + tmp < sum) {
            sum = sum + tmp + 1;
        }
        else {
            sum = sum + tmp;
        }
    }
    // cout << "dataSum:" << sum << endl;
    sum += checksum1;
    sum += checksum2;
    // cout << "checkSumTotal:" << sum << endl;
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
    datagramHeader rheader;
    datagramHeader sheader;
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
        sheader.clearFlags();
        int msg = recvfrom(sockServer, rdatagram, datagramLen, 0, (SOCKADDR*)&addrClient, &fromLen);
        if (msg > 0) {
            int len = unpackageData(buffer, rheader, rdatagram);
            cout << "receive message length: " << len << endl;
            if (rheader.getACKConnection()) {
                printf("3 rounds of shaking hands finished!\n");
            }
            else if (rheader.getConnection()) {
                printf("1 round of shaking hands!\n");
                sheader.clearFlags();
                sheader.setACKConnection();
                packageData(sdatagram, sheader);
                sendto(sockServer, sdatagram, headerLen, 0, (SOCKADDR*)&addrClient, sizeof(addrClient));
            }
            else if (rheader.getIsFileName()) {
                inFile = (string)buffer;
                if (checksum(rdatagram, len + headerLen)) {
                    buffer[strlen(buffer)] = '\0';
                    cout << "successfully receive fileName> " << buffer << endl;
                    outFile = setOutputFileName(inFile);
                    sheader.clearFlags();
                    sheader.setACK();
                    sheader.setIsFileName(true);
                    memset(sdatagram, 0, datagramLen);
                    packageData(sdatagram, sheader);
                    sendto(sockServer, sdatagram, headerLen, 0, (SOCKADDR*)&addrClient, sizeof(addrClient));
                    fw.open(outFile.c_str(), ios::binary | ios::app);
                }
                else {
                    cout << "checksum error for filePath" << endl;
                    sheader.clearFlags();
                    sheader.setIsFileName(true);
                    memset(sdatagram, 0, datagramLen);
                    packageData(sdatagram, sheader);
                    sendto(sockServer, sdatagram, headerLen, 0, (SOCKADDR*)&addrClient, sizeof(addrClient));
                }
            }
            else if (rheader.getFinishConnection()) {
                try {
                    sheader.setACKFinishConnection();
                    packageData(sdatagram, sheader);
                    sendto(sockServer, sdatagram, headerLen, 0, (SOCKADDR*)&addrClient, sizeof(addrClient));
                    printf("Agree to stop the connection from sender to receiver.\n");
                    sheader.clearFlags();
                    sheader.setFinishConnection();
                    memset(sdatagram, 0, datagramLen);
                    packageData(sdatagram, sheader);
                    sendto(sockServer, sdatagram, headerLen, 0, (SOCKADDR*)&addrClient, sizeof(addrClient));
                }
                catch (std::exception& e) {
                    e.what();
                }
                break;
            }
            else if (rheader.getIsFileEnd()) {
                if (inFile != "" && outFile != "") {
                    fw.close();
                    sheader.clearFlags();
                    sheader.setACKFileEnd();
                    memset(sdatagram, 0, datagramLen);
                    packageData(sdatagram, sheader);
                    int msg = sendto(sockServer, sdatagram, headerLen, 0, (SOCKADDR*)&addrClient, sizeof(addrClient));
                    if (msg > 0) {
                        cout << "Prepared to receive next file." << endl;
                    }
                }

            }
            else {
                if (checksum(rdatagram, len + headerLen)) {
                    char sequence = rheader.getSequenceNumber();
                    if (sequence == (lastSequence + 1) % 256) {
                        lastSequence++;
                        // recvBuffer.push_back(buffer);
                        if (inFile != "" && outFile != "") {
                            fw.write(buffer, len);
                            printf("successfully write file: %s, size: %d.\n", outFile.c_str(), len);
                        }
                        
                        sheader.clearFlags();
                        sheader.setSequenceNumber((sequence + 1) % 256);
                        sheader.setACK();
                        memset(sdatagram, 0, datagramLen);
                        packageData(sdatagram, sheader);
                        sendto(sockServer, sdatagram, headerLen, 0, (SOCKADDR*)&addrClient, sizeof(addrClient));
                    }
                    else {
                        sheader.clearFlags();
                        sheader.setSequenceNumber((lastSequence + 1) % 256);
                        sheader.setACK();
                        memset(sdatagram, 0, datagramLen);
                        packageData(sdatagram, sheader);
                        sendto(sockServer, sdatagram, headerLen, 0, (SOCKADDR*)&addrClient, sizeof(addrClient));
                    }
                }
                else {
                    cout << "checksum error for data" << endl;
                    continue;
                }
            }
        }
    }
    
    try {
        int msg = recvfrom(sockServer, rdatagram, datagramLen, 0, (SOCKADDR*)&addrClient, &fromLen);
        unpackageData(buffer, rheader, rdatagram);
        if (msg > 0 && rheader.getACKFinishConnection()) {
            printf("Successfully stop the connection from receiver to sender.\n");
        }
    }
    catch (std::exception& e) {
        cout << e.what() << endl;
    }
        

    delete[] buffer;
    delete[] sdatagram;
    delete[] rdatagram;
    closesocket(sockServer);
    WSACleanup();
    return 0;
}

