#ifndef WIN32
#include <unistd.h>
#include <netinet/in.h>
#else
#include <winsock2.h>
#include <windows.h>
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

bool sendAll(UDTSOCKET socket, const char *data, int len)
{
   int sent = 0;
   while (sent < len)
   {
      int result = UDT::send(socket, data + sent, len - sent, 0);
      if (UDT::ERROR == result)
      {
         cout << "send: " << UDT::getlasterror().getErrorMessage() << endl;
         return false;
      }

      if (0 == result)
      {
         cout << "send: connection closed" << endl;
         return false;
      }

      sent += result;
   }

   return true;
}
}

int main(int argc, char *argv[])
{
   const char *usage = "usage: appnicefileclient <file_to_send>";
   if (argc != 2)
   {
      cout << usage << endl;
      return 0;
   }

   const string filepath = argv[1];
   string filename = filepath;
   size_t slash = filepath.find_last_of("/\\");
   if (slash != string::npos && slash + 1 < filepath.size())
      filename = filepath.substr(slash + 1);

   fstream ifs(filepath.c_str(), ios::in | ios::binary);
   if (!ifs)
   {
      cout << "Unable to open file: " << filepath << endl;
      return 1;
   }

   ifs.seekg(0, ios::end);
   int64_t filesize = ifs.tellg();
   ifs.seekg(0, ios::beg);

   if (filesize < 0)
   {
      cout << "Failed to determine file size for: " << filepath << endl;
      return 1;
   }

   UDTUpDown _udt_;

   UDTSOCKET client = UDT::socket(AF_INET, SOCK_STREAM, 0);

   sockaddr_in any;
   any.sin_family = AF_INET;
   any.sin_port = 0;
   any.sin_addr.s_addr = INADDR_ANY;
   if (UDT::ERROR == UDT::bind(client, (sockaddr *)&any, sizeof(any)))
   {
      cout << "bind: " << UDT::getlasterror().getErrorMessage() << endl;
      return 1;
   }

#ifdef USE_LIBNICE
   string ufrag, pwd;
   vector<string> candidates;
   if (UDT::ERROR == UDT::getICEInfo(client, ufrag, pwd, candidates))
   {
      cout << "getICEInfo: " << UDT::getlasterror().getErrorMessage() << endl;
      return 1;
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
      return 1;
   }
   if (UDT::ERROR == UDT::setICEInfo(client, rem_ufrag, rem_pwd, rem_cand))
   {
      cout << "setICEInfo: " << UDT::getlasterror().getErrorMessage() << endl;
      return 1;
   }
#endif

   if (UDT::ERROR == UDT::connect(client, NULL, 0))
   {
      cout << "connect: " << UDT::getlasterror().getErrorMessage() << endl;
      return 1;
   }

   const int32_t name_len = static_cast<int32_t>(filename.size());
   if (!sendAll(client, reinterpret_cast<const char *>(&name_len), sizeof(name_len)))
   {
      UDT::close(client);
      return 1;
   }

   if (!sendAll(client, filename.data(), name_len))
   {
      UDT::close(client);
      return 1;
   }

   if (!sendAll(client, reinterpret_cast<const char *>(&filesize), sizeof(filesize)))
   {
      UDT::close(client);
      return 1;
   }

   int64_t offset = 0;
   if (UDT::ERROR == UDT::sendfile(client, ifs, offset, filesize))
   {
      cout << "sendfile: " << UDT::getlasterror().getErrorMessage() << endl;
      UDT::close(client);
      return 1;
   }

   cout << "File sent successfully." << endl;

   ifs.close();
   UDT::close(client);
   return 0;
}
