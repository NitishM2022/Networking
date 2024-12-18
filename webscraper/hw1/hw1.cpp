// Nitish Malluru
// CSCE 463
// Fall 2024

#include "pch.h"

#include <iostream>

#include <cstring>   
#include <cstdio>
#include <cstdlib>

#include <string>
#include <queue>
#include <set> 
#include <vector>
#include <time.h>

#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>

using namespace std;

mutex mtx;
mutex setIpMutex;
mutex setHostMutex;
mutex statsMutex;
atomic<bool> crawlDone(false);

queue<char*> urls;
set<string> seenIPs;
set<string> seenHosts;

atomic<int> activeThreads(0);
atomic<int> extractedURLs(0);
atomic<int> uHosts(0);
atomic<int> dns(0);
atomic<int> uIPs(0);
atomic<int> arobots(0); //attempter robots
atomic<int> robots(0);
atomic<int> crawledURLs(0);
atomic<long long> totalLinks(0);
atomic<long long> totalBytes(0);
atomic<long long> pageBytes(0);

atomic<int> h2(0);
atomic<int> h3(0);
atomic<int> h4(0);
atomic<int> h5(0);
atomic<int> diffCode(0);
atomic<int> tamuLink(0);
atomic<int> tamuEx(0);

bool parseURL(char* url, string& scheme, string& host, string& port, string& path, string& query, string& frag) {

    //scheme://host[:port][/path][?query][#fragment] 

    if (!url) {
        return false;
    }

    char* fragC = nullptr;
    char* queryC = nullptr;
    char* pathC = nullptr;
    char* portC = nullptr;
    char* hostC = nullptr;
    char* schemeC = nullptr;

    fragC = strrchr(url, '#');
    if (fragC) {
        *fragC = '\0';
        fragC++;
    }

    queryC = strrchr(url, '?');
    if (queryC) {
        *queryC = '\0';
        queryC++;
    }

    hostC = strstr(url, "://");
    if (hostC) {
        *hostC = '\0';
        hostC += 3;
    }
    else {
        return false;
    }

    pathC = strchr(hostC, '/');
    if (pathC) {
        *pathC = '\0';
        pathC++;
    }

    portC = strchr(hostC, ':');
    if (portC) {
        *portC = '\0';
        portC++;

        if (strlen(portC) == 0) {
            cout << "failed with invalid port" << endl;
            return false;
        }
    }

    schemeC = url;

    host = hostC ? string(hostC) : "";
    port = portC ? string(portC) : "80";
    path = pathC ? "/" + string(pathC) : "/";
    query = queryC ? string(queryC) : "";
    frag = fragC ? string(fragC) : "";
    scheme = schemeC ? string(schemeC) : "";

    if (!(scheme == "http" || scheme == "https")) {
        cout << "failed with invalid scheme" << endl;
        return false;
    }

    if (!port.empty()) {
        for (char c : port) {
            if (!isdigit(c)) {
                cout << "failed with invalid port" << endl;
                return false;
            }
        }
        int portNum = stoi(port);
        if (portNum <= 0 || portNum > 65535) {
            cout << "failed with invalid port" << endl;
            return false;
        }
    }

    return true;
}

int dechunk(char* buf) {
    char* readPtr = strstr(buf, "\r\n\r\n");
    char* writePtr = strstr(buf, "\r\n\r\n") + strlen("\r\n\r\n");
    int size = 0;

    while (true) {
        int chunkSize;
        if (sscanf(readPtr, "%x", &chunkSize) != 1) {
            break;
        }

        char* chunkData = strstr(readPtr, "\r\n");
        if (!chunkData) {
            break;
        }
        chunkData += 2;

        if (chunkSize == 0) {
            break;
        }

        memcpy(writePtr, chunkData, chunkSize);
        writePtr += chunkSize;
        size += chunkSize;

        readPtr = chunkData + chunkSize;
        char* chunkEnd = strstr(readPtr, "\r\n");
        if (!chunkEnd) {
            break;
        }
        readPtr = chunkEnd + 2;
    }

    *writePtr = '\0';
    return size;
}

