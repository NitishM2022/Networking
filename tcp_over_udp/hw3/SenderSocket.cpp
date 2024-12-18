// Nitish Malluru
// CSCE 463
// Fall 2024

#include "pch.h"

using namespace std;

SenderSocket::SenderSocket(int senderWindow) : sock(INVALID_SOCKET), W(senderWindow) {
	FD_ZERO(&readSet);

	rate = 0;

	nextSeq = 0;
	senderBase = 0;
	num_to = 0;
	fast_rt = 0;

	bytes_acked = 0;

	sampleRTT = -1;
	estimatedRTT = -1;
	devRTT = -1;

	empty = CreateSemaphore(NULL, 0, W, NULL); // start at 0 cause we can choose how to much to make available through flow control
	full = CreateSemaphore(NULL, 0, W, NULL);

	workerDone = CreateEvent(NULL, TRUE, FALSE, NULL);
	killStats = CreateEvent(NULL, TRUE, FALSE, NULL);

	eventQuit = CreateEvent(NULL, TRUE, FALSE, NULL);
	socketReceiveReady = CreateEvent(NULL, FALSE, FALSE, NULL);
	pending_pkts = new Packet[W];
}

SenderSocket::~SenderSocket() {
	SetEvent(eventQuit);
	SetEvent(killStats);

	CloseHandle(worker);
	CloseHandle(recv);
	CloseHandle(send);
	CloseHandle(stats);

	CloseHandle(killStats);
	CloseHandle(workerDone);
	CloseHandle(empty);
	CloseHandle(full);
	CloseHandle(eventQuit);
	CloseHandle(socketReceiveReady);

	CloseSock();
	delete pending_pkts;
}

int SenderSocket::CloseSock() {
	if (sock != INVALID_SOCKET) {
		closesocket(sock);
		sock = INVALID_SOCKET;
		return STATUS_OK;
	}
	return NOT_CONNECTED;
}

