#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <gst/app/app.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <glib.h>

#ifndef WIN32
#include <netinet/in.h>
#include <unistd.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wspiapi.h>
#endif

#include <udt.h>

#include "test_util.h"

namespace
{
std::string encodeField(const std::string& value)
{
   std::ostringstream oss;
   oss << value.size() << ':' << value;
   return oss.str();
}

bool decodeField(const std::string& line, size_t& pos, std::string& value)
{
   while (pos < line.size() && isspace(static_cast<unsigned char>(line[pos])))
      ++pos;
   if (pos >= line.size())
      return false;

   size_t colon = line.find(':', pos);
   if (colon == std::string::npos || colon == pos)
      return false;

   size_t len = 0;
   try
   {
      len = static_cast<size_t>(std::stoul(line.substr(pos, colon - pos)));
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

std::string formatICEInfo(const std::string& ufrag, const std::string& pwd, const std::vector<std::string>& candidates)
{
   std::string line = encodeField(ufrag) + encodeField(pwd);
   for (std::vector<std::string>::const_iterator it = candidates.begin(); it != candidates.end(); ++it)
      line += encodeField(*it);
   return line;
}

bool parseICEInfo(const std::string& line, std::string& ufrag, std::string& pwd, std::vector<std::string>& candidates)
{
   size_t pos = 0;
   if (!decodeField(line, pos, ufrag) || !decodeField(line, pos, pwd))
      return false;

   candidates.clear();
   std::string cand;
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

guint64 host_to_be64(guint64 value)
{
#if G_BYTE_ORDER == G_BIG_ENDIAN
   return value;
#else
   return GUINT64_SWAP_LE_BE(value);
#endif
}

bool send_all_nonblocking(UDTSOCKET sock, const guint8* data, size_t len, std::atomic<bool>& running)
{
   size_t offset = 0;
   while (offset < len && running.load())
   {
      int sent = UDT::send(sock, reinterpret_cast<const char*>(data + offset), static_cast<int>(len - offset), 0);
      if (UDT::ERROR == sent)
      {
         CUDTException ex = UDT::getlasterror();
         if (CUDTException::EASYNCSND == ex.getErrorCode())
         {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
         }
         std::cerr << "send: " << ex.getErrorMessage() << std::endl;
         return false;
      }
      if (0 == sent)
      {
         std::cerr << "send: connection closed" << std::endl;
         return false;
      }
      offset += static_cast<size_t>(sent);
   }
   return offset == len;
}

bool recv_all_blocking(UDTSOCKET sock, guint8* data, size_t len)
{
   size_t offset = 0;
   while (offset < len)
   {
      int received = UDT::recv(sock, reinterpret_cast<char*>(data + offset), static_cast<int>(len - offset), 0);
      if (UDT::ERROR == received)
      {
         std::cerr << "recv: " << UDT::getlasterror().getErrorMessage() << std::endl;
         return false;
      }
      if (0 == received)
      {
         std::cerr << "recv: connection closed" << std::endl;
         return false;
      }
      offset += static_cast<size_t>(received);
   }
   return true;
}

std::string choose_best_source()
{
#ifdef G_OS_WIN32
   const char* candidates[] = {"d3d11screencapture", "dx9screencapture", "dxgiscreencapture", "gdigrab", "wfdscreencapsrc"};
#else
   const char* candidates[] = {"ximagesrc", "pipewiresrc", "waylandscreencapturesrc", "autovideosrc"};
#endif
   for (const char* name : candidates)
   {
      if (!name)
         continue;
      GstElementFactory* factory = gst_element_factory_find(name);
      if (factory)
      {
         gst_object_unref(factory);
         return name;
      }
   }
   return std::string();
}

std::string choose_h265_encoder()
{
#ifdef G_OS_WIN32
   const char* candidates[] = {"d3d11h265enc", "nvh265enc", "msdkh265enc", "openh265enc", "x265enc"};
#else
   const char* candidates[] = {"nvh265enc", "vaapih265enc", "v4l2h265enc", "x265enc", "openh265enc"};
#endif
   for (const char* name : candidates)
   {
      if (!name)
         continue;
      GstElementFactory* factory = gst_element_factory_find(name);
      if (factory)
      {
         gst_object_unref(factory);
         return name;
      }
   }
   return std::string();
}

void set_property_if_exists(GstElement* element, const char* property, gint value)
{
   GParamSpec* pspec = g_object_class_find_property(G_OBJECT_GET_CLASS(element), property);
   if (pspec && G_PARAM_SPEC_VALUE_TYPE(pspec) == G_TYPE_INT)
      g_object_set(element, property, value, nullptr);
}

void set_property_if_exists(GstElement* element, const char* property, bool value)
{
   GParamSpec* pspec = g_object_class_find_property(G_OBJECT_GET_CLASS(element), property);
   if (pspec && (G_PARAM_SPEC_VALUE_TYPE(pspec) == G_TYPE_BOOLEAN))
      g_object_set(element, property, value ? TRUE : FALSE, nullptr);
}

void set_property_if_exists(GstElement* element, const char* property, const char* value)
{
   GParamSpec* pspec = g_object_class_find_property(G_OBJECT_GET_CLASS(element), property);
   if (pspec && (G_PARAM_SPEC_VALUE_TYPE(pspec) == G_TYPE_STRING))
      g_object_set(element, property, value, nullptr);
}

struct PipelineContext
{
   GstElement* pipeline = nullptr;
   GstElement* sink = nullptr;
   std::atomic<bool> running{true};
   std::atomic<bool> negotiated{false};
   std::string encoder_name;
   UDTSOCKET socket = UDT::INVALID_SOCK;
};

bool send_negotiation(PipelineContext& ctx, GstCaps* caps)
{
   if (ctx.negotiated.load())
      return true;

   if (!caps)
      return false;

   gchar* caps_str = gst_caps_to_string(caps);
   if (!caps_str)
      return false;

   std::string encoder_field = ctx.encoder_name;
   std::string caps_field = caps_str;
   g_free(caps_str);

   if (encoder_field.size() > static_cast<size_t>(std::numeric_limits<guint32>::max()) ||
       caps_field.size() > static_cast<size_t>(std::numeric_limits<guint32>::max()))
   {
      std::cerr << "Negotiation fields too large" << std::endl;
      return false;
   }

   guint32 encoder_len = htonl(static_cast<guint32>(encoder_field.size()));
   guint32 caps_len = htonl(static_cast<guint32>(caps_field.size()));

   std::vector<guint8> payload;
   const guint8* encoder_len_ptr = reinterpret_cast<const guint8*>(&encoder_len);
   payload.insert(payload.end(), encoder_len_ptr, encoder_len_ptr + sizeof(encoder_len));
   payload.insert(payload.end(), encoder_field.begin(), encoder_field.end());
   const guint8* caps_len_ptr = reinterpret_cast<const guint8*>(&caps_len);
   payload.insert(payload.end(), caps_len_ptr, caps_len_ptr + sizeof(caps_len));
   payload.insert(payload.end(), caps_field.begin(), caps_field.end());

   if (!send_all_nonblocking(ctx.socket, payload.data(), payload.size(), ctx.running))
      return false;

   guint32 response = 0;
   if (!recv_all_blocking(ctx.socket, reinterpret_cast<guint8*>(&response), sizeof(response)))
      return false;

   response = ntohl(response);
   if (0 != response)
   {
      std::cerr << "Remote rejected negotiation (code " << response << ")" << std::endl;
      return false;
   }

   ctx.negotiated.store(true);
   return true;
}

bool transmit_sample(PipelineContext& ctx, GstSample* sample)
{
   GstBuffer* buffer = gst_sample_get_buffer(sample);
   GstCaps* caps = gst_sample_get_caps(sample);

   if (!send_negotiation(ctx, caps))
   {
      ctx.running.store(false);
      return false;
   }

   if (!buffer)
      return true;

   GstMapInfo info;
   if (!gst_buffer_map(buffer, &info, GST_MAP_READ))
      return true;

   guint32 payload_len = static_cast<guint32>(info.size);
   guint32 payload_len_be = htonl(payload_len);

   guint64 pts = GST_BUFFER_PTS(buffer);
   guint64 duration = GST_BUFFER_DURATION(buffer);
   guint32 flags = static_cast<guint32>(GST_BUFFER_FLAGS(buffer));

   if (!GST_CLOCK_TIME_IS_VALID(pts))
      pts = G_MAXUINT64;
   if (!GST_CLOCK_TIME_IS_VALID(duration))
      duration = G_MAXUINT64;

   guint64 pts_be = host_to_be64(pts);
   guint64 duration_be = host_to_be64(duration);
   guint32 flags_be = htonl(flags);

   std::vector<guint8> header;
   const guint8* payload_len_ptr = reinterpret_cast<const guint8*>(&payload_len_be);
   const guint8* pts_ptr = reinterpret_cast<const guint8*>(&pts_be);
   const guint8* duration_ptr = reinterpret_cast<const guint8*>(&duration_be);
   const guint8* flags_ptr = reinterpret_cast<const guint8*>(&flags_be);

   header.insert(header.end(), payload_len_ptr, payload_len_ptr + sizeof(payload_len_be));
   header.insert(header.end(), pts_ptr, pts_ptr + sizeof(pts_be));
   header.insert(header.end(), duration_ptr, duration_ptr + sizeof(duration_be));
   header.insert(header.end(), flags_ptr, flags_ptr + sizeof(flags_be));

   bool ok = send_all_nonblocking(ctx.socket, header.data(), header.size(), ctx.running);
   if (ok && info.size > 0)
      ok = send_all_nonblocking(ctx.socket, info.data, info.size, ctx.running);

   gst_buffer_unmap(buffer, &info);

   if (!ok)
      ctx.running.store(false);
   return ok;
}

void bus_watch(PipelineContext& ctx)
{
   GstBus* bus = gst_element_get_bus(ctx.pipeline);
   while (ctx.running.load())
   {
      GstMessage* msg = gst_bus_timed_pop(bus, 100 * GST_MSECOND);
      if (!msg)
         continue;

      switch (GST_MESSAGE_TYPE(msg))
      {
      case GST_MESSAGE_ERROR:
      {
         GError* err = nullptr;
         gchar* dbg = nullptr;
         gst_message_parse_error(msg, &err, &dbg);
         std::cerr << "GStreamer error: " << (err ? err->message : "unknown") << std::endl;
         if (dbg)
         {
            std::cerr << dbg << std::endl;
            g_free(dbg);
         }
         if (err)
            g_error_free(err);
         ctx.running.store(false);
         break;
      }
      case GST_MESSAGE_EOS:
         std::cerr << "Pipeline signalled EOS" << std::endl;
         ctx.running.store(false);
         break;
      default:
         break;
      }
      gst_message_unref(msg);
   }
   gst_object_unref(bus);
}

bool run_pipeline(PipelineContext& ctx)
{
   std::string source_name = choose_best_source();
   if (source_name.empty())
   {
      std::cerr << "No suitable screen capture source found" << std::endl;
      return false;
   }

   ctx.encoder_name = choose_h265_encoder();
   if (ctx.encoder_name.empty())
   {
      std::cerr << "No H.265 encoder available" << std::endl;
      return false;
   }

   GstElement* pipeline = gst_pipeline_new("gst-client-pipeline");
   GstElement* source = gst_element_factory_make(source_name.c_str(), "source");
   GstElement* convert = gst_element_factory_make("videoconvert", "convert");
   GstElement* queue = gst_element_factory_make("queue", "queue");
   GstElement* encoder = gst_element_factory_make(ctx.encoder_name.c_str(), "encoder");
   GstElement* parse = gst_element_factory_make("h265parse", "parse");
   GstElement* sink = gst_element_factory_make("appsink", "sink");

   if (!pipeline || !source || !convert || !queue || !encoder || !parse || !sink)
   {
      std::cerr << "Failed to create pipeline elements" << std::endl;
      if (pipeline)
         gst_object_unref(pipeline);
      if (source)
         gst_object_unref(source);
      if (convert)
         gst_object_unref(convert);
      if (queue)
         gst_object_unref(queue);
      if (encoder)
         gst_object_unref(encoder);
      if (parse)
         gst_object_unref(parse);
      if (sink)
         gst_object_unref(sink);
      return false;
   }

   g_object_set(queue, "leaky", 2, "max-size-buffers", 2, "max-size-time", static_cast<guint64>(0), nullptr);

   set_property_if_exists(source, "cursor", true);
   set_property_if_exists(source, "show-cursor", true);
   set_property_if_exists(source, "is-live", true);

   set_property_if_exists(encoder, "tune", "zerolatency");
   set_property_if_exists(encoder, "speed-preset", "ultrafast");
   set_property_if_exists(encoder, "low-latency", true);
   set_property_if_exists(encoder, "bframes", 0);

   g_object_set(parse, "config-interval", 1, nullptr);

   gst_app_sink_set_emit_signals(GST_APP_SINK(sink), FALSE);
   gst_app_sink_set_drop(GST_APP_SINK(sink), TRUE);
   gst_app_sink_set_max_buffers(GST_APP_SINK(sink), 4);

   gst_bin_add_many(GST_BIN(pipeline), source, convert, queue, encoder, parse, sink, nullptr);

   if (!gst_element_link_many(source, convert, queue, encoder, parse, nullptr))
   {
      std::cerr << "Failed to link source to encoder" << std::endl;
      gst_object_unref(pipeline);
      return false;
   }

   if (!gst_element_link(parse, sink))
   {
      std::cerr << "Failed to link parser to sink" << std::endl;
      gst_object_unref(pipeline);
      return false;
   }

   ctx.pipeline = pipeline;
   ctx.sink = sink;

   GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
   if (GST_STATE_CHANGE_FAILURE == ret)
   {
      std::cerr << "Unable to set pipeline to PLAYING" << std::endl;
      gst_element_set_state(pipeline, GST_STATE_NULL);
      gst_object_unref(pipeline);
      return false;
   }

   std::thread bus_thread(bus_watch, std::ref(ctx));

   while (ctx.running.load())
   {
      GstSample* sample = gst_app_sink_try_pull_sample(GST_APP_SINK(ctx.sink), GST_SECOND / 4);
      if (!sample)
         continue;
      if (!transmit_sample(ctx, sample))
      {
         gst_sample_unref(sample);
         break;
      }
      gst_sample_unref(sample);
   }

   gst_element_send_event(pipeline, gst_event_new_eos());
   gst_element_set_state(pipeline, GST_STATE_NULL);
   ctx.running.store(false);
   bus_thread.join();
   gst_object_unref(pipeline);
   return true;
}
}

int main(int argc, char* argv[])
{
   const char* usage = "usage: appgstclient [--verbose|--quiet]";
   for (int i = 1; i < argc; ++i)
   {
      std::string arg(argv[i]);
      if ((arg == "--verbose") || (arg == "-v") || (arg == "--quiet") || (arg == "-q"))
      {
         // Retained for compatibility
      }
      else if ((arg == "--help") || (arg == "-h"))
      {
         std::cout << usage << std::endl;
         return 0;
      }
      else
      {
         std::cout << usage << std::endl;
         return 0;
      }
   }

   gst_init(&argc, &argv);

   UDTUpDown _udt_;

   UDTSOCKET client = UDT::socket(AF_INET, SOCK_STREAM, 0);

   sockaddr_in any;
   any.sin_family = AF_INET;
   any.sin_port = 0;
   any.sin_addr.s_addr = INADDR_ANY;
   if (UDT::ERROR == UDT::bind(client, reinterpret_cast<sockaddr*>(&any), sizeof(any)))
   {
      std::cout << "bind: " << UDT::getlasterror().getErrorMessage() << std::endl;
      return 0;
   }

#ifdef USE_LIBNICE
   std::string ufrag, pwd;
   std::vector<std::string> candidates;
   if (UDT::ERROR == UDT::getICEInfo(client, ufrag, pwd, candidates))
   {
      std::cout << "getICEInfo: " << UDT::getlasterror().getErrorMessage() << std::endl;
      return 0;
   }
   std::cout << formatICEInfo(ufrag, pwd, candidates) << std::endl;

   std::cout << "Paste remote ICE info (length-prefixed fields as printed above):" << std::endl;
   std::string line;
   std::getline(std::cin, line);
   std::string rem_ufrag, rem_pwd;
   std::vector<std::string> rem_cand;
   if (!parseICEInfo(line, rem_ufrag, rem_pwd, rem_cand))
   {
      std::cout << "Invalid remote ICE info format" << std::endl;
      return 0;
   }
   if (UDT::ERROR == UDT::setICEInfo(client, rem_ufrag, rem_pwd, rem_cand))
   {
      std::cout << "setICEInfo: " << UDT::getlasterror().getErrorMessage() << std::endl;
      return 0;
   }
#endif

   if (UDT::ERROR == UDT::connect(client, nullptr, 0))
   {
      std::cout << "connect: " << UDT::getlasterror().getErrorMessage() << std::endl;
      return 0;
   }

   bool blocking = false;
   UDT::setsockopt(client, 0, UDT_SNDSYN, &blocking, sizeof(blocking));

   PipelineContext ctx;
   ctx.socket = client;

   std::atomic<bool> monitor_running{true};
   std::thread monitor_thread([client, &monitor_running]() {
      UDT::TRACEINFO perf;
      while (monitor_running.load())
      {
         if (UDT::ERROR == UDT::perfmon(client, &perf))
         {
            std::cout << "perfmon: " << UDT::getlasterror().getErrorMessage() << std::endl;
            break;
         }

         std::cout << perf.mbpsSendRate << "\t\t" << perf.msRTT << "\t" << perf.pktCongestionWindow << "\t"
                   << perf.usPktSndPeriod << "\t\t\t" << perf.pktRecvACK << "\t" << perf.pktRecvNAK << std::endl;
         std::this_thread::sleep_for(std::chrono::seconds(1));
      }
   });

   run_pipeline(ctx);

   monitor_running.store(false);
   if (monitor_thread.joinable())
      monitor_thread.join();

   UDT::close(client);
   gst_deinit();
   return 0;
}
