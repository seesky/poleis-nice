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
#include <udt.h>
#include "test_util.h"

using namespace std;

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
   cout << "Local ICE username fragment: " << ufrag << endl;
   cout << "Local ICE password: " << pwd << endl;
   cout << "Local ICE candidates:" << endl;
   for (vector<string>::iterator it = candidates.begin(); it != candidates.end(); ++it)
      cout << *it << endl;

   string rem_ufrag, rem_pwd;
   cout << "Enter remote ICE username fragment: ";
   getline(cin, rem_ufrag);
   cout << "Enter remote ICE password: ";
   getline(cin, rem_pwd);
   cout << "Enter number of remote candidates: ";
   int n = 0;
   cin >> n;
   cin.ignore();
   vector<string> rem_cand;
   for (int i = 0; i < n; ++i)
   {
      string cand;
      getline(cin, cand);
      rem_cand.push_back(cand);
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
