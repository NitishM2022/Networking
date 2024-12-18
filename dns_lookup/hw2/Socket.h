// Nitish Malluru
// CSCE 463
// Fall 2024

#ifndef SOCKET_H
#define SOCKET_H

#include "pch.h"

#define DNS_QUERY (0 << 15) /* 0 = query; 1 = response */
#define DNS_RESPONSE (1 << 15)
#define DNS_STDQUERY (0 << 11) /* opcode - 4 bits */
#define DNS_AA (1 << 10) /* authoritative answer */
#define DNS_TC (1 << 9) /* truncated */
#define DNS_RD (1 << 8) /* recursion desired */
#define DNS_RA (1 << 7) /* recursion available */

/* DNS query types */
#define DNS_A 1 /* name -> IP */
#define DNS_NS 2 /* name server */
#define DNS_CNAME 5 /* canonical name */
#define DNS_PTR 12 /* IP -> name */
#define DNS_HINFO 13 /* host info/SOA */
#define DNS_MX 15 /* mail exchange */
#define DNS_AXFR 252 /* request for zone transfer */
#define DNS_ANY 255 /* all records */

/* query classes */
#define DNS_INET 1 

#define DNS_OK 0 /* success */
#define DNS_FORMAT 1 /* format error (unable to interpret) */
#define DNS_SERVERFAIL 2 /* can’t find authority nameserver */
#define DNS_ERROR 3 /* no DNS entry */
#define DNS_NOTIMPL 4 /* not implemented */
#define DNS_REFUSED 5 /* server refused the query */ 

#define MAX_DNS_SIZE 512
#define MAX_DNS_JUMPS 50

#pragma pack(push,1) // sets struct padding/alignment to 1 byte
class QueryHeader {
public:
    USHORT qType;
    USHORT qClass;
};

class FixedDNSheader {
public:
    USHORT ID;
    USHORT flags;
    USHORT questions;
    USHORT answers;
    USHORT authority;
    USHORT additional;
};

class DNSanswerHdr {
public:
    u_short type;
    u_short class_;
    u_int TTL;
    u_short len;
};
#pragma pack(pop)

class Socket {
public:
    Socket();
    ~Socket();

    bool Bind();
    void makeDNSquestion(char* fdh, char* host);
    bool Send(std::string dns, char * host, unsigned short, bool isIp);

    bool Read(int txid);
    bool ParseDNSResponse(int txid, char* buf, int len);
    std::string ReadName(char* buf, int& curPos, int len);
    int PrintResourceRecord(char* buf, int curPos, int len);

    void Close();

private:
    SOCKET sock;        // socket handle
	struct sockaddr_in local;
    struct sockaddr_in remote;
    fd_set readSet;
    timeval timeout;
};

#endif