bool search(char* url) {
    int pageBuffSize = 2 * 1024 * 1024;
    int robotBuffSize = 16 * 1024;

    cout << "URL: " << url << endl;
    cout << "        Parsing URL... ";

    string scheme = "";
    string host = "";
    string port = "";
    string path = "";
    string query = "";
    string frag = "";

    char* urlCpy = NULL;
    urlCpy = _strdup(url);

    if (!parseURL(urlCpy, scheme, host, port, path, query, frag)) {
        return 1;
    }

    free(urlCpy);

    cout << "host " << host
        << ", port " << port
        << ", request " << path;
    if (!query.empty()) {
        cout << "?" << query;
    }
    cout << endl;

    Socket sock;
    // DNS lookup 
    cout << "        Doing DNS... ";
    clock_t t = clock();
    string ip = "";
    if (!sock.FindHost(host, stoi(port), ip)) {
        return 1;
    }
    t = clock() - t;
    cout << "done in " << ((int)(t * 1000) / CLOCKS_PER_SEC) << " ms, found " << ip << endl;

    // Connect to server
    cout << "      * Connecting on page... ";
    t = clock();
    if (!sock.Connect()) {
        return 1;
    }
    t = clock() - t;
    cout << "done in " << ((int)(t * 1000) / CLOCKS_PER_SEC) << " ms" << endl;

    // Send & Read Request
    string request = "/" + path;
    if (!query.empty()) {
        request += "?" + query;
    }

    cout << "        Loading... ";
    t = clock();
    if (!sock.Send(host, stoi(port), "GET", request, "1.1")) {
        return 1;
    }
    if (!sock.Read(pageBuffSize)) {
        return 1;
    }
    t = clock() - t;

    char* statusLine = strstr(sock.buf, "HTTP/");
    int statusCode = 0;
    if (statusLine) {
        cout << "done in " << ((int)(t * 1000) / CLOCKS_PER_SEC) << " ms with " << sock.curPos << " bytes" << endl;
        cout << "        Verifying header... ";
        sscanf(statusLine, "%*s %d", &statusCode);
    }
    else {
        cout << "failed with non-HTTP header (does not begin with HTTP/)" << endl;
        return 1;
    }
    cout << "status code " << statusCode << endl;

    if (statusCode >= 200 && statusCode < 300) {
        char* transferEncoding = strstr(sock.buf, "Transfer-Encoding: chunked");
        int bodySizeBefore = sock.curPos;

        if (transferEncoding) {
            cout << "        Dechunking... ";
            int bodySizeAfter = dechunk(sock.buf);
            cout << "body size was " << bodySizeBefore << ", now " << bodySizeAfter << endl;
        }

        cout << "      + Parsing page... ";
        HTMLParserBase parser;
        int nLinks = 0;

        t = clock();
        char* links = parser.Parse(sock.buf, sock.curPos, url, strlen(url), &nLinks);
        t = clock() - t;
        cout << "done in " << ((int)(t * 1000) / CLOCKS_PER_SEC) << " ms with " << nLinks << " links" << endl;

        cout << "----------------------------------------" << endl;
        char* end = strstr(sock.buf, "\r\n\r\n");
        if (end) {
            for (char* ptr = sock.buf; ptr < end; ptr++) {
                cout << *ptr;
            }
        }
        cout << endl;
    }

    return true;
}

bool getStatusCode(char*& buf, int& statusCode, int& curPos, clock_t t) {
    char* statusLine = strstr(buf, "HTTP/");
    if (statusLine) {
        //cout << "done in " << ((int)(t * 1000) / CLOCKS_PER_SEC) << " ms with " << curPos << " bytes" << endl;
        //cout << "        Verifying header... ";
        sscanf(statusLine, "%*s %d", &statusCode);
    }
    else {
        //cout << "failed with non-HTTP header (does not begin with HTTP/)" << endl;
        return false;
    }
    //cout << "status code " << statusCode << endl;

    return true;
}


void countTAMU(HTMLParserBase& parser, const string& baseURL, int nLinks, char* links) {
    if (nLinks <= 0 || links == nullptr) {
        return;
    }

    char* urlPtr = links;
    for (int i = 0; i < nLinks; i++) {
        string url(urlPtr);

        if (url.find("tamu.edu") != string::npos) {
            if (url.find("tamu.edu/") == 0 || url.find(".tamu.edu/") != string::npos) {
                tamuLink++;
                if (baseURL.find("tamu.edu") == string::npos || url.find(baseURL) == string::npos) {
                    tamuEx++;
                }
                break;
            }
        }
        urlPtr += url.size() + 1;
    }
}

