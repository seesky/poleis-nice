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
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cctype>
#include <udt.h>
#include "test_util.h"

using namespace std;

#ifndef WIN32
void *handle_client(void *);
#else
DWORD WINAPI handle_client(LPVOID);
#endif

namespace
{
#ifdef USE_LIBNICE
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
#endif

bool recvAll(UDTSOCKET socket, char *data, int len)
{
   int received = 0;
   while (received < len)
   {
      int result = UDT::recv(socket, data + received, len - received, 0);
      if (UDT::ERROR == result)
      {
         cout << "recv: " << UDT::getlasterror().getErrorMessage() << endl;
         return false;
      }

      if (0 == result)
      {
         cout << "recv: connection closed" << endl;
         return false;
      }

      received += result;
   }

   return true;
}
}

int main(int argc, char *argv[])
{
   const char *usage = "usage: appnicefileserver [--verbose|--quiet]";
   for (int i = 1; i < argc; ++i)
   {
      string arg(argv[i]);
      if ((arg == "--verbose") || (arg == "-v") || (arg == "--quiet") || (arg == "-q"))
      {
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
   if (UDT::ERROR == UDT::bind(serv, (sockaddr *)&any, sizeof(any)))
   {
      cout << "bind: " << UDT::getlasterror().getErrorMessage() << endl;
      return 0;
   }

#ifdef USE_LIBNICE
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
      UDTSOCKET recver = UDT::accept(serv, (sockaddr *)&clientaddr, &addrlen);
      if (UDT::INVALID_SOCK == recver)
      {
         cout << "accept: " << UDT::getlasterror().getErrorMessage() << endl;
         continue;
      }

      char clienthost[NI_MAXHOST] = {0};
      char clientservice[NI_MAXSERV] = {0};
      if (0 == getnameinfo((sockaddr *)&clientaddr, addrlen, clienthost, sizeof(clienthost), clientservice, sizeof(clientservice), NI_NUMERICHOST | NI_NUMERICSERV))
         cout << "new connection: " << clienthost << ":" << clientservice << endl;
      else
         cout << "new connection" << endl;

#ifndef WIN32
      UDTSOCKET *worker = new UDTSOCKET(recver);
      pthread_t rcvthread;
      if (0 != pthread_create(&rcvthread, NULL, handle_client, worker))
      {
         cout << "pthread_create failed" << endl;
         delete worker;
         UDT::close(recver);
         continue;
      }
      pthread_detach(rcvthread);
#else
      UDTSOCKET *worker = new UDTSOCKET(recver);
      HANDLE rcvthread = CreateThread(NULL, 0, handle_client, worker, 0, NULL);
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
void *handle_client(void *usocket)
#else
DWORD WINAPI handle_client(LPVOID usocket)
#endif
{
   UDTSOCKET recver = *(UDTSOCKET *)usocket;
   delete (UDTSOCKET *)usocket;

   int32_t name_len = 0;
   if (!recvAll(recver, reinterpret_cast<char *>(&name_len), sizeof(name_len)))
   {
      UDT::close(recver);
#ifndef WIN32
      return NULL;
#else
      return 0;
#endif
   }

   if (name_len < 0 || name_len > 1024 * 1024)
   {
      cout << "Invalid file name length received" << endl;
      UDT::close(recver);
#ifndef WIN32
      return NULL;
#else
      return 0;
#endif
   }

   string remote_name(static_cast<size_t>(name_len), '\0');
   if (!recvAll(recver, &remote_name[0], name_len))
   {
      UDT::close(recver);
#ifndef WIN32
      return NULL;
#else
      return 0;
#endif
   }

   int64_t filesize = 0;
   if (!recvAll(recver, reinterpret_cast<char *>(&filesize), sizeof(filesize)))
   {
      UDT::close(recver);
#ifndef WIN32
      return NULL;
#else
      return 0;
#endif
   }

   if (filesize < 0)
   {
      cout << "Invalid file size received" << endl;
      UDT::close(recver);
#ifndef WIN32
      return NULL;
#else
      return 0;
#endif
   }

   const string output_name = "filetest";
   fstream ofs(output_name.c_str(), ios::out | ios::binary | ios::trunc);
   if (!ofs)
   {
      cout << "Unable to open destination file: " << output_name << endl;
      UDT::close(recver);
#ifndef WIN32
      return NULL;
#else
      return 0;
#endif
   }

   int64_t offset = 0;
   if (UDT::ERROR == UDT::recvfile(recver, ofs, offset, filesize))
   {
      cout << "recvfile: " << UDT::getlasterror().getErrorMessage() << endl;
      ofs.close();
      UDT::close(recver);
#ifndef WIN32
      return NULL;
#else
      return 0;
#endif
   }

   cout << "Received file from client: " << remote_name << " saved as '" << output_name << "'" << endl;

   ofs.close();
   UDT::close(recver);

#ifndef WIN32
   return NULL;
#else
   return 0;
#endif
}