int SenderSocket::Open(const char* targetHost, int port, LinkProperties* lp) {
	// UDP Setup
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == INVALID_SOCKET) {
		cout << "failed with " << WSAGetLastError() << endl;
		return -1;
	}

	this->lp = lp;
	lp->bufferSize = W + 3;
	rto = max(1, 2 * lp->RTT);

	// Initalize Local socket
	struct sockaddr_in local;
	memset(&local, 0, sizeof(local));
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = INADDR_ANY;
	local.sin_port = htons(0);

	startt = clock();
	if (bind(sock, (struct sockaddr*)&local, sizeof(local)) == SOCKET_ERROR) {
		auto t = clock() - startt;
		printf("[%.3f] --> failed bind with %d\n", (double)t / CLOCKS_PER_SEC, WSAGetLastError());

		CloseSock();
		return -1;
	}

	// Initalize Remote Socket
	memset(&remote, 0, sizeof(remote));
	remote.sin_family = AF_INET;

	DWORD ip = inet_addr(targetHost);
	if (ip == INADDR_NONE) {
		struct hostent* he = gethostbyname(targetHost);
		if (he != nullptr) {
			remote.sin_addr = *((struct in_addr*)he->h_addr);
		}
		else {
			auto t = clock() - startt;
			printf("[%.3f] --> target %s is invalid\n", (double)t / CLOCKS_PER_SEC, targetHost);

			CloseSock();
			return INVALID_NAME;
		}
	}
	else {
		remote.sin_addr.s_addr = ip;
	}

	remote.sin_port = htons(port);

	// Create SYN Packet
	char synPkt[MAX_PKT_SIZE];
	memset(synPkt, 0, sizeof(synPkt));
	int pkt_size = sizeof(SenderSynHeader);

	SenderSynHeader* synHeader = (SenderSynHeader*)synPkt;

	synHeader->sdh.flags = Flags();
	synHeader->sdh.flags.SYN = 1;
	synHeader->sdh.flags.magic = MAGIC_PROTOCOL;
	synHeader->sdh.seq = senderBase;

	synHeader->lp.RTT = lp->RTT;
	synHeader->lp.speed = lp->speed;
	synHeader->lp.pLoss[FORWARD_PATH] = lp->pLoss[FORWARD_PATH];
	synHeader->lp.pLoss[RETURN_PATH] = lp->pLoss[RETURN_PATH];
	synHeader->lp.bufferSize = lp->bufferSize;

	WSAEventSelect(sock, socketReceiveReady, FD_READ);

	for (int i = 1; i <= MAX_RETX; i++) {
		auto t = clock();
		// send SYN
		int bytes = sendto(sock, synPkt, pkt_size, 0, (struct sockaddr*)&remote, sizeof(remote));
		if (bytes == SOCKET_ERROR) {
			t = clock() - startt;
			printf("[%4.2f] --> failed sendto with %d\n", (double)t / CLOCKS_PER_SEC, WSAGetLastError());
			CloseSock();
			return FAILED_SEND;
		}

		// recv SYN-ACK
		char packet[MAX_PKT_SIZE];
		ReceiverHeader* rh = (ReceiverHeader*)packet;
		int status;
		WaitForSingleObject(socketReceiveReady, rto * 1000);
		if ((status = Read((char*)rh)) == STATUS_OK && rh->flags.SYN == 1 && rh->flags.ACK == 1) {
			double ackt = (clock() - t) / (double)CLOCKS_PER_SEC;
			CalcRTO(ackt);

			effectiveWin = min(W, rh->recvWnd);

			lastReleased = effectiveWin;
			ReleaseSemaphore(empty, lastReleased, NULL);



			int kernelBuffer = 20e6;
			setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char*)&kernelBuffer, sizeof(int));
			setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char*)&kernelBuffer, sizeof(int));

			#ifndef EXTRA_CREDIT
			worker = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)&WorkerRun, this, 0, NULL);
			#else
			send = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)&WorkerSend, this, 0, NULL);
			recv = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)&WorkerRecv, this, 0, NULL);
			#endif 

			startt = clock() / (double)CLOCKS_PER_SEC;
			sendTime = clock() / (double)CLOCKS_PER_SEC;
			stats = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)&PrintStats, this, 0, NULL);
			return STATUS_OK;
		}
	}

	CloseSock();
	return TIMEOUT;
}

int SenderSocket::Send(const char* buffer, int bytes) {
	HANDLE arr[] = { eventQuit, empty };

	DWORD result = WaitForMultipleObjects(2, arr, FALSE, INFINITE);
	if (result == WAIT_OBJECT_0) {
		return FAILED_SEND;
	}

	Packet* p = &pending_pkts[nextSeq % W];
	memset(p, 0, sizeof(Packet));

	SenderDataHeader* sdh = (SenderDataHeader*)p->pkt;
	sdh->flags = Flags();
	sdh->seq = nextSeq;
	sdh->flags.magic = MAGIC_PROTOCOL;

	memcpy(sdh + 1, buffer, bytes);
	p->size = bytes + sizeof(SenderDataHeader);
	p->type = DATAT;

	nextSeq++;
	ReleaseSemaphore(full, 1, NULL);

	return STATUS_OK;
}