void crawl() {

    SetThreadPriority(GetCurrentThread(), THREAD_BASE_PRIORITY_IDLE);

    int pageBuffSize = 2 * 1024 * 1024;
    int robotBuffSize = 16 * 1024;
    HTMLParserBase parser;
    activeThreads++;

    while (true) {

        char* url = nullptr;
        {
            lock_guard<mutex> lock(mtx);
            if (urls.empty()) {
                break;
            }
            url = urls.front();
            urls.pop();
        }

        extractedURLs++;

        //cout << "URL: " << url << endl;
        //cout << "        Parsing URL... ";

        string scheme = "";
        string host = "";
        string port = "";
        string path = "";
        string query = "";
        string frag = "";

        char* urlCpy = NULL;
        urlCpy = _strdup(url);

        if (!parseURL(urlCpy, scheme, host, port, path, query, frag)) {
            continue;
        }

        free(urlCpy);

        //cout << "host " << host << ", port " << port << endl;

        //cout << "        Checking host uniqueness... ";

        {
            lock_guard<mutex> lock(setHostMutex);
            auto result = seenHosts.insert(host);
            if (result.second) {
                //cout << "passed" << endl;
            }
            else {
                //cout << "failed" << endl;
                continue;
            }
            uHosts++;
        }

        Socket sock;

        // DNS lookup 
        //cout << "        Doing DNS... ";
        clock_t t = clock();
        string ip = "";
        if (!sock.FindHost(host, stoi(port), ip)) {
            continue;
        }
        t = clock() - t;
        dns++;
        //cout << "done in " << ((int)(t * 1000) / CLOCKS_PER_SEC) << " ms, found " << ip << endl;
        //cout << "        Checking IP uniqueness... ";

        {
            lock_guard<mutex> lock(setIpMutex);
            auto result = seenIPs.insert(ip);
            if (result.second) {
                //cout << "passed" << endl;
            }
            else {
                //cout << "failed" << endl;
                continue;
            }
            uIPs++;
        }

        // Connect to server
        //cout << "        Connecting on robots... ";
        t = clock();
        if (!sock.Connect()) {
            continue;
        }
        t = clock() - t;
        //cout << "done in " << ((int)(t * 1000) / CLOCKS_PER_SEC) << " ms" << endl;

        // Send & Read Robot Request
        //cout << "        Loading... ";
        arobots++;
        t = clock();
        if (!sock.Send(host, stoi(port), "HEAD", "/robots.txt")) {
            continue;
        }
        if (!sock.Read(robotBuffSize)) {
            continue;
        }
        t = clock() - t;

        totalBytes += sock.curPos;

        int statusCode = 0;
        if (!getStatusCode(sock.buf, statusCode, sock.curPos, t)) {
            continue;
        }

        if (statusCode >= 500 || statusCode < 400) {
            continue;
        }

        robots++;

        // Send & Read Request Page
        string request = "/" + path;
        if (!query.empty()) {
            request += "?" + query;
        }

        sock.clearBuff();
        //cout << "      * Connecting on page... ";
        t = clock();
        if (!sock.Connect()) {
            continue;
        }
        t = clock() - t;
        //cout << "done in " << ((int)(t * 1000) / CLOCKS_PER_SEC) << " ms" << endl;

        //cout << "        Loading... ";
        t = clock();
        if (!sock.Send(host, stoi(port), "GET", request)) {
            continue;
        }
        if (!sock.Read(pageBuffSize)) {
            continue;
        }
        t = clock() - t;
        totalBytes += sock.curPos;
        pageBytes += sock.curPos;

        statusCode = 0;
        if (!getStatusCode(sock.buf, statusCode, sock.curPos, t)) {
            continue;
        }

        if (statusCode >= 200 && statusCode < 300) {
            h2++;
        }
        else if (statusCode >= 300 && statusCode < 400) {
            h3++;
        }
        else if (statusCode >= 400 && statusCode < 500) {
            h4++;
        }
        else if (statusCode >= 500 && statusCode < 600) {
            h5++;
        }
        else {
            diffCode++;
        }

        if (statusCode >= 200 && statusCode < 300) {
            crawledURLs++;

            //cout << "      + Parsing page... ";
            int nLinks = 0;

            t = clock();
            char* links = parser.Parse(sock.buf, sock.curPos, url, strlen(url), &nLinks);
            t = clock() - t;

            countTAMU(parser, url, nLinks, links);

            //cout << "done in " << ((int)(t * 1000) / CLOCKS_PER_SEC) << " ms with " << nLinks << " links" << endl;
            totalLinks += nLinks;
        }
    }
    activeThreads--;
}

