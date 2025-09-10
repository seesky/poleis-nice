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
      cout << "usage: appniceclient" << endl;
      return 0;
   }

   UDTUpDown _udt_;

   UDTSOCKET client = UDT::socket(AF_INET, SOCK_STREAM, 0);

#ifdef USE_LIBNICE
   string ufrag, pwd;
   vector<string> candidates;
   if (UDT::ERROR == UDT::getICEInfo(client, ufrag, pwd, candidates))
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
   if (UDT::ERROR == UDT::setICEInfo(client, rem_ufrag, rem_pwd, rem_cand))
   {
      cout << "setICEInfo: " << UDT::getlasterror().getErrorMessage() << endl;
      return 0;
   }
#endif

   sockaddr_in any;
   any.sin_family = AF_INET;
   any.sin_port = 0;
   any.sin_addr.s_addr = INADDR_ANY;
   if (UDT::ERROR == UDT::bind(client, (sockaddr*)&any, sizeof(any)))
   {
      cout << "bind: " << UDT::getlasterror().getErrorMessage() << endl;
      return 0;
   }

   if (UDT::ERROR == UDT::connect(client, NULL, 0))
   {
      cout << "connect: " << UDT::getlasterror().getErrorMessage() << endl;
      return 0;
   }

   string msg("hello from libnice client\n");
   if (UDT::ERROR == UDT::send(client, msg.c_str(), msg.size(), 0))
      cout << "send: " << UDT::getlasterror().getErrorMessage() << endl;

   UDT::close(client);
   return 0;
}