VOID WINAPI SenderSocket::WorkerRecv(LPVOID self) {
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
	SenderSocket* ss = (SenderSocket*)self;

	HANDLE events[] = { ss->socketReceiveReady, ss->workerDone, ss->eventQuit };

	int retx = 0;
	int dup = 0;
	int nextToSend = ss->senderBase;

	auto timerExpire = clock() + ss->rto * CLOCKS_PER_SEC;

	CRITICAL_SECTION revCs;
	InitializeCriticalSection(&revCs);

	while (true)
	{
		DWORD timeout =  1000 * (timerExpire - clock())/CLOCKS_PER_SEC;

		if (ss->senderDone && ss->senderBase == ss->nextSeq) {
			SetEvent(ss->workerDone);
			DeleteCriticalSection(&revCs);
			return;
		}

		int ret = WaitForMultipleObjects(3, events, false, timeout);
		switch (ret)
		{
		case WAIT_TIMEOUT: {// retx base
			EnterCriticalSection(&revCs);
			Packet* p = &ss->pending_pkts[ss->senderBase % ss->W];
			retx++;
			ss->num_to++;

			if (retx >= MAX_RETX) {
				SetEvent(ss->eventQuit);
				break;
			}

			sendto(ss->sock, p->pkt, p->size, 0, (struct sockaddr*)&ss->remote, sizeof(ss->remote));
			p->txTime = clock();

			timerExpire = clock() + ss->rto * CLOCKS_PER_SEC;
			LeaveCriticalSection(&revCs);
			break;
		}
		case WAIT_OBJECT_0: {// recv ACK (move senderBase; update RTT; handle fast retx; do flow control)
			EnterCriticalSection(&revCs);
			char ACKpkt[MAX_PKT_SIZE];
			ReceiverHeader* rh;

			if (ss->Read(ACKpkt) == STATUS_OK) {
				rh = (ReceiverHeader*)ACKpkt;

				if (rh->ackSeq > ss->senderBase) {
					Packet* p = &ss->pending_pkts[(rh->ackSeq - 1) % ss->W];

					if (retx == 0) {
						double sRTT = (clock() - p->txTime) / (double)CLOCKS_PER_SEC;
						ss->CalcRTO(sRTT);
					}

					ss->bytes_acked += (rh->ackSeq - ss->senderBase) * (MAX_PKT_SIZE - sizeof(SenderDataHeader));
					ss->senderBase = rh->ackSeq;
					if (ss->senderBase != ss->nextSeq) {
						timerExpire = clock() + ss->rto * CLOCKS_PER_SEC;
					}
					ss->effectiveWin = min(ss->W, rh->recvWnd);

					int newReleased = ss->senderBase + ss->effectiveWin - ss->lastReleased;
					ReleaseSemaphore(ss->empty, newReleased, NULL);
					ss->lastReleased += newReleased;

					retx = 0;
					dup = 0;
				}
				else if (rh->ackSeq == ss->senderBase) {
					if (++dup == 3) {
						Packet* p = &ss->pending_pkts[ss->senderBase % ss->W];

						sendto(ss->sock, p->pkt, p->size, 0, (struct sockaddr*)&ss->remote, sizeof(remote));
						p->txTime = clock();

						timerExpire = clock() + ss->rto * CLOCKS_PER_SEC;
						ss->fast_rt++;
					}
				}
			}
			LeaveCriticalSection(&revCs);
			break;
		}
		case WAIT_OBJECT_0 + 1:
			DeleteCriticalSection(&revCs);
			return;
		default:
			DeleteCriticalSection(&revCs);
			SetEvent(ss->workerDone);
			return;
		}
	}
}

VOID WINAPI SenderSocket::WorkerSend(LPVOID self) {
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
	SenderSocket* ss = (SenderSocket*)self;

	HANDLE events[] = { ss->full, ss->workerDone, ss->eventQuit };

	int retx = 0;
	int dup = 0;
	int nextToSend = ss->senderBase;

	auto timerExpire = clock();

	CRITICAL_SECTION sendCs;
	InitializeCriticalSection(&sendCs);

	while (true)
	{
		DWORD timeout = INFINITE;
		int ret = WaitForMultipleObjects(3, events, false, timeout);
		switch (ret)
		{
		case WAIT_OBJECT_0: {
			EnterCriticalSection(&sendCs);
			Packet* p = &ss->pending_pkts[nextToSend % ss->W];

			sendto(ss->sock, p->pkt, p->size, 0, (struct sockaddr*)&ss->remote, sizeof(remote));
			p->txTime = clock();

			if (nextToSend == ss->senderBase) {
				timerExpire = clock();
			}

			nextToSend++;
			LeaveCriticalSection(&sendCs);
			break;
		}
		case WAIT_OBJECT_0 + 1:
			DeleteCriticalSection(&sendCs);
			return;
		default:
			DeleteCriticalSection(&sendCs);
			SetEvent(ss->workerDone);
			return;
		}
	}
}

