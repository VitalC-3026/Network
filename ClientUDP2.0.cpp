#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <Windows.h>
#include <process.h>

using namespace std;

bool flag = false;

DWORD WINAPI timer(LPVOID lparam){
	DWORD start_time = *((DWORD*)lparam);
	cout << "In Thread:" << endl;
	DWORD end_time;
	while (true) {
		end_time = GetTickCount();
		
		if (end_time - start_time > 2000) {
			cout << start_time << "\t" << end_time << "\t" << end_time - start_time << endl;
			flag = true;
		}
	}
}

unsigned _stdcall ntimer(PVOID param){
	DWORD start_time = *((DWORD*)param);
	cout << "In Thread:" << endl;
	DWORD end_time;
	while (true) {
		end_time = GetTickCount();

		if (end_time - start_time > 2000) {
			cout << start_time << "\t" << end_time << "\t" << end_time - start_time << endl;
			flag = true;
		}
	}
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
	cout << sum << " " << (USHORT)~sum << endl;
	return ~sum;
}

USHORT checksum(char* data, int len) {
	USHORT sum = 0;
	USHORT checksum1 = (USHORT)data[0] % 256 << 8;
	USHORT checksum2 = ((USHORT)data[1] % 256);
	
	for (int i = 2; i < len; i++) {
		USHORT tmp = (USHORT)data[i] % 256;
		if (sum + tmp < sum) {
			sum = sum + tmp + 1;
		}
		else {
			sum = sum + tmp;
		}
	}
	cout << sum << endl;
	sum += checksum1;
	sum += checksum2;
	/*if (sum == (USHORT)~0) {
		return true;
	}
	return false;*/
	return sum;
}

USHORT genCK(char* buf, int len)
{
	if(len % 2 == 1) { len++; }
	unsigned char* ubuf = (unsigned char*) buf;
	register ULONG sum = 0;
	while(len)
	{
		USHORT temp = USHORT(*ubuf << 8) + USHORT(*(ubuf + 1));
		sum += temp;
		if (sum & 0xFFFF0000)
		{
			sum &= 0xFFFF;
			sum++;
		}
		ubuf += 2;
		len -= 2;
	}
	return ~(sum & 0xFFFF);
}

int main() {
	vector<pair<char*, USHORT>> recvBuffer;
	string filePath = "E:\\Network\\test1\\test.jpg";
	fstream is(filePath.c_str(), ios::in|ios::binary);
	is.seekg(0, is.end);
	int len = is.tellg();
	is.seekg(0, is.beg);
	int pos = 0, dataLen = 0;
	int maxLen = 1024;
	char* contentBuffer = new char[len];
	char* p = contentBuffer;
	is.read(contentBuffer, len);
	while (pos < len) {

		if (pos + maxLen < len) {
			pos += maxLen;
		}
		else {
			dataLen = len - pos;
			pos += len - pos;
		}
		recvBuffer.push_back(make_pair(p, (USHORT)dataLen));
		p = p + dataLen;
	}
	for (vector<pair<char*, USHORT>>::iterator iter = recvBuffer.begin(); iter != recvBuffer.end(); iter++) {
		char* content = (*iter).first;
		USHORT contentLen = (*iter).second;
		char* contentH = new char[contentLen + 2];
		char* contentO = new char[contentLen + 2];
		contentO[0] = contentO[1] = '\0';
		for (int i = 2; i < contentLen + 2; i++) {
			contentH[i] = content[i - 2];
			contentO[i] = content[i - 2];
		}
		USHORT sum = computeChecksum(contentO, contentLen + 2);
		cout << sum << endl;
		contentH[0] = sum >> 8;
		contentH[1] = sum % 256;
		USHORT res = checksum(contentH, contentLen + 2);
		cout << res << endl;
	}
	
	return 0;
}
