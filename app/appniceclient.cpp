#ifndef WIN32
#include <unistd.h>
#include <netinet/in.h>
#include <pthread.h>
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
#include <udt.h>
#include "test_util.h"

using namespace std;

namespace
{
struct MonitorContext
{
   UDTSOCKET socket;
   volatile bool* running;
};
}

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
   for (int i = 1; i < argc; ++i)
   {
      string arg(argv[i]);
      if ((arg == "--verbose") || (arg == "-v") || (arg == "--quiet") || (arg == "-q"))
      {
         // Legacy options retained for compatibility but no longer change behavior.
      }
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

   volatile bool running = true;
   MonitorContext monitor_ctx;
   monitor_ctx.socket = client;
   monitor_ctx.running = &running;

#ifndef WIN32
   pthread_t monitor_thread;
#else
   HANDLE monitor_thread = NULL;
#endif
   bool monitor_started = false;

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

   cout << "SendRate(Mb/s)\tRTT(ms)\tCWnd\tPktSndPeriod(us)\tRecvACK\tRecvNAK" << endl;

#ifndef WIN32
   if (0 == pthread_create(&monitor_thread, NULL, monitor, &monitor_ctx))
      monitor_started = true;
   else
      cout << "Unable to start monitor thread" << endl;
#else
   monitor_thread = CreateThread(NULL, 0, monitor, &monitor_ctx, 0, NULL);
   if (NULL != monitor_thread)
      monitor_started = true;
   else
      cout << "Unable to start monitor thread" << endl;
#endif

   const string message = "hello word!";

   while (running)
   {
      int sent = 0;
      while (sent < static_cast<int>(message.size()))
      {
         int result = UDT::send(client, message.data() + sent, static_cast<int>(message.size()) - sent, 0);
         if (UDT::ERROR == result)
         {
            cout << "send: " << UDT::getlasterror().getErrorMessage() << endl;
            running = false;
            break;
         }

         if (0 == result)
         {
            cout << "send: connection closed" << endl;
            running = false;
            break;
         }

         sent += result;
      }

      if (!running)
         break;

      cout << "Sent: " << message << endl;

#ifndef WIN32
      sleep(1);
#else
      Sleep(1000);
#endif
   }

   running = false;

   if (monitor_started)
   {
#ifndef WIN32
      pthread_join(monitor_thread, NULL);
#else
      WaitForSingleObject(monitor_thread, INFINITE);
      CloseHandle(monitor_thread);
#endif
   }

   UDT::close(client);
   return 0;
}

#ifndef WIN32
void* monitor(void* param)
#else
DWORD WINAPI monitor(LPVOID param)
#endif
{
   MonitorContext* ctx = static_cast<MonitorContext*>(param);
   UDTSOCKET u = ctx->socket;

   UDT::TRACEINFO perf;

   while (*(ctx->running))
   {
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

#ifndef WIN32
      sleep(1);
#else
      Sleep(1000);
#endif
   }

#ifndef WIN32
   return NULL;
#else
   return 0;
#endif
}
