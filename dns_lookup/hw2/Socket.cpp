// Nitish Malluru
// CSCE 463
// Fall 2024

#include "pch.h"

using namespace std;

Socket::Socket() : sock(INVALID_SOCKET) {
	FD_ZERO(&readSet);
	timeout.tv_sec = 10;
	timeout.tv_usec = 0;
}

Socket::~Socket() {
	Close();
}

void Socket::Close() {
	if (sock != INVALID_SOCKET) {
		closesocket(sock);
		sock = INVALID_SOCKET;
	}
}

bool Socket::Bind() {
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == INVALID_SOCKET) {
		cout << "failed with " << WSAGetLastError() << endl;
		return false;
	}

	struct sockaddr_in local;
	memset(&local, 0, sizeof(local));
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = INADDR_ANY;
	local.sin_port = htons(0);

	if (bind(sock, (struct sockaddr*)&local, sizeof(local)) == SOCKET_ERROR) {
		cout << "failed with " << WSAGetLastError() << endl;
		Close();
		return false;
	}

	return true;
}

void Socket::makeDNSquestion(char* buf, char* host) {
	int i = 0;
	char* next = host;
	while (*next != '\0') {
		char* dot = strchr(next, '.');

		int len;
		if (dot == NULL)
			len = strlen(next);
		else
			len = dot - next;

		buf[i++] = len;
		memcpy(buf + i, next, len);
		i += len;

		next += len;
		if (*next == '.')
			next++;
	}
	buf[i] = 0;
}

bool Socket::Send(string dns, char* host, unsigned short txid, bool isIp) {
	memset(&remote, 0, sizeof(remote));
	remote.sin_family = AF_INET;
	remote.sin_addr.s_addr = inet_addr(dns.c_str()); // server’s IP
	remote.sin_port = htons(53); // DNS port on server
	
	char packet[MAX_DNS_SIZE];
	memset(packet, 0, MAX_DNS_SIZE);

	int pkt_size = sizeof(FixedDNSheader) + strlen(host) + 2 + sizeof(QueryHeader);

	FixedDNSheader* fdh = (FixedDNSheader*)packet;
	fdh->ID = htons(txid);
	fdh->flags = htons(DNS_QUERY | DNS_RD | DNS_STDQUERY);
	fdh->questions = htons(1);
	fdh->answers = 0;
	fdh->authority = 0;
	fdh->additional = 0;

	makeDNSquestion(packet + sizeof(FixedDNSheader), host);

	QueryHeader* qh = (QueryHeader*)(packet + sizeof(FixedDNSheader) + strlen(host) + 2);
	qh->qType = htons(isIp ? DNS_PTR : DNS_A);
	qh->qClass = htons(DNS_INET);

	int bytes = sendto(sock, packet, pkt_size, 0, (struct sockaddr*)&remote, sizeof(remote));
	if (bytes == SOCKET_ERROR) {
		cout << "failed with " << WSAGetLastError() << " on send" << endl;
		return false;
	}

	cout << " with " << bytes << " bytes... ";
	return true;
}

bool Socket::Read(int txid) {
	int totalTime = 0;
	int start = GetTickCount64();

	FD_ZERO(&readSet); 
	FD_SET(sock, &readSet);
	int ret = select(0, &readSet, NULL, NULL, &timeout);

	if (ret > 0)
	{
		char buf[MAX_DNS_SIZE];

		struct sockaddr_in response;
		int len = sizeof(response);

		int bytes = recvfrom(sock, buf, MAX_DNS_SIZE, 0, (struct sockaddr*)&response, &len);
		cout << "response in " << (GetTickCount64() - start) << " ms with " << bytes << " bytes" << endl;


		if (bytes > 0)
		{
			if (response.sin_addr.s_addr == remote.sin_addr.s_addr && response.sin_port == remote.sin_port)
			{
				if (ParseDNSResponse(txid, buf, bytes)) {
					return true;
				}
				return false;
			}
			else {
				cout << "Received packet from unknown source" << endl;
				return false;
			}
		}
		else if (bytes == 0)
		{
			cout << "Connection closed by server" << endl;
			return false;
		}
		else
		{
			cout << "recvfrom failed with error: " << WSAGetLastError() << endl;
			return false;
		}

		return true;
	}
	else if (ret == 0) {
		cout << "failed with timeout" << endl;
		return false;
	}
	else {
		cout << "failed with " << WSAGetLastError() << endl;
		return false;
	}

}

