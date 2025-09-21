#ifndef WIN32
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#else
#include <winsock2.h>
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

#ifndef WIN32
void* recvdata(void*);
#else
DWORD WINAPI recvdata(LPVOID);
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
   const char* usage =
      "usage: appniceserver [--verbose|--quiet]"
#ifdef USE_LIBNICE
      " [--stun=HOST[:PORT]] [--turn=HOST[:PORT],USERNAME,PASSWORD]"
#endif
      "";
#ifdef USE_LIBNICE
   std::string stun_option;
   std::string turn_option;
#endif
   for (int i = 1; i < argc; ++i)
   {
      string arg(argv[i]);
#ifdef USE_LIBNICE
      if (arg.rfind("--stun=", 0) == 0)
      {
         stun_option = arg.substr(7);
         continue;
      }
      if (arg.rfind("--turn=", 0) == 0)
      {
         turn_option = arg.substr(7);
         continue;
      }
#endif
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

   UDTSOCKET serv = UDT::socket(AF_INET, SOCK_STREAM, 0);

   sockaddr_in any;
   any.sin_family = AF_INET;
   any.sin_port = 0;
   any.sin_addr.s_addr = INADDR_ANY;
   if (UDT::ERROR == UDT::bind(serv, (sockaddr*)&any, sizeof(any)))
   {
      cout << "bind: " << UDT::getlasterror().getErrorMessage() << endl;
      return 0;
   }

#ifdef USE_LIBNICE
   if (!stun_option.empty())
   {
      std::string host;
      int port = 3478;
      if (!ParseHostPortSpec(stun_option, host, port))
      {
         cout << "Invalid STUN server specification: " << stun_option << endl;
         return 0;
      }
      if (UDT::ERROR == UDT::setICESTUNServer(serv, host, port))
      {
         cout << "setICESTUNServer: " << UDT::getlasterror().getErrorMessage() << endl;
         return 0;
      }
   }

   if (!turn_option.empty())
   {
      std::string server;
      int port = 3478;
      std::string username;
      std::string password;
      if (!ParseTurnSpec(turn_option, server, port, username, password))
      {
         cout << "Invalid TURN relay specification: " << turn_option << endl;
         return 0;
      }
      if (UDT::ERROR == UDT::setICETURNServer(serv, server, port, username, password))
      {
         cout << "setICETURNServer: " << UDT::getlasterror().getErrorMessage() << endl;
         return 0;
      }
   }

   string ufrag, pwd;
   vector<string> candidates;
   if (UDT::ERROR == UDT::getICEInfo(serv, ufrag, pwd, candidates))
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
   if (UDT::ERROR == UDT::setICEInfo(serv, rem_ufrag, rem_pwd, rem_cand))
   {
      cout << "setICEInfo: " << UDT::getlasterror().getErrorMessage() << endl;
      return 0;
   }
#endif
   if (UDT::ERROR == UDT::listen(serv, 1))
   {
      cout << "listen: " << UDT::getlasterror().getErrorMessage() << endl;
      return 0;
   }

   while (true)
   {
      sockaddr_storage clientaddr;
      int addrlen = sizeof(clientaddr);
      UDTSOCKET recver = UDT::accept(serv, (sockaddr*)&clientaddr, &addrlen);
      if (UDT::INVALID_SOCK == recver)
      {
         cout << "accept: " << UDT::getlasterror().getErrorMessage() << endl;
         continue;
      }

      char clienthost[NI_MAXHOST] = {0};
      char clientservice[NI_MAXSERV] = {0};
      if (0 == getnameinfo((sockaddr*)&clientaddr, addrlen, clienthost, sizeof(clienthost), clientservice, sizeof(clientservice), NI_NUMERICHOST | NI_NUMERICSERV))
         cout << "new connection: " << clienthost << ":" << clientservice << endl;
      else
         cout << "new connection" << endl;

#ifndef WIN32
      UDTSOCKET* worker = new UDTSOCKET(recver);
      pthread_t rcvthread;
      if (0 != pthread_create(&rcvthread, NULL, recvdata, worker))
      {
         cout << "pthread_create failed" << endl;
         delete worker;
         UDT::close(recver);
         continue;
      }
      pthread_detach(rcvthread);
#else
      UDTSOCKET* worker = new UDTSOCKET(recver);
      HANDLE rcvthread = CreateThread(NULL, 0, recvdata, worker, 0, NULL);
      if (NULL == rcvthread)
      {
         cout << "CreateThread failed" << endl;
         delete worker;
         UDT::close(recver);
         continue;
      }
      CloseHandle(rcvthread);
#endif
   }

   UDT::close(serv);
   return 0;
}

#ifndef WIN32
void* recvdata(void* usocket)
#else
DWORD WINAPI recvdata(LPVOID usocket)
#endif
{
   UDTSOCKET recver = *(UDTSOCKET*)usocket;
   delete (UDTSOCKET*)usocket;

   const int BUFFER_SIZE = 64;
   char buffer[BUFFER_SIZE];

   double count = 0;

   while (true)
   {
      int received = UDT::recv(recver, buffer, BUFFER_SIZE, 0);
      if (UDT::ERROR == received)
      {
         cout << "recv: " << UDT::getlasterror().getErrorMessage() << endl;
         break;
      }

      if (0 == received)
      {
         cout << "connection closed by peer" << endl;
         break;
      }

      string message(buffer, buffer + received);
      cout << "Received: " << message << endl;
      count = count + 1;
      // cout << "Received Message Count: " << count << endl;
   }

   UDT::close(recver);

#ifndef WIN32
   return NULL;
#else
   return 0;
#endif
}
