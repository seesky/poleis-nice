#ifndef _UDT_TEST_UTIL_H_
#define _UDT_TEST_UTIL_H_

struct UDTUpDown{
   UDTUpDown()
   {
      // use this function to initialize the UDT library
      UDT::startup();
   }
   ~UDTUpDown()
   {
      // use this function to release the UDT library
      UDT::cleanup();
   }
};

#ifdef USE_LIBNICE
#include <string>
#include <cstdlib>

inline bool ParseHostPortSpec(const std::string& spec, std::string& host, int& port, int default_port = 3478)
{
   if (spec.empty())
      return false;

   std::string host_part;
   std::string port_part;
   if (spec[0] == '[')
   {
      std::string::size_type end = spec.find(']');
      if (end == std::string::npos)
         return false;
      host_part = spec.substr(1, end - 1);
      if (end + 1 < spec.size())
      {
         if (spec[end + 1] != ':')
            return false;
         port_part = spec.substr(end + 2);
      }
   }
   else
   {
      std::string::size_type colon = spec.rfind(':');
      if (colon != std::string::npos && spec.find(':') == colon)
      {
         host_part = spec.substr(0, colon);
         port_part = spec.substr(colon + 1);
      }
      else
      {
         host_part = spec;
      }
   }

   if (host_part.empty())
      return false;

   port = default_port;
   if (!port_part.empty())
   {
      char* end = NULL;
      long value = strtol(port_part.c_str(), &end, 10);
      if ((NULL == end) || (*end != '\0') || (value <= 0) || (value > 65535))
         return false;
      port = static_cast<int>(value);
   }

   host.swap(host_part);
   return true;
}

inline bool ParseTurnSpec(const std::string& spec, std::string& server, int& port,
                          std::string& username, std::string& password, int default_port = 3478)
{
   std::string::size_type first = spec.find(',');
   if (first == std::string::npos)
      return false;
   std::string host_port = spec.substr(0, first);
   std::string rest = spec.substr(first + 1);
   std::string::size_type second = rest.find(',');
   if (second == std::string::npos)
      return false;
   username = rest.substr(0, second);
   password = rest.substr(second + 1);
   if (!ParseHostPortSpec(host_port, server, port, default_port))
      return false;
   return true;
}
#endif

#endif
