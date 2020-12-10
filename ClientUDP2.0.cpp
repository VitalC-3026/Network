#include <iostream>
#include <WinSock2.h>
#include <windows.h>
#include <stdio.h>
#include <cstring>
#include <fstream>
#include <vector>
#include <process.h>
#include <utility>
#include <mutex>
#pragma comment(lib, "ws2_32.lib")

using namespace std;

const int maxLen = 1024;
const int headerLen = 5;
const int bufferLen = 65536; // 64KB
const int windowSize = 128; // 发送端窗口
vector<pair<char*, USHORT>> recbuffer; // 缓冲区无限大，记录一下目前确认到了vector的第几个元素
int curBufferNum = 0;
unsigned char windowBase = '\0';
unsigned char nextSequence = '\0';
bool timeout = false;
bool fend = false;
bool exitRecvThread = false;
HANDLE timer;
HANDLE recvThread;
int quickResend = 0;
mutex gMutex;

struct params {
    SOCKET sockClient;
    SOCKADDR* addrServer;
};
// GBN 设置一个线程计时，设置一个线程
// while循环一直在发送，直到到了发送端的窗口大小还是缓冲区大小
// 主线程接收ack，阻塞状态，接收到之后就广播，关掉线程，重新开启线程，移动base， base要作为一个全局变量

struct datagramHeader 
{
    unsigned char checksum[2] = { 0 };
    unsigned char flagsDataLen[2] = { 0 }; // FILE/ACK/SYN/FIN/FileEnd/0/10位DataLen
    unsigned char sequenceNum = '\0'; // 8位序列号                                                                                                                                                                      
    void clearFlags() {
        flagsDataLen[0] &= 3; // 00000011
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
        if ((flagsDataLen[0] >> 6) % 2) { return windowBase + 1; }
        return false;
        return sequenceNum;
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
    bool getFileEnd() {
        if (flagsDataLen[0] == 8) { return true; }
        return false;
    }
    bool getACKFileEnd() {
        if (flagsDataLen[0] == 72) { return true; }
        return false;
    }
    int getDataLen() {
        int len = flagsDataLen[1];
        len += (flagsDataLen[0] &= 3) << 8;
    }
    char getSequenceNumber() { return sequenceNum; }
};

void packageData(char* s, const char* data, int len, datagramHeader header) 
{
    s[0] = header.checksum[0];
    s[1] = header.checksum[1];
    s[2] = header.flagsDataLen[0];
    s[3] = header.flagsDataLen[1];
    s[4] = header.sequenceNum;
    for (int i = 0; i < len; i++) {
        s[i + headerLen] = data[i];
    }
}

void unpackageData(datagramHeader &header, const char* r) 
{
    header.checksum[0] = r[0];
    header.checksum[1] = r[1];
    header.flagsDataLen[0] = r[2];
    header.flagsDataLen[1] = r[3];
    header.sequenceNum = r[4];
}

void printDatagram(char* data, datagramHeader header) 
{
    
}

USHORT computeChecksum(char* data, int len) 
{
    // cout << "computeChecksum" << len << endl;
    USHORT sum = 0;
    bool flag;
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
    // cout << sum << " " << (USHORT)~sum << endl;
    return ~sum;
}

bool checkIpAddr(const char* s) 
{
    int len = strlen(s);
    int res = 0, idx = -1, count = 0;
    for (int i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9') {
            if (s[i] != '.') {
                return false;
            }
            else {
                count++;
                if (i - idx == 4) {
                    int tmp = (s[i - 3] - '0') * 100 + (s[i - 2] - '0') * 10 + (s[i - 1] - '0');
                    if (tmp > 255 || tmp < 0) {
                        return false;
                    }
                }
                else if (i - idx > 4) {
                    return false;
                }
                idx = i;
            }
        }
    }
    if (count != 3) {
        return false;
    }
    return true;
}

