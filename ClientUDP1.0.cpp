#define NOMINMAX
#include <iostream>
#include <WinSock2.h>
#include <windows.h>
#include <stdio.h>
#include <cstring>
#include <fstream>
#pragma comment(lib, "ws2_32.lib")

using namespace std;

const int maxLen = 256;

const int headerLen = 8;

// 总共有64位, 8个字节
struct datagramHeader {
    unsigned char checksum[2] = {0};
    unsigned char flags = 0; // 只需要用6个bit N/FN/E/A/P/R/S/F
    unsigned char dataLen = 0;
    unsigned char dstPort[2] = {0};
    unsigned char rscPort[2] = {0};
    datagramHeader(USHORT dst, USHORT rsc);
    datagramHeader();

    void printInfo();

    bool setACK();
    bool setSequenceNumber(bool);
    bool setConnection();
    bool setACKConnection();
    bool setACKFinishConnection();
    bool setFinishConnection();
    bool setIsFileName(bool);

    bool getACK();
    bool getIsFileName();
    bool getSequenceNumber();
    bool getACKConnection();
    bool getACKFinishConnection();
    bool getConnection();
    bool getFinishConnection();
    
    void setDstPort(USHORT);
    void setRscPort(USHORT);
    USHORT getDstPort();
    USHORT getRscPort();
    void clearFlags() {
        flags = 0;
    }
};

datagramHeader::datagramHeader(USHORT dst, USHORT rsc) 
{
    dstPort[1] = dst % 256;
    dstPort[0] = (dst / 256) % 256;
    rscPort[1] = rsc % 256;
    rscPort[0] = (rsc / 256) % 256;
}

datagramHeader::datagramHeader() {}

void datagramHeader::printInfo() {
    std::cout << "checksum: " << (USHORT)this->checksum[0] % 256 << (USHORT)this->checksum[1] << endl;
    std::cout << "flags: " << (USHORT)this->flags % 256 << endl;
    std::cout << "dataLen: " << (USHORT)this->dataLen % 256 << endl;
}

void datagramHeader::setRscPort(USHORT rcv)
{
    rscPort[1] = rcv % 256;
    rscPort[0] = (rcv / 256) % 256;
}

void datagramHeader::setDstPort(USHORT dst)
{
    dstPort[1] = dst % 256;
    dstPort[0] = (dst / 256) % 256;
}

USHORT datagramHeader::getRscPort()
{
    USHORT res = (USHORT)rscPort[1] % 256;
    res += ((USHORT)rscPort[0] % 256) * 256;
    return res;
}

USHORT datagramHeader::getDstPort() 
{
    USHORT res = (USHORT)dstPort[1] % 256;
    res += ((USHORT)dstPort[0] % 256) * 256;
    return res;
}

bool datagramHeader::getACK() {
    if ((USHORT)flags % 256 == 16 || (USHORT)flags % 256 == 144 || 
        (USHORT)flags % 256 == 208 || (USHORT)flags % 256 == 80) {
        return true;
    }
    return false;
}

