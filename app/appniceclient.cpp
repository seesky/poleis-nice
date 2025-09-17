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
#include <cctype>
#include <udt.h>
#include "test_util.h"

using namespace std;

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
   if (1 != argc)
   {
      cout << "usage: appniceclient" << endl;
      return 0;
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

   string msg("hello from libnice client\n");
   if (UDT::ERROR == UDT::send(client, msg.c_str(), msg.size(), 0))
      cout << "send: " << UDT::getlasterror().getErrorMessage() << endl;

   UDT::close(client);
   return 0;
}
