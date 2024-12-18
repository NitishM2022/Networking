// Nitish Malluru
// CSCE 463
// Fall 2024

#ifndef SENDER_SOCKET_H
#define SENDER_SOCKET_H

#include "pch.h"

#define SYNT 0
#define FINT 1
#define DATAT 2

#define MAGIC_PORT 22345 // receiver listens on this port
#define MAX_PKT_SIZE (1500-28) // maximum UDP packet size accepted by receiver
#define MAGIC_PROTOCOL 0x8311AA

#define STATUS_OK 0 // no error
#define ALREADY_CONNECTED 1 // second call to ss.Open() without closing connection
#define NOT_CONNECTED 2 // call to ss.Send()/Close() without ss.Open()
#define INVALID_NAME 3 // ss.Open() with targetHost that has no DNS entry
#define FAILED_SEND 4 // sendto() failed in kernel
#define TIMEOUT 5 // timeout after all retx attempts are exhausted
#define FAILED_RECV 6 // recvfrom() failed in kernel

#define FORWARD_PATH 0
#define RETURN_PATH 1

#define MAX_RETX 50
#define NUM_THREADS 1
//#define EXTRA_CREDIT

#pragma pack(push,1) // sets struct padding/alignment to 1 byte
class LinkProperties {
public:
    // transfer parameters
    float RTT; // propagation RTT (in sec)
    float speed; // bottleneck bandwidth (in bits/sec)
    float pLoss[2]; // probability of loss in each direction
    DWORD bufferSize; // buffer size of emulated routers (in packets)
    LinkProperties() { memset(this, 0, sizeof(*this)); }
};

class Flags {
public:
    DWORD reserved : 5; // must be zero
    DWORD SYN : 1;
    DWORD ACK : 1;
    DWORD FIN : 1;
    DWORD magic : 24;
    Flags() { memset(this, 0, sizeof(*this)); magic = MAGIC_PROTOCOL; }
};

class SenderDataHeader {
public:
    Flags flags;
    DWORD seq; // must begin from 0
};

class SenderSynHeader {
public:
    SenderDataHeader sdh;
    LinkProperties lp;
};

class ReceiverHeader {
public:
    Flags flags;
    DWORD recvWnd; // receiver window for flow control (in pkts)
    DWORD ackSeq; // ack value = next expected sequence
};

class Packet {
public:
    int type; // SYN, FIN, data
    int size; // bytes in packet data
    clock_t txTime; // transmission time
    char pkt[MAX_PKT_SIZE];
};
#pragma pack(pop)


class SenderSocket {
public:
    double rate;
    double sendTime;
    double estimatedRTT;
    int W;

    SenderSocket(int senderWindow);
    ~SenderSocket();

    int Open(const char* targetHost, int port, LinkProperties* lp);
    int Send(const char* buffer, int bytes);
    int Close();

    bool senderDone;

private:
    int senderBase;
    int nextSeq;
    int num_to;
    int fast_rt;
    int send_wnd;
    //goodput speed at which application consumes data at the receiver

    double sampleRTT;
    double devRTT;
    double rto;

    int effectiveWin;
    long long bytes_acked;

    // Socket Vars
    SOCKET sock;
	struct sockaddr_in local;
    struct sockaddr_in remote;

    fd_set readSet;
    timeval timeout;

    // Connection Vars
    LinkProperties *lp;

    // Packet buffer
    Packet * pending_pkts;

    // Synchronization 
    HANDLE worker;
    HANDLE recv;
    HANDLE send;
    HANDLE stats;

    HANDLE killStats;
    HANDLE workerDone;
    HANDLE eventQuit;
    HANDLE empty;
    HANDLE full;
    HANDLE socketReceiveReady;

    // Flow Control
    int lastReleased;

    clock_t startt;

    static VOID WINAPI WorkerRecv(LPVOID self);
    static VOID WINAPI WorkerSend(LPVOID self);
    static VOID WINAPI WorkerRun(LPVOID self);
    static VOID WINAPI PrintStats(LPVOID self);

    int Read(char * buf);
    int CloseSock();

    void CalcRTO(double sRTT) {
        if (sampleRTT < 0){
            sampleRTT = sRTT;
            estimatedRTT = sRTT;
            devRTT = 0;
        }
        else {
            double alpha = 0.125;
            double beta = 0.25;

            sampleRTT = sRTT;
            estimatedRTT = (1 - alpha) * estimatedRTT + alpha * sampleRTT;
            devRTT = (1 - beta) * devRTT + beta * fabs(sampleRTT - estimatedRTT);
        }

        rto = estimatedRTT + 4 * max(devRTT, 0.010);
    }
};

#endif // SENDER_SOCKET_H