bool checkPortAddr(const char* s, USHORT& port) 
{
    int len = strlen(s);
    int res = 0;
    for (int i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }
        res += (s[i] - '0') * pow(10, len - i - 1);
    }
    if (res < 0 || res > 65535) {
        return false;
    }
    port = res;
    return true;
}

// 计时器的线程函数
DWORD WINAPI ntimer(LPVOID param) 
{
    DWORD start_time = *((DWORD*)param);
    cout << "In Thread:" << endl;
    DWORD end_time;
    while (true) {
        end_time = GetTickCount();
        if (end_time - start_time > 30000) {
            cout << start_time << "\t" << end_time << "\t" << end_time - start_time << endl;
            Sleep(500);
            timeout = true;
        }
    }
}

DWORD WINAPI _stdcall recvMsg(LPVOID lparam) 
{
    params* param = (params*)lparam;
    int fromLen = sizeof(*(param->addrServer));
    while (!exitRecvThread) {
        char* datagram = new char[headerLen];
        memset(datagram, 0, headerLen);
        cout << "receiving..." << endl;
        int msg = recvfrom(param->sockClient, datagram, headerLen, 0, param->addrServer, &fromLen);
        datagramHeader header;
        unpackageData(header, datagram);
        if (msg > 0) {
            char sequence = header.getSequenceNumber();
            // if (sequence == windowBase + 1 || sequence < (windowBase + 128) % 256) {
            if (header.getACK() && sequence == windowBase + 1) {
                cout << "successfully send: " << (USHORT)windowBase % 256 << endl;
                gMutex.lock();
                windowBase++;
                curBufferNum++; // 从vector那里删掉信息
                quickResend = 0;
                // CloseHandle(timer);
                if (curBufferNum == recbuffer.size()) {
                    fend = true;
                }
                gMutex.unlock();
            }
            else {
                cout << "failed to send: " << (USHORT)windowBase % 256 << endl;
                quickResend++;
            }
        }
    }
    return 0;
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
    SOCKET sockClient = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockClient == INVALID_SOCKET) {
        WSACleanup();
        printf("Socket open error. Any key return. \n");
        getchar();
        return 0;
    }

    printf("Hello, sender!\n");
    SOCKADDR_IN addrServer;
    int fromLen = sizeof(addrServer);
    SOCKADDR_IN addrClient;
    addrClient.sin_family = AF_INET;
    addrServer.sin_family = AF_INET;

    // 输入IP地址并检查正确性
    printf("Please enter the destination IP and port.\n");
    char IpAddr[16] = { 0 };
    printf("IP:");
    std::cin.getline(IpAddr, 15, '\n');
    IpAddr[strlen(IpAddr)] = '\0';

    int count = 0;
    while (!checkIpAddr(IpAddr)) {
        printf("\nPlease retype a correct IP address.\nIP:");
        std::cin.clear();
        std::cin.getline(IpAddr, 15);
        IpAddr[strlen(IpAddr)] = '\0';
         cin.ignore((std::numeric_limits< streamsize >::max)(), '\n');
        count++;
        if (count >= 10) {
            printf("Too many times of erroneous Ip address. Any key to return. \n");
            getchar();
            return -1;
        }
    }

    // 输入端口号并检查正确性
    USHORT port = 0;
    printf("port:");
    char cport[6];
    std::cin.getline(cport, 6, '\n');


    count = 0;
    while (!checkPortAddr(cport, port)) {
        printf("\nPlease retype a correct port.\nport:");
        std::cin.clear();
        std::cin.getline(cport, 5);
        cport[strlen(cport)] = '\0';
         cin.ignore((std::numeric_limits< streamsize >::max)(), '\n');
        count++;
        if (count >= 10) {
            printf("Too many times of erroneous port. Any key to return. \n");
            getchar();
            return -1;
        }
    }
    addrServer.sin_port = htons(port);
    addrServer.sin_addr.S_un.S_addr = inet_addr(IpAddr);

    // 获取本地动态分配的端口号
    int lenClient = sizeof(addrClient);
    getsockname(sockClient, (SOCKADDR*)&addrClient, &lenClient);
    USHORT localPort = ntohs(addrClient.sin_port);

    char* buffer = new char[maxLen + 1];

    
    datagramHeader sheader;
    datagramHeader rheader;
    sheader.setConnection();
    int datagramLen = headerLen + maxLen;
    char* sdatagram = new char[datagramLen + 1];
    char* rdatagram = new char[datagramLen + 1];
    memset(sdatagram, 0, datagramLen);
    memset(rdatagram, 0, datagramLen);
    memset(buffer, 0, maxLen + 1);
    packageData(sdatagram, buffer, 0, sheader);
    
    // 发起连接请求
    while (true) {
        int scon = sendto(sockClient, sdatagram, headerLen, 0, (SOCKADDR*)&addrServer, sizeof(addrServer));
        if (scon > 0) {
            printf("successfully send connection request, size: %d. \n", scon);
        }
        int rcon = recvfrom(sockClient, rdatagram, headerLen, 0, (SOCKADDR*)&addrServer, &fromLen);
        if (rcon > 0) {
            memset(buffer, 0, maxLen + 1);
            unpackageData(rheader, rdatagram);
            bool ACK = rheader.getACKConnection();
            if (ACK) {
                printf("successfully shake hands!\n");
                break;
            }
        }
    }
    sheader.setACKConnection();
    sheader.setDataLen(0);
    memset(buffer, 0, maxLen + 1);
    packageData(sdatagram, buffer, 0, sheader);
    int scon = sendto(sockClient, sdatagram, headerLen, 0, (SOCKADDR*)&addrServer, sizeof(addrServer));
    if (scon > 0) {
        printf("successfully connected!\n");
    }
    memset(rdatagram, 0, datagramLen);
    // 输入要读取的文件的路径，如果输入为exit，则程序结束
    char* filePath = new char[maxLen + 1];
    memset(filePath, 0, maxLen + 1);
    std::cout << "filePath>";
    bool error = false, error5 = false;
    while (std::cin.getline(filePath, maxLen)) {
        if (!strcmp(filePath, "exit")) {
            sheader.clearFlags();
            sheader.setFinishConnection();
            sheader.setDataLen(0);
            memset(sdatagram, 0, datagramLen);
            memset(buffer, 0, maxLen + 1);
            packageData(sdatagram, buffer, 0, sheader);
            sendto(sockClient, sdatagram, headerLen, 0, (SOCKADDR*)&addrServer, sizeof(addrServer));
            int msg = recvfrom(sockClient, rdatagram, headerLen, 0, (SOCKADDR*)&addrServer, &fromLen);
            if (msg > 0) {
                unpackageData(rheader, rdatagram);
                if (rheader.getACKFinishConnection()) {
                    printf("Successfully stop the connection from sender to receiver.\n");
                    break;
                }
            }
            std::cout << "filePath>";
            continue;
        }
        ifstream fr(filePath, ios::binary);
        if (fr.good()) {
            printf("File %s exists.\n", filePath);
        }
        else {
            printf("File %s doesn't exist.\n", filePath);
            std::cout << "filePath>";
            continue;
        }

        // 设置标志位
        sheader.clearFlags();
        sheader.setIsFileName(true);
        sheader.setDataLen(strlen(filePath));
        // 装包并计算校验和
        packageData(sdatagram, filePath, strlen(filePath), sheader);
        USHORT checksum = computeChecksum(sdatagram, strlen(filePath) + headerLen);
        sdatagram[0] = checksum >> 8;
        sdatagram[1] = checksum % 256;
        

        // 发送文件名
        int resendFileCount = 0;
        bool exit = false;
        while (true) {
            if (resendFileCount > 5) {
                std::cout << "too many resend!" << endl;
                exit = true;
                break;
            }
            
            int scon = sendto(sockClient, sdatagram, datagramLen, 0, (SOCKADDR*)&addrServer, sizeof(addrServer));
            if (scon > 0) {
                printf("successfully send file name, size: %d. \n", scon);
            }
            memset(rdatagram, 0, datagramLen);
            int rcon = recvfrom(sockClient, rdatagram, headerLen, 0, (SOCKADDR*)&addrServer, &fromLen);
            if (rcon > 0) {
                memset(buffer, 0, maxLen + 1);
                unpackageData(rheader, rdatagram);
                bool ACK = rheader.getIsFileName() && rheader.getACK();
                if (ACK) {
                    printf("file name is received successfully.\n");
                    break;
                }
            }
            resendFileCount++;
        }
        if (exit) {
            cout << "filePath>";
            continue;
        }
        // 读取文件内容，进行缓存
        fr.open(filePath, ios::binary, 0);
        int pos = 0;
        fr.clear();
        fr.seekg(0, fr.end);
        int fileLen = fr.tellg();
        fr.clear();
        fr.seekg(0, fr.beg);
        int dataLen = maxLen;
        char* contentBuffer = new char[fileLen];
        char* p = contentBuffer;
        fr.read(contentBuffer, fileLen);
        while (pos < fileLen) {
            if (pos + maxLen < fileLen) {
                pos += maxLen;
            }
            else {
                dataLen = fileLen - pos;
                pos += fileLen - pos;
            }
            recbuffer.push_back(make_pair(p, (USHORT)dataLen));
            p = p + dataLen;
        }
        

        // 发送与接收消息
        // 初始化需要用到的变量
        int serverAddrLen = sizeof(addrServer);
        
        // 创建线程发送数据
        struct params param;
        param.sockClient = sockClient;
        param.addrServer = (SOCKADDR*)&addrServer;
        DWORD recvThreadId;
        DWORD timerThreadId;
        // recvThread = (HANDLE)_beginthreadex(NULL, 0, recvMsg, (struct params*)&param, 0, &recvThreadId);
        recvThread = CreateThread(NULL, NULL, recvMsg, (struct params*)&param, 0, &recvThreadId);
        while (!fend) {
            // 封装数据包
            // 1° 发送数据包之前从缓冲区获取数据
            int offset = 0;
            int curWindowBase = windowBase;
            // 受刁兆琪启发，这里保存windowBase，万一从这里被调度，计算出来的offset也不会造成人为丢包，同时可以避免上锁，导致死锁局面
            if (nextSequence < curWindowBase) {
                offset = nextSequence + 256 - curWindowBase;
            }
            else {
                offset = nextSequence - curWindowBase;
            }
            // cout << "recbuffer size: " << recbuffer.size() << endl;
            if (curBufferNum + offset >= recbuffer.size()) {
                cout << "No datagram needs to be sent" << endl;
                Sleep(5000);
            }
            // 窗口大小，限制了继续发送新数据报
            if (offset < windowSize && curBufferNum + offset < recbuffer.size()) {
                gMutex.lock();
                char* dataStart = recbuffer.at(curBufferNum + offset).first;
                dataLen = recbuffer.at(curBufferNum + offset).second;
                gMutex.unlock();
                memset(buffer, 0, maxLen + 1);
                for (int i = 0; i < dataLen; i++) {
                    buffer[i] = dataStart[i];
                }
                // 2°设置报文头部
                sheader.clearFlags();
                sheader.setDataLen(dataLen);
                sheader.setSequenceNumber(nextSequence);
                // 3°封装数据报 计算校验和 （可删除packageData的len
                memset(sdatagram, 0, datagramLen);
                packageData(sdatagram, buffer, dataLen, sheader);
                USHORT sum = computeChecksum(sdatagram, dataLen + headerLen);
                sdatagram[0] = sum >> 8;
                sdatagram[1] = sum % 256;
                // 上锁 ?
                int msg = sendto(sockClient, sdatagram, datagramLen, 0, (SOCKADDR*)&addrServer, sizeof(addrServer));
                memset(buffer, 0, maxLen + 1);
                if (msg > 0) {
                    nextSequence++;
                    // 创建线程进行计时
                    /*if (nextSequence == windowBase + 1) {
                        DWORD current_time = GetTickCount();
                        timer = CreateThread(NULL, NULL, ntimer, (DWORD*)&current_time, 0, &timerThreadId);
                    }*/
                    
                }
            }
            
            if (quickResend == 3 || timeout) {
                CloseHandle(timer);
                quickResend = 0;
                timeout = false;
                int resendN = 0;
                if (nextSequence < windowBase) {
                    resendN = nextSequence + 256 - windowBase;
                }
                else {
                    resendN = nextSequence - windowBase;
                }
                int i;
                vector<pair<char*, USHORT>>::const_iterator iter;
                for (i = 0, iter = recbuffer.cbegin() + curBufferNum; i < resendN && iter != recbuffer.cend(); i++, iter++) {
                    USHORT len = (*iter).second;
                    memset(buffer, 0, maxLen + 1);
                    for (int j = 0; j < len; j++) {
                        buffer[j] = (*iter).first[j];
                    }
                    // 设置header
                    memset(sdatagram, 0, datagramLen);
                    sheader.clearFlags();
                    sheader.setDataLen(len);
                    sheader.setSequenceNumber(i + windowBase);
                    packageData(sdatagram, buffer, len, sheader);
                    sendto(sockClient, sdatagram, datagramLen, 0, (SOCKADDR*)&addrServer, sizeof(addrServer));
                    /*if (iter == recbuffer.cbegin() + curBufferNum) {
                        DWORD current_time = GetTickCount();
                        timer = (HANDLE)_beginthreadex(NULL, 0, ntimer, (DWORD*)&current_time, 0, &timerThreadId);
                    }*/
                }
                /*DWORD current_time = GetTickCount();
                timer = (HANDLE)_beginthreadex(NULL, 0, ntimer, (DWORD*)&current_time, 0, &timerThreadId);*/
            }
        }
        exitRecvThread = true;
        CloseHandle(recvThread);
        recbuffer.clear();
        delete[] contentBuffer;
        fr.close();
        sheader.clearFlags();
        sheader.setFileEnd();
        sheader.setDataLen(0);
        memset(sdatagram, 0, datagramLen);
        packageData(sdatagram, buffer, 0, sheader);
        int rcon = sendto(sockClient, sdatagram, datagramLen, 0, (SOCKADDR*)&addrServer, sizeof(addrServer));
        if (rcon > 0) {
            printf("Finish sending this file %s!\n", filePath);
        }
        gMutex.lock();
        windowBase = '\0';
        nextSequence = '\0';
        quickResend = 0;
        gMutex.unlock();
        std::cout << "filePath>";
        std::cin.clear();
    }
    // 断开连接
    int msg = recvfrom(sockClient, rdatagram, headerLen, 0, (SOCKADDR*)&addrServer, &fromLen);
    if (msg > 0) {
        memset(buffer, 0, maxLen + 1);
        unpackageData(rheader, rdatagram);
        if (rheader.getFinishConnection()) {
            sheader.clearFlags();
            sheader.setACKFinishConnection();
            memset(sdatagram, 0, datagramLen);
            packageData(sdatagram, buffer, 0, sheader);
            int msg = sendto(sockClient, sdatagram, datagramLen, 0, (SOCKADDR*)&addrServer, sizeof(addrServer));
            if (msg > 0) {
                printf("Agree to stop the connection from receiver to sender.\n");
            }
        }
    }
    delete[] filePath;
    delete[] buffer;
    delete[] sdatagram;
    delete[] rdatagram;
    closesocket(sockClient);
    WSACleanup();
    return 0;
}


