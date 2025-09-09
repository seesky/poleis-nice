#ifndef WIN32
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <netdb.h>
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
   if ((3 != argc) || (0 == atoi(argv[2])))
   {
      cout << "usage: appniceclient server_ip server_port" << endl;
      return 0;
   }

   UDTUpDown _udt_;

   struct addrinfo hints, *local, *peer;
   memset(&hints, 0, sizeof(struct addrinfo));
   hints.ai_flags = AI_PASSIVE;
   hints.ai_family = AF_INET;
   hints.ai_socktype = SOCK_STREAM;

   if (0 != getaddrinfo(NULL, "0", &hints, &local))
   {
      cout << "incorrect network address.\n" << endl;
      return 0;
   }

   UDTSOCKET client = UDT::socket(local->ai_family, local->ai_socktype, local->ai_protocol);
   freeaddrinfo(local);

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

   if (0 != getaddrinfo(argv[1], argv[2], &hints, &peer))
   {
      cout << "incorrect server/peer address. " << argv[1] << ":" << argv[2] << endl;
      return 0;
   }

   if (UDT::ERROR == UDT::connect(client, peer->ai_addr, peer->ai_addrlen))
   {
      cout << "connect: " << UDT::getlasterror().getErrorMessage() << endl;
      return 0;
   }
   freeaddrinfo(peer);

   string msg("hello from libnice client\n");
   if (UDT::ERROR == UDT::send(client, msg.c_str(), msg.size(), 0))
      cout << "send: " << UDT::getlasterror().getErrorMessage() << endl;

   UDT::close(client);
   return 0;
}