VOID WINAPI SenderSocket::WorkerRun(LPVOID self) {
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
	SenderSocket* ss = (SenderSocket*)self;

	HANDLE events[] = { ss->socketReceiveReady, ss->full, ss->workerDone, ss->eventQuit };

	int retx = 0;
	int dup = 0;
	int nextToSend = ss->senderBase;

	auto timerExpire = clock() + ss->rto * CLOCKS_PER_SEC;


	while (true)
	{
		DWORD timeout = 1000 * (timerExpire - clock())/CLOCKS_PER_SEC;

		if (ss->senderBase == ss->nextSeq) {
			if (ss->senderDone) {
				SetEvent(ss->workerDone);
				return;
			}
			else {
				timeout = INFINITE;
			}
		}


		int ret = WaitForMultipleObjects(4, events, false, timeout);
		switch (ret)
		{
		case WAIT_TIMEOUT: {// retx base
			Packet* p = &ss->pending_pkts[ss->senderBase % ss->W];
			retx++;
			ss->num_to++;

			if (retx >= MAX_RETX) {
				SetEvent(ss->eventQuit);
				break;
			}

			sendto(ss->sock, p->pkt, p->size, 0, (struct sockaddr*)&ss->remote, sizeof(ss->remote));
			p->txTime = clock();

			timerExpire = clock() + ss->rto * CLOCKS_PER_SEC;
			break;
		}
		case WAIT_OBJECT_0: {// recv ACK (move senderBase; update RTT; handle fast retx; do flow control)
			char ACKpkt[MAX_PKT_SIZE];
			ReceiverHeader* rh;

			if (ss->Read(ACKpkt) == STATUS_OK) {
				rh = (ReceiverHeader*)ACKpkt;

				if (rh->ackSeq > ss->senderBase) {
					Packet* p = &ss->pending_pkts[(rh->ackSeq - 1) % ss->W];

					if (retx == 0) {
						double sRTT = (clock() - p->txTime) / (double) CLOCKS_PER_SEC;
						ss->CalcRTO(sRTT);
					}

					ss->bytes_acked += (rh->ackSeq - ss->senderBase) * (MAX_PKT_SIZE - sizeof(SenderDataHeader));
					ss->senderBase = rh->ackSeq;
					timerExpire = clock() + ss->rto * CLOCKS_PER_SEC;

					ss->effectiveWin = min(ss->W, rh->recvWnd);

					int newReleased = ss->senderBase + ss->effectiveWin - ss->lastReleased;
					ReleaseSemaphore(ss->empty, newReleased, NULL);
					ss->lastReleased += newReleased;

					retx = 0;
					dup = 0;
				}
				else if (rh->ackSeq == ss->senderBase) {
					if (++dup == 3) {
						Packet* p = &ss->pending_pkts[ss->senderBase % ss->W];

						sendto(ss->sock, p->pkt, p->size, 0, (struct sockaddr*)&ss->remote, sizeof(remote));
						p->txTime = clock();

						timerExpire = clock() + ss->rto * CLOCKS_PER_SEC;
						ss->fast_rt++;
						retx++;
					}
				}
			}
			break;
		}
		case WAIT_OBJECT_0 + 1: {
			Packet* p = &ss->pending_pkts[nextToSend % ss->W];

			sendto(ss->sock, p->pkt, p->size, 0, (struct sockaddr*)&ss->remote, sizeof(remote));
			p->txTime = clock();

			if (nextToSend == ss->senderBase) {
				timerExpire = clock() + ss->rto * CLOCKS_PER_SEC;
			}

			nextToSend++;
			break;
		}
		case WAIT_OBJECT_0 + 2:
			return;
		default:
			SetEvent(ss->workerDone);
			break;
		}
	}
}

