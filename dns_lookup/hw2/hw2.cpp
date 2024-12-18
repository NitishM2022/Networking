// Nitish Malluru
// CSCE 463
// Fall 2024

#include "pch.h"

#define MAX_ATTEMPTS 3

using namespace std;

int main(int argc, char* argv[]) {

    if (argc != 3) {
        cout << "Usage: " << argv[0] << " <LOOKUP_STRING> <DNS>" << endl;
        return 1;
    }

	srand(time(nullptr));

    WSADATA wsaData;
    WORD wVersionRequested = MAKEWORD(2, 2);
    if (WSAStartup(wVersionRequested, &wsaData) != 0)
    {
        printf("WSAStartup error %d\n", WSAGetLastError());
        exit(-1);
    }

	string lookup = argv[1];
	string dns = argv[2];

    unsigned long addr = inet_addr(argv[1]);
	bool isIP = addr != INADDR_NONE;

    unsigned short txid = rand() % 65536;

    cout << "Lookup  : " << lookup << endl;
    
    if (isIP) {
        addr = ntohl(addr);

        unsigned char octets[4];
        for (int i = 0; i < 4; ++i) {
            octets[i] = (addr >> (i * 8)) & 0xFF;
        }

        stringstream reverseIPString;
        reverseIPString << (int)octets[0] << "." << (int)octets[1] << "."
            << (int)octets[2] << "." << (int)octets[3] << ".in-addr.arpa";

        lookup = reverseIPString.str();
    }

    cout << "Query   : " << lookup << ", type " << (isIP ? 12 : 1) << ", TXID 0x" << uppercase << hex << txid << dec << endl;
    cout << "Server  : " << dns << endl;
    cout << "********************************" << endl;

    Socket sock;

	for (int i = 0; i < MAX_ATTEMPTS; i++) {
        if (i != 0) {
            cout << endl;
            txid = rand() % 65536;
        }
		cout << "Attempt " << i;

        if (!sock.Bind()) {

            continue;
        }

        if (!sock.Send(dns, (char *)lookup.c_str(), txid, isIP)) {
            continue;
        }

        if (!sock.Read(txid)) {
			continue;
        }
        else {
            break;
        }
	}

    return 0;
}