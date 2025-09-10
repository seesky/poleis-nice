#ifndef WIN32
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wspiapi.h>
#endif
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <udt.h>
#include "test_util.h"

using namespace std;

string formatICEInfo(const string &ufrag, const string &pwd, const vector<string> &candidates)
{
   string line = ufrag + " " + pwd;
   for (vector<string>::const_iterator it = candidates.begin(); it != candidates.end(); ++it)
      line += " " + *it;
   return line;
}

bool parseICEInfo(const string &line, string &ufrag, string &pwd, vector<string> &candidates)
{
   istringstream iss(line);
   if (!(iss >> ufrag >> pwd))
      return false;
   candidates.clear();
   string cand;
   while (iss >> cand)
      candidates.push_back(cand);
   return true;
}

int main(int argc, char* argv[])
{
   if (1 != argc)
   {
      cout << "usage: appniceserver" << endl;
      return 0;
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
   string ufrag, pwd;
   vector<string> candidates;
   if (UDT::ERROR == UDT::getICEInfo(serv, ufrag, pwd, candidates))
   {
      cout << "getICEInfo: " << UDT::getlasterror().getErrorMessage() << endl;
      return 0;
   }
   cout << formatICEInfo(ufrag, pwd, candidates) << endl;

   cout << "Paste remote ICE info (ufrag pwd cand1 cand2 ...):" << endl;
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

   sockaddr_storage clientaddr;
   int addrlen = sizeof(clientaddr);
   UDTSOCKET recver = UDT::accept(serv, (sockaddr*)&clientaddr, &addrlen);
   if (UDT::INVALID_SOCK == recver)
   {
      cout << "accept: " << UDT::getlasterror().getErrorMessage() << endl;
      return 0;
   }

   cout << "connection established" << endl;

   char data[100];
   int r;
   while ((r = UDT::recv(recver, data, sizeof(data), 0)) > 0)
   {
      cout.write(data, r);
      cout.flush();
   }

   UDT::close(recver);
   UDT::close(serv);
   return 0;
}