bool Socket::ParseDNSResponse(int txid, char* buf, int len) {
	if (len < sizeof(FixedDNSheader)) {
		cout << "++ invalid reply: packet smaller than fixed DNS header" << endl;
		return false;
	}

	int curPos = 0;
	FixedDNSheader* fdh = (FixedDNSheader*)buf;

	int ID = ntohs(fdh->ID);
	int questions = ntohs(fdh->questions);
	int answers = ntohs(fdh->answers);
	int authority = ntohs(fdh->authority);
	int additional = ntohs(fdh->additional);

	if (ID != txid) {
		cout << "  ++ invalid reply: TXID mismatch (Expected: 0x" << uppercase << hex << txid << ", Received: 0x" << ID << ")" << endl;
		return false;
	}

	cout << "  TXID 0x" << hex << ntohs(fdh->ID) << " flags 0x" << hex << ntohs(fdh->flags)
		<< " questions " << dec << questions << " answers " << answers
		<< " authority " << authority << " additional " << additional << endl;

	int rcode = ntohs(fdh->flags) & 0xF;
	if (rcode == 0) {
		cout << "  succeeded with Rcode = 0 " << endl;
	}
	else {
		cout << "  failed with Rcode = " << rcode << endl;
		return false;
	}

	curPos += sizeof(FixedDNSheader);

	cout << "  ------------ [questions] ----------" << endl;
	for (int i = 0; i < questions; i++) {
		string qname = ReadName(buf, curPos, len);
		if (qname.empty()) {
			return false;
		}
		QueryHeader* query = (QueryHeader*)(buf + curPos);
		cout << "        " << qname << " type " << ntohs(query->qType) << " class " << ntohs(query->qClass) << endl;
		curPos += sizeof(QueryHeader);
	}

	if (answers > 0) {
		cout << "  ------------ [answers] ------------" << endl;
		for (int i = 0; i < answers; i++) {
			curPos = PrintResourceRecord(buf, curPos, len);
			if (curPos == -1) {
				return false;
			}
		}
	}

	if (authority > 0) {
		cout << "  ------------ [authority] ----------" << endl;
		for (int i = 0; i < authority; i++) {
			curPos = PrintResourceRecord(buf, curPos, len);
			if (curPos == -1) {
				return false;
			}
		}
	}

	if (additional > 0) {
		cout << "  ------------ [additional] ---------" << endl;
		for (int i = 0; i < additional; i++) {
			curPos = PrintResourceRecord(buf, curPos, len);
			if (curPos == -1) {
				return false;
			}
		}
	}

	return true;
}

string Socket::ReadName(char* buf, int& curPos, int len) {
	string name;
	bool jumped = false;
	int jumpCount = 0;
	int originalPos = curPos;


	while (buf[curPos] != 0) {
		if ((buf[curPos] & 0xC0) == 0xC0) {
			if (curPos + 1 >= len) {
				cout << "  ++ invalid record: truncated jump offset" << endl;
				return "";
			}

			int offset = (((unsigned char)buf[curPos] & 0x3F) << 8) | (unsigned char)buf[curPos + 1];
			if (offset >= len) {
				cout << "  ++ invalid record: jump beyond packet boundary" << endl;
				return "";
			}
			if (offset < sizeof(FixedDNSheader)) {
				cout << "  ++ invalid record: jump into fixed DNS header" << endl;
				return "";
			}
			if (++jumpCount > MAX_DNS_JUMPS) {
				cout << "  ++ invalid record: jump loop" << endl;
				return "";
			}

			if (!jumped) {
				originalPos = curPos + 2;
			}
			curPos = offset;
			jumped = true;
		}
		else {
			int labelLen = buf[curPos];
			if (curPos + labelLen + 1 > len) {
				cout << "  ++ invalid record: truncated name" << endl;
				return "";
			}
			if (!name.empty()) {
				name += ".";  
			}
			name.append(buf + curPos + 1, labelLen);
			curPos += labelLen + 1;
		}
	}

	if (!jumped) {
		curPos++;
	}
	else {
		curPos = originalPos;
	}

	return name;
}

int Socket::PrintResourceRecord(char* buf, int curPos, int len) {
	stringstream print;

	if (curPos >= len) {
		cout << "  ++ invalid section: not enough records" << endl;
		return -1;
	}

	string name = ReadName(buf, curPos, len);
	if (name.empty()) {
		return -1;
	}

	if (curPos + sizeof(DNSanswerHdr) > len) {
		cout << "  ++ invalid record: truncated RR answer header" << endl;
		return -1;
	}

	DNSanswerHdr* ansHdr = (DNSanswerHdr*)(buf + curPos);
	curPos += sizeof(DNSanswerHdr);

	int rrLen = ntohs(ansHdr->len);
	if (curPos + rrLen > len) {
		cout << "  ++ invalid record: RR value length stretches the answer beyond packet" << endl;
		return -1;
	}

	print << "        " << name << " ";

	if (ntohs(ansHdr->type) == 1) {  // A (IPv4 address)
		struct in_addr addr;
		memcpy(&addr, buf + curPos, 4);
		print << "A " << inet_ntoa(addr) << " TTL = " << ntohl(ansHdr->TTL) << endl;
		curPos += 4;
	}
	else if (ntohs(ansHdr->type) == 12) {  // PTR
		string ptrname = ReadName(buf, curPos, len);
		if (ptrname.empty()) {
			return -1;
		}
		print << "PTR " << ptrname << " TTL = " << ntohl(ansHdr->TTL) << endl;
	}
	else if (ntohs(ansHdr->type) == 2) {  // NS
		string nsname = ReadName(buf, curPos, len);
		if (nsname.empty()) {
			return -1;
		}
		print << "NS " << nsname << " TTL = " << ntohl(ansHdr->TTL) << endl;
	}
	else if (ntohs(ansHdr->type) == 5) {  // CNAME
		string cname = ReadName(buf, curPos, len);
		if (cname.empty()) {
			return -1;
		}
		print << "CNAME " << cname << " TTL = " << ntohl(ansHdr->TTL) << endl;
	}

	cout << print.str();

	return curPos;
}