int SenderSocket::Close() {
	if (sock == INVALID_SOCKET) {
		return NOT_CONNECTED;
	}


	// Create FIN Packet
	char packet[MAX_PKT_SIZE];
	memset(packet, 0, sizeof(packet));

	SenderSynHeader* synHeader = (SenderSynHeader*)packet;
	synHeader->sdh.flags = Flags();
	synHeader->sdh.flags.FIN = 1;
	synHeader->sdh.flags.magic = MAGIC_PROTOCOL;
	synHeader->sdh.seq = max(nextSeq, 0);

	WaitForSingleObject(workerDone, INFINITE);
	sendTime = (clock() - startt) / (double)CLOCKS_PER_SEC - sendTime;

	SetEvent(killStats);

	for (int attempt = 1; attempt <= MAX_RETX; attempt++) {
		auto t = clock() - startt;

		// send FIN
		int bytes = sendto(sock, packet, sizeof(SenderSynHeader), 0, (struct sockaddr*)&remote, sizeof(remote));
		if (bytes == SOCKET_ERROR) {
			auto t = clock() - startt;
			printf("[%4.2f] --> failed sendto with %d\n", (double)t / CLOCKS_PER_SEC, WSAGetLastError());

			CloseSock();
			return FAILED_SEND;
		}

		// recv FIN-ACK
		char packet[MAX_PKT_SIZE];
		memset(packet, 0, sizeof(packet));

		WaitForSingleObject(socketReceiveReady, rto * 1000);
		int status = Read(packet);
		ReceiverHeader* rh = (ReceiverHeader*)packet;
		if (status == STATUS_OK && rh->flags.FIN == 1 && rh->flags.ACK == 1) {
			auto ackt = clock() - startt;

			printf("[%4.3f] <-- FIN-ACK %d window %X\n", (double)ackt / CLOCKS_PER_SEC, nextSeq, rh->recvWnd);
			CloseSock();
			return STATUS_OK;
		}
	}

	CloseSock();
	return TIMEOUT;
}

int SenderSocket::Read(char* buf) {
	int totalTime = 0;

	struct sockaddr_in response;
	int len = sizeof(response);

	int bytes = recvfrom(sock, buf, sizeof(ReceiverHeader), 0, (struct sockaddr*)&response, &len);

	if (bytes > 0)
	{
		if (response.sin_addr.s_addr == remote.sin_addr.s_addr && response.sin_port == remote.sin_port)
		{
			return STATUS_OK;
		}
		else {
			auto t = clock() - startt;
			//printf("[%4.2f] --> failed with packet from unknown source\n", (double)t / CLOCKS_PER_SEC);
			return FAILED_RECV;
		}
	}
	else if (bytes == 0)
	{
		auto t = clock() - startt;
		//printf("[%4.2f] --> failed server pre-emptively closing connection\n", (double)t / CLOCKS_PER_SEC);
		return FAILED_RECV;
	}
	else
	{
		auto t = clock() - startt;
		//printf("[%4.2f] --> failed recvfrom with %d\n", (double)t / CLOCKS_PER_SEC, WSAGetLastError());
		return FAILED_RECV;
	}

}

VOID WINAPI SenderSocket::PrintStats(LPVOID self) {
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
	SenderSocket* ss = (SenderSocket*)self;

	auto start = clock();
	int prev_base = ss->senderBase;

	HANDLE events[] = { ss->killStats, ss->eventQuit};

	while (true) {
		int ret = WaitForMultipleObjects(2, events, FALSE, 2000);
		if (ret != WAIT_TIMEOUT) {
			break;
		}
		// MB acked
		double elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;
		long double mb_acked = ss->bytes_acked / 1e6;

		// speed 
		int diff = ss->senderBase - prev_base;
		double speed = diff * (8.0 * (MAX_PKT_SIZE - sizeof(SenderDataHeader))) / 1e6;

		printf("[%2d] B %7d ( %4.1f MB) N %6d T %d F %d W %d S %0.3f Mbps RTT %.3f\n",
			(int)floor(elapsed),
			ss->senderBase,
			mb_acked,
			ss->nextSeq,
			ss->num_to,
			ss->fast_rt,
			ss->effectiveWin,
			speed,
			ss->estimatedRTT);

		prev_base = ss->senderBase;
	}
	double elapsed = (clock() - ss->startt) / (double) CLOCKS_PER_SEC;
	ss->rate = ss->bytes_acked * 8.0 / (1e3 * elapsed);
}
