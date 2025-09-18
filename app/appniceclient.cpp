#ifndef WIN32
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#else
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <wspiapi.h>
#endif
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <cctype>
#include <algorithm>
#include <iomanip>
#include <udt.h>
#include "test_util.h"

using namespace std;

#ifndef WIN32
void* monitor(void*);
#else
DWORD WINAPI monitor(LPVOID);
#endif

namespace
{
string encodeField(const string &value)
{
   ostringstream oss;
   oss << value.size() << ':' << value;
   return oss.str();
}

bool decodeField(const string &line, size_t &pos, string &value)
{
   while (pos < line.size() && isspace(static_cast<unsigned char>(line[pos])))
      ++pos;
   if (pos >= line.size())
      return false;

   size_t colon = line.find(':', pos);
   if (colon == string::npos || colon == pos)
      return false;

   size_t len = 0;
   try
   {
      len = static_cast<size_t>(stoul(line.substr(pos, colon - pos)));
   }
   catch (...)
   {
      return false;
   }

   pos = colon + 1;
   if (pos + len > line.size())
      return false;

   value.assign(line, pos, len);
   pos += len;
   return true;
}
}

string formatICEInfo(const string &ufrag, const string &pwd, const vector<string> &candidates)
{
   string line = encodeField(ufrag) + encodeField(pwd);
   for (vector<string>::const_iterator it = candidates.begin(); it != candidates.end(); ++it)
      line += encodeField(*it);
   return line;
}

bool parseICEInfo(const string &line, string &ufrag, string &pwd, vector<string> &candidates)
{
   size_t pos = 0;
   if (!decodeField(line, pos, ufrag) || !decodeField(line, pos, pwd))
      return false;

   candidates.clear();
   string cand;
   while (decodeField(line, pos, cand))
      candidates.push_back(cand);

   // Allow trailing whitespace after the last field but not unexpected characters.
   while (pos < line.size())
   {
      if (!isspace(static_cast<unsigned char>(line[pos])))
         return false;
      ++pos;
   }

   return true;
}

