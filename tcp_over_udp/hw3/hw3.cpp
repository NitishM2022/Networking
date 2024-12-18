// hw3.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"

using namespace std;

int main(int argc, char** argv)
{
	if (argc != 8) {
		cout << "Usage: rdt <hostname> <power> <senderWindow> <rtt> <lossRate> <returnLossRate> <linkSpeed>" << endl;
		return 1;
	}

	// Initialize Winsock
	WSADATA wsaData;
	WORD wVersionRequested = MAKEWORD(2, 2);
	if (WSAStartup(wVersionRequested, &wsaData) != 0)
	{
		printf("WSAStartup error %d\n", WSAGetLastError());
		exit(-1);
	}

	// parse arguments
	char* targetHost = argv[1];
	int power = atoi(argv[2]);
	int senderWindow = atoi(argv[3]);

	LinkProperties lp;
	lp.RTT = atof(argv[4]);
	lp.speed = 1e6 * atof(argv[7]); // convert from megabits
	lp.pLoss[FORWARD_PATH] = atof(argv[5]);
	lp.pLoss[RETURN_PATH] = atof(argv[6]);

	printf("Main:    sender W = %d, RTT %.3f sec, loss %g / %g, link %.0f Mbps\n",
		senderWindow, lp.RTT, lp.pLoss[FORWARD_PATH], lp.pLoss[RETURN_PATH], lp.speed / 1e6);

	// create DWORD array
	UINT64 dwordBufSize = (UINT64)1 << power;
	DWORD* dwordBuf = new DWORD[dwordBufSize];

	cout << "Main:    initializing DWORD array with 2^" << power << " elements...";
	auto t = clock();
	for (UINT64 i = 0; i < dwordBufSize; i++) {
		dwordBuf[i] = i;
	}
	double elapsed = (double)(clock() - t) / CLOCKS_PER_SEC;
	cout << " done in " << (int)(elapsed * 1000) << " ms" << endl;

	// Open RDT Socket
	int status;
	SenderSocket ss(senderWindow);
	t = clock();
	if ((status = ss.Open(targetHost, MAGIC_PORT, &lp)) != STATUS_OK) {
		if (status != -1) {
			cout << "Main:    connect failed with status " << status << endl;
		}
		return 1;
	}
	elapsed = (double)(clock() - t) / CLOCKS_PER_SEC;
	printf("Main:    connected to %s in %.3f sec, pkt %d bytes\n", targetHost, elapsed, MAX_PKT_SIZE);

	char* charBuf = (char*)dwordBuf; // this buffer goes into socket
	UINT64 byteBufferSize = dwordBufSize << 2; // convert to bytes

	UINT64 off = 0;
	while (off < byteBufferSize)
	{
		int bytes = min(byteBufferSize - off, MAX_PKT_SIZE - sizeof(SenderDataHeader));
		if ((status = ss.Send(charBuf + off, bytes)) != STATUS_OK) {
			cout << "Main:    send failed with status " << status << endl;
		}
		off += bytes;
	}
	ss.senderDone = true;

	if ((status = ss.Close()) != STATUS_OK) {
		cout << "Main:    close failed with status " << status << endl;
		return 1;
	}

	Checksum cs;
	DWORD crc32 = cs.CRC32((unsigned char *)charBuf, byteBufferSize);

	printf("Main:\ttransfer finished in %.3f sec, %.2f Kbps, checksum %X\n", ss.sendTime, ss.rate, crc32);
	printf("Main:\testRTT %.3f, ideal rate %.2f Kbps\n", ss.estimatedRTT, (ss.W * 8 * (MAX_PKT_SIZE - sizeof(SenderDataHeader))) / (ss.estimatedRTT * 1e3));
}