void printStats() {
    long long prevBytes = 0;
    int prevPages = 0;
    auto trueStart = chrono::steady_clock::now();

    this_thread::sleep_for(chrono::seconds(2));

    while (!crawlDone) {
        auto start = chrono::steady_clock::now();

        {
            lock_guard<mutex> lock(statsMutex);

            long long currBytes = totalBytes.load();
            int pages = crawledURLs.load();

            int pps = (pages - prevPages) / 2;
            double mbps = (currBytes - prevBytes) * 8.0 / (1024 * 1024) / 2;

            printf("[%3d] %4d Q %6d E %7d H %6d D %6d I %5d R %5d C %5d L %4lldK\n",
                ((int)(chrono::duration_cast<chrono::seconds>(start - trueStart).count())), activeThreads.load(),
                urls.size(), extractedURLs.load(), uHosts.load(), dns.load(),
                uIPs.load(), robots.load(), crawledURLs.load(), (totalLinks.load() / 1000));

            printf("*** crawling %d pps @ %.1f Mbps\n", pps, mbps);

            prevBytes = currBytes;
            prevPages = pages;
        }

        chrono::duration<double> elapse = chrono::steady_clock::now() - start;
        double sleepTime = 2.0 - elapse.count();

        if (sleepTime > 0) {
            this_thread::sleep_for(chrono::duration<double>(sleepTime));
        }
    }
}




int main(int argc, char* argv[]) {

    if (argc != 2 && argc != 3) {
        cout << "Usage: " << argv[0] << " <URL>" << endl;
        cout << "Usage: " << argv[0] << " <NUM_Threads> <URL_file>" << endl;
        return 1;
    }

    WSADATA wsaData;
    WORD wVersionRequested = MAKEWORD(2, 2);
    if (WSAStartup(wVersionRequested, &wsaData) != 0)
    {
        printf("WSAStartup error %d\n", WSAGetLastError());
        exit(-1);
    }

    if (argc == 2) {
        if (!search(argv[1])) {
            return 1;
        }
    }
    else {
        auto t = clock();
        int numThreads = atoi(argv[1]);

        FILE* file = fopen(argv[2], "rb");
        if (file == nullptr) {
            perror("Error opening file");
            return 1;
        }

        fseek(file, 0, SEEK_END);
        long fileSize = ftell(file);
        rewind(file);

        cout << "Opened " << argv[2] << " with size " << fileSize << endl;

        char* buffer = (char*)malloc(fileSize + 1);
        if (buffer == nullptr) {
            cout << "Memory allocation failed" << endl;
            fclose(file);
            return 1;
        }

        size_t result = fread(buffer, 1, fileSize, file);
        if (result != fileSize) {
            cout << "Reading error" << endl;
            free(buffer);
            fclose(file);
            return 1;
        }

        buffer[fileSize] = '\0';
        fclose(file);

        char* line = strtok(buffer, "\r\n");
        while (line != nullptr) {
            urls.push(line);
            line = strtok(nullptr, "\r\n");
        }

        thread statsThread(printStats);

        vector<thread> threads;
        for (int i = 0; i < numThreads; i++) {
            threads.emplace_back(crawl);
        }

        for (auto& th : threads) {
            th.join();
        }

        crawlDone = true;

        statsThread.join();

        free(buffer);

        cout << endl;
        double passedT = double(clock() - t) / CLOCKS_PER_SEC;

        printf("Extracted %d URLs @ %d/s\n",
            extractedURLs.load(),
            (int)(extractedURLs.load() / passedT)
        );

        printf("Looked up %d DNS names @ %d/s\n",
            uHosts.load(),
            (int)(uHosts.load() / passedT)
        );

        printf("Attempted %d site robots @ %d/s\n",
            arobots.load(),
            (int)(arobots.load() / passedT)
        );

        printf("Crawled %d pages @ %d/s (%.2f MB)\n",
            crawledURLs.load(),
            (int)(crawledURLs.load() / passedT),
            pageBytes.load() / (1024.0 * 1024.0)
        );

        printf("Parsed %lld links @ %lld/s\n",
            totalLinks.load(),
            (long long)(totalLinks.load() / passedT)
        );

        printf("HTTP codes: 2xx = %d, 3xx = %d, 4xx = %d, 5xx = %d, other = %d\n",
            h2.load(),
            h3.load(),
            h4.load(),
            h5.load(),
            diffCode.load()
        );

        //cout << "\n\n\n\n\n\n\n\n\n\n";
        //cout << "TAMU Links: " << tamuLink.load() << " TAMU External Host: " << tamuEx.load() << endl;
    }

    return 0;
}