int main(int argc, char* argv[])
{
   const char* usage = "usage: appniceclient [--verbose|--quiet]";
   bool verbose = false;

   for (int i = 1; i < argc; ++i)
   {
      string arg(argv[i]);
      if ((arg == "--verbose") || (arg == "-v"))
         verbose = true;
      else if ((arg == "--quiet") || (arg == "-q"))
         verbose = false;
      else if ((arg == "--help") || (arg == "-h"))
      {
         cout << usage << endl;
         return 0;
      }
      else
      {
         cout << usage << endl;
         return 0;
      }
   }

   UDTUpDown _udt_;

   UDTSOCKET client = UDT::socket(AF_INET, SOCK_STREAM, 0);

   sockaddr_in any;
   any.sin_family = AF_INET;
   any.sin_port = 0;
   any.sin_addr.s_addr = INADDR_ANY;
   if (UDT::ERROR == UDT::bind(client, (sockaddr*)&any, sizeof(any)))
   {
      cout << "bind: " << UDT::getlasterror().getErrorMessage() << endl;
      return 0;
   }

#ifdef USE_LIBNICE
   string ufrag, pwd;
   vector<string> candidates;
   if (UDT::ERROR == UDT::getICEInfo(client, ufrag, pwd, candidates))
   {
      cout << "getICEInfo: " << UDT::getlasterror().getErrorMessage() << endl;
      return 0;
   }
   cout << formatICEInfo(ufrag, pwd, candidates) << endl;

   cout << "Paste remote ICE info (length-prefixed fields as printed above):" << endl;
   string line;
   getline(cin, line);
   string rem_ufrag, rem_pwd;
   vector<string> rem_cand;
   if (!parseICEInfo(line, rem_ufrag, rem_pwd, rem_cand))
   {
      cout << "Invalid remote ICE info format" << endl;
      return 0;
   }
   if (UDT::ERROR == UDT::setICEInfo(client, rem_ufrag, rem_pwd, rem_cand))
   {
      cout << "setICEInfo: " << UDT::getlasterror().getErrorMessage() << endl;
      return 0;
   }
#endif

   if (UDT::ERROR == UDT::connect(client, NULL, 0))
   {
      cout << "connect: " << UDT::getlasterror().getErrorMessage() << endl;
      return 0;
   }

   int size = 100000;
   char* data = new char[size];
   memset(data, 0, size);

   size_t chunkCount = 0;
   size_t totalBytesSent = 0;
   size_t summaryChunkCount = 0;
   size_t summaryBytesSent = 0;
   const size_t SUMMARY_INTERVAL = 128;

#ifndef WIN32
   pthread_t monitor_thread;
   pthread_create(&monitor_thread, NULL, monitor, &client);
#else
   HANDLE monitor_thread = CreateThread(NULL, 0, monitor, &client, 0, NULL);
#endif

   for (int i = 0; i < 1000000; ++i)
   {
      int ssize = 0;
      int ss;
      while (ssize < size)
      {
         const char* chunkStart = data + ssize;
         if (UDT::ERROR == (ss = UDT::send(client, chunkStart, size - ssize, 0)))
         {
            cout << "send: " << UDT::getlasterror().getErrorMessage() << endl;
            break;
         }

         ssize += ss;
         totalBytesSent += static_cast<size_t>(ss);
         ++chunkCount;

         if (verbose)
         {
            size_t bytesToPreview = min(static_cast<size_t>(ss), static_cast<size_t>(16));
            ostringstream preview;
            preview << hex << setfill('0');
            for (size_t b = 0; b < bytesToPreview; ++b)
            {
               preview << setw(2) << static_cast<int>(static_cast<unsigned char>(chunkStart[b]));
               if (b + 1 < bytesToPreview)
                  preview << ' ';
            }

            cout << "[UDT::send] chunk " << static_cast<unsigned long long>(chunkCount)
                 << " sent " << ss << " bytes (total "
                 << static_cast<unsigned long long>(totalBytesSent) << " bytes). ";
            if (bytesToPreview > 0)
               cout << "Preview: " << preview.str();
            else
               cout << "Preview: <empty>";
            cout << '\n' << flush;
         }
         else
         {
            ++summaryChunkCount;
            summaryBytesSent += static_cast<size_t>(ss);
            if (summaryChunkCount >= SUMMARY_INTERVAL)
            {
               size_t firstChunk = chunkCount - summaryChunkCount + 1;
               cout << "[UDT::send] chunks "
                    << static_cast<unsigned long long>(firstChunk) << "-"
                    << static_cast<unsigned long long>(chunkCount)
                    << " sent " << summaryBytesSent << " bytes (total "
                    << static_cast<unsigned long long>(totalBytesSent) << " bytes)"
                    << endl;
               summaryChunkCount = 0;
               summaryBytesSent = 0;
            }
         }
      }

      if (ssize < size)
         break;
   }

   if (!verbose && summaryChunkCount > 0)
   {
      size_t firstChunk = chunkCount - summaryChunkCount + 1;
      cout << "[UDT::send] chunks "
           << static_cast<unsigned long long>(firstChunk) << "-"
           << static_cast<unsigned long long>(chunkCount)
           << " sent " << summaryBytesSent << " bytes (total "
           << static_cast<unsigned long long>(totalBytesSent) << " bytes)"
           << endl;
   }

   cout << "Total bytes sent: " << static_cast<unsigned long long>(totalBytesSent) << endl;

   UDT::close(client);

#ifndef WIN32
   pthread_join(monitor_thread, NULL);
#else
   if (NULL != monitor_thread)
   {
      WaitForSingleObject(monitor_thread, INFINITE);
      CloseHandle(monitor_thread);
   }
#endif

   delete [] data;
   return 0;
}

#ifndef WIN32
void* monitor(void* s)
#else
DWORD WINAPI monitor(LPVOID s)
#endif
{
   UDTSOCKET u = *(UDTSOCKET*)s;

   UDT::TRACEINFO perf;

   cout << "SendRate(Mb/s)\tRTT(ms)\tCWnd\tPktSndPeriod(us)\tRecvACK\tRecvNAK" << endl;

   while (true)
   {
#ifndef WIN32
      sleep(1);
#else
      Sleep(1000);
#endif

      if (UDT::ERROR == UDT::perfmon(u, &perf))
      {
         cout << "perfmon: " << UDT::getlasterror().getErrorMessage() << endl;
         break;
      }

      cout << perf.mbpsSendRate << "\t\t"
           << perf.msRTT << "\t"
           << perf.pktCongestionWindow << "\t"
           << perf.usPktSndPeriod << "\t\t\t"
           << perf.pktRecvACK << "\t"
           << perf.pktRecvNAK << endl;
   }

#ifndef WIN32
   return NULL;
#else
   return 0;
#endif
}