bool datagramHeader::getSequenceNumber() {
    if (((USHORT)flags % 256) >> 7) {
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

bool datagramHeader::getACKFinishConnection() {
    if (flags == 17) {
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

bool checkIpAddr(const char* s) {
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

bool checkPortAddr(const char* s, USHORT& port) {
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

void packageData(char* d, const char* content, int dataLen, datagramHeader header) {
    d[0] = header.checksum[0];
    d[1] = header.checksum[1];
    d[2] = header.flags;
    d[3] = dataLen;
    d[4] = header.dstPort[0];
    d[5] = header.dstPort[1];
    d[6] = header.rscPort[0];
    d[7] = header.rscPort[1];
    for (int i = 0; i < dataLen; i++) {
        d[8 + i] = content[i];
    }
}

void unpackageData(char* content, datagramHeader &header, const char* s) {
    header.checksum[0] = s[0];
    header.checksum[1] = s[1];
    header.flags = s[2];
    header.dataLen = s[3];
    header.dstPort[0] = s[4];
    header.dstPort[1] = s[5];
    header.rscPort[0] = s[6];
    header.rscPort[1] = s[7];
    //// 判断dataLen是0还是256
    //int len = (USHORT)s[3] % 256;
    //if (len == 0) {
    //    if (s[8] != '\0') {
    //        len = 256;
    //    }
    //}
    //for (int i = 0; i < len; i++) {
    //    content[i] = s[i + 8];
    //}
}

void printDatagram(const char* buffer, datagramHeader header) {
    std::cout << "header: " << endl;
    header.printInfo();
    std::cout << "data content: " << endl;
    std::cout << buffer << endl;
}

USHORT computeChecksum(char* data, int len) {
	USHORT sum = 0;
	bool flag;
	for (int i = 2; i < len; i++) {
		USHORT tmp = (USHORT)data[i] % 256;
		if (sum + tmp < sum) {
			sum = sum + tmp + 1;
		}
		else {
			sum = sum + tmp;
		}
	}
	return ~sum;
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
        return -1;
    }

    // 判断版本是否符合要求
    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
        WSACleanup();
        printf("Socket doesn't support Version 2.2. Any key return. \n");
        getchar();
        return -1;
    }

    // 建立套接字
    SOCKET sockClient = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockClient == INVALID_SOCKET) {
        WSACleanup();
        printf("Socket open error. Any key return. \n");
        getchar();
        return -1;
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
        // cin.ignore((std::numeric_limits< streamsize >::max)(), '\n');
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
        // cin.ignore((std::numeric_limits< streamsize >::max)(), '\n');
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
    
    // 发起连接请求
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
    sdatagram[8] = 128;
    printDatagram(buffer, sheader);
    
    while (true) {
        int scon = sendto(sockClient, sdatagram, datagramLen, 0, (SOCKADDR*)&addrServer, sizeof(addrServer));
        if (scon > 0) {
            printf("successfully send connection request, size: %d. \n", scon);
        }
        int rcon = recvfrom(sockClient, rdatagram, datagramLen, 0, (SOCKADDR*)&addrServer, &fromLen);
        if (rcon > 0) {
            memset(buffer, 0, maxLen + 1);
            unpackageData(buffer, rheader, rdatagram);
            buffer[strlen(buffer)] = '\0';
            printDatagram(buffer, rheader);
            bool ACK = rheader.getACKConnection();
            if(ACK){
                printf("successfully shake hands!\n");
                break;
            }
        }
    }
    sheader.setACKConnection();
    memset(buffer, 0, maxLen + 1);
    packageData(sdatagram, buffer, 0, sheader);
    sdatagram[8] = '\0';
    int scon = sendto(sockClient, sdatagram, datagramLen, 0, (SOCKADDR*)&addrServer, sizeof(addrServer));
    if (scon > 0) {
        printf("successfully connected!\n");
    }

    // 输入要读取的文件的路径，如果输入为exit，则程序结束
    char* filePath = new char[maxLen + 1];
    std::cout << "filePath>";
    bool sequenceNum = false;
    bool error = false, error5 = false;
    while (std::cin.getline(filePath, maxLen)) {
        if (!strcmp(filePath, "exit")) {
            sheader.clearFlags();
            sheader.setFinishConnection();
            memset(sdatagram, 0, datagramLen);
            memset(buffer, 0, maxLen + 1);
            packageData(sdatagram, buffer, 0, sheader);
            sdatagram[8] = '\0';
            sendto(sockClient, sdatagram, datagramLen, 0, (SOCKADDR*)&addrServer, sizeof(addrServer));
            int msg = recvfrom(sockClient, rdatagram, datagramLen, 0, (SOCKADDR*)&addrServer, &fromLen);
            if (msg > 0) {
                unpackageData(buffer, rheader, rdatagram);
                if (rheader.getACKFinishConnection()) {
                    printf("Successfully stop the connection from sender to receiver.\n");
                    break;
                }
            }
            std::cout << "filePath>";
            continue;
        }
        if (!strcmp(filePath, "error")) {
            error = true;
            const char errorPath[] = "E:\\Network\\test1\\test.txt";
            for (int i = 0; i < strlen(errorPath); i++) {
                filePath[i] = errorPath[i];
            }
            filePath[strlen(errorPath)] = '\0';
        }
        if (!strcmp(filePath, "error+")) {
            error5 = true;
            const char errorPath[] = "E:\\Network\\test1\\test.txt";
            for (int i = 0; i < strlen(errorPath); i++) {
                filePath[i] = errorPath[i];
            }
            filePath[strlen(errorPath)] = '\0';
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

        // 装包并计算校验和
        packageData(sdatagram, filePath, strlen(filePath), sheader);
        USHORT checksum = computeChecksum(sdatagram, strlen(filePath) + 8);
        sdatagram[0] = checksum >> 8;
	    sdatagram[1] = checksum % 256;

        // 发送文件名
        int resendCount = 0;
        while (true) {
            if (resendCount > 5) { 
                std::cout << "too many resend!" << endl;
                break; 
            }
            resendCount++;
            int scon = sendto(sockClient, sdatagram, datagramLen, 0, (SOCKADDR*)&addrServer, sizeof(addrServer));
            if (scon > 0) {
                printf("successfully send file name, size: %d. \n", scon);
            }
            memset(rdatagram, 0, datagramLen);
            int rcon = recvfrom(sockClient, rdatagram, datagramLen, 0, (SOCKADDR*)&addrServer, &fromLen);
            if (rcon > 0) {
                memset(buffer, 0, maxLen + 1);
                unpackageData(buffer, rheader, rdatagram);
                buffer[strlen(buffer)] = '\0';
                bool ACK = rheader.getIsFileName() && rheader.getACK() && !rheader.getSequenceNumber();
                if (ACK) {
                    printf("file name is received successfully.\n");
                    break;
                }
            }
        }

        fr.open(filePath, ios::binary, 0);
        
        bool resend = false;
        int pos = 0;
        fr.clear();
        fr.seekg(0, fr.end);
        int fileLen = fr.tellg();
        fr.clear();
        fr.seekg(0, fr.beg);
        int count = 0;
        resendCount = 0;
        int dataLen = maxLen;

        // 发送与接收消息
        while ((!fr.eof()) && pos < fileLen) {
            std::cout << "pack" << count++ << endl;
            memset(buffer, 0, maxLen + 1);
            if (resend) {
                if (resendCount > 5) {
                    std::cout << "too many resend!" << endl;
                    break;
                }
                resendCount++;
                // 读文件
                pos -= dataLen;
                fr.clear();
                fr.seekg(pos);
                fr.read(buffer, dataLen);
                pos += dataLen;
                fr.clear();
                fr.seekg(pos);
                buffer[maxLen] = '\0';
                std::cout << "resend: ";
                std::cout << dataLen << endl;
                resend = false;
            }
            else {
                if (pos + maxLen < fileLen) {
                    dataLen = maxLen;
                }
                else {
                    dataLen = fileLen - pos;
                }
                fr.read(buffer, dataLen);
                pos += dataLen;
                fr.clear();
                fr.seekg(pos);
                buffer[maxLen] = '\0';
                std::cout << "send: ";
                std::cout << dataLen << endl;
                // dataLen = fr.gcount();
                // std::cout << buffer << endl;
            }

            // 设置标志位
            sheader.clearFlags();
            std::cout << "current sequence number: " << sequenceNum << endl;
            sheader.setSequenceNumber(sequenceNum);
            // 装包并计算校验和
            packageData(sdatagram, buffer, dataLen, sheader);
            USHORT checksum = computeChecksum(sdatagram, dataLen + 8);
            sdatagram[0] = checksum >> 8;
            sdatagram[1] = checksum % 256;
            if (error) {
                sdatagram[9] = 'X';
                error = false;
            }
            if (error5) {
                sdatagram[9] = 'X';
            }
            int sendmsg = sendto(sockClient, sdatagram, datagramLen, 0, (SOCKADDR*)&addrServer, sizeof(addrServer));
            if (sendmsg > 0) {
                printf("successfully send, size: %d. \n", sendmsg);
            }
            
            memset(buffer, 0, maxLen + 1);
            int recvmsg = recvfrom(sockClient, rdatagram, datagramLen, 0, (SOCKADDR*)&addrServer, &fromLen);
            if (recvmsg > 0) {
                unpackageData(buffer, rheader, rdatagram);
                buffer[strlen(buffer)] = '\0';
                if (rheader.getSequenceNumber() == sequenceNum) {
                    printf("successfully acknowledged! sequenceNum: %d.\n", sequenceNum);
                    sequenceNum = !sequenceNum;
                }
                else {
                    resend = true;
                }
            }
            else if (recvmsg == 0) {
                printf("Connection stop. \n");
            }
            else if (recvmsg == SOCKET_ERROR) {
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
            }
        }
        fr.close();
        std::cout << "filePath>";
        std::cin.clear();
    }
    
    // 断开连接
    int msg = recvfrom(sockClient, rdatagram, datagramLen, 0, (SOCKADDR*)&addrServer, &fromLen);
    if (msg > 0) {
        memset(buffer, 0, maxLen + 1);
        unpackageData(buffer, rheader, rdatagram);
        if (rheader.getFinishConnection()) {
            sheader.clearFlags();
            sheader.setACKFinishConnection();
            memset(sdatagram, 0, datagramLen);
            packageData(sdatagram, buffer, 0, sheader);
            sdatagram[8] = '\0';
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

