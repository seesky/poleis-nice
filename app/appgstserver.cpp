#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <gst/app/app.h>
#include <gst/gst.h>
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

guint64 be64_to_host(guint64 value)
{
#if G_BYTE_ORDER == G_BIG_ENDIAN
   return value;
#else
   return GUINT64_SWAP_LE_BE(value);
#endif
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

bool send_all_blocking(UDTSOCKET sock, const guint8* data, size_t len)
{
   size_t offset = 0;
   while (offset < len)
   {
      int sent = UDT::send(sock, reinterpret_cast<const char*>(data + offset), static_cast<int>(len - offset), 0);
      if (UDT::ERROR == sent)
      {
         std::cerr << "send: " << UDT::getlasterror().getErrorMessage() << std::endl;
         return false;
      }
      if (0 == sent)
      {
         std::cerr << "send: connection closed" << std::endl;
         return false;
      }
      offset += static_cast<size_t>(sent);
   }
   return true;
}

std::string choose_video_sink()
{
#ifdef G_OS_WIN32
   const char* candidates[] = {"d3d11videosink", "dx9videosink", "d3d11compositor", "glimagesink", "autovideosink"};
#else
   const char* candidates[] = {"glimagesink", "waylandsink", "xvimagesink", "autovideosink"};
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

void set_property_if_exists(GstElement* element, const char* property, bool value)
{
   GParamSpec* pspec = g_object_class_find_property(G_OBJECT_GET_CLASS(element), property);
   if (pspec && (G_PARAM_SPEC_VALUE_TYPE(pspec) == G_TYPE_BOOLEAN))
      g_object_set(element, property, value ? TRUE : FALSE, nullptr);
}

void set_property_if_exists(GstElement* element, const char* property, gint value)
{
   GParamSpec* pspec = g_object_class_find_property(G_OBJECT_GET_CLASS(element), property);
   if (pspec && (G_PARAM_SPEC_VALUE_TYPE(pspec) == G_TYPE_INT))
      g_object_set(element, property, value, nullptr);
}

void set_property_if_exists(GstElement* element, const char* property, gint64 value)
{
   GParamSpec* pspec = g_object_class_find_property(G_OBJECT_GET_CLASS(element), property);
   if (pspec && (G_PARAM_SPEC_VALUE_TYPE(pspec) == G_TYPE_INT64))
      g_object_set(element, property, value, nullptr);
}

struct ServerPipelineContext
{
   GstElement* pipeline = nullptr;
   GstElement* appsrc = nullptr;
   std::atomic<bool> running{true};
};

void bus_watch(ServerPipelineContext& ctx)
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
         std::cerr << "Pipeline received EOS" << std::endl;
         ctx.running.store(false);
         break;
      default:
         break;
      }
      gst_message_unref(msg);
   }
   gst_object_unref(bus);
}

void on_decodebin_pad_added(GstElement* decodebin, GstPad* pad, gpointer user_data)
{
   GstElement* sink_queue = GST_ELEMENT(user_data);
   GstPad* sinkpad = gst_element_get_static_pad(sink_queue, "sink");
   if (gst_pad_is_linked(sinkpad))
   {
      gst_object_unref(sinkpad);
      return;
   }

   if (GST_PAD_LINK_OK != gst_pad_link(pad, sinkpad))
      std::cerr << "Failed to link decodebin to sink" << std::endl;

   gst_object_unref(sinkpad);
}

bool setup_pipeline(ServerPipelineContext& ctx, GstCaps* caps, const std::string& sink_name)
{
   GstElement* pipeline = gst_pipeline_new("gst-server-pipeline");
   GstElement* appsrc = gst_element_factory_make("appsrc", "appsrc");
   GstElement* queue = gst_element_factory_make("queue", "queue");
   GstElement* parse = gst_element_factory_make("h265parse", "parse");
   GstElement* decodebin = gst_element_factory_make("decodebin", "decodebin");
   GstElement* sink_queue = gst_element_factory_make("queue", "sink_queue");
   GstElement* sink = gst_element_factory_make(sink_name.c_str(), "sink");

   if (!pipeline || !appsrc || !queue || !parse || !decodebin || !sink_queue || !sink)
   {
      std::cerr << "Failed to create server pipeline elements" << std::endl;
      if (pipeline)
         gst_object_unref(pipeline);
      if (appsrc)
         gst_object_unref(appsrc);
      if (queue)
         gst_object_unref(queue);
      if (parse)
         gst_object_unref(parse);
      if (decodebin)
         gst_object_unref(decodebin);
      if (sink_queue)
         gst_object_unref(sink_queue);
      if (sink)
         gst_object_unref(sink);
      return false;
   }

   g_object_set(appsrc, "is-live", TRUE, "format", GST_FORMAT_TIME, "block", FALSE, "do-timestamp", FALSE, nullptr);
   gst_app_src_set_stream_type(GST_APP_SRC(appsrc), GST_APP_STREAM_TYPE_STREAM);
   if (caps)
      gst_app_src_set_caps(GST_APP_SRC(appsrc), caps);

   g_object_set(queue, "leaky", 2, "max-size-buffers", 4, "max-size-time", static_cast<guint64>(0), nullptr);
   g_object_set(sink_queue, "leaky", 2, "max-size-buffers", 4, "max-size-time", static_cast<guint64>(0), nullptr);

   set_property_if_exists(parse, "config-interval", 1);

   set_property_if_exists(sink, "sync", false);
   set_property_if_exists(sink, "enable-last-sample", false);
   set_property_if_exists(sink, "async", false);
   set_property_if_exists(sink, "qos", false);
   set_property_if_exists(sink, "max-lateness", static_cast<gint64>(0));

   gst_bin_add_many(GST_BIN(pipeline), appsrc, queue, parse, decodebin, sink_queue, sink, nullptr);

   if (!gst_element_link_many(appsrc, queue, parse, decodebin, nullptr))
   {
      std::cerr << "Failed to link appsrc to decodebin" << std::endl;
      gst_object_unref(pipeline);
      return false;
   }

   if (!gst_element_link(sink_queue, sink))
   {
      std::cerr << "Failed to link sink queue to sink" << std::endl;
      gst_object_unref(pipeline);
      return false;
   }

   g_signal_connect(decodebin, "pad-added", G_CALLBACK(on_decodebin_pad_added), sink_queue);

   ctx.pipeline = pipeline;
   ctx.appsrc = appsrc;
   return true;
}

bool receive_stream(UDTSOCKET sock, ServerPipelineContext& ctx)
{
   std::vector<guint8> payload;
   while (ctx.running.load())
   {
      guint32 payload_len_be = 0;
      guint64 pts_be = 0;
      guint64 duration_be = 0;
      guint32 flags_be = 0;

      if (!recv_all_blocking(sock, reinterpret_cast<guint8*>(&payload_len_be), sizeof(payload_len_be)))
         return false;
      if (!recv_all_blocking(sock, reinterpret_cast<guint8*>(&pts_be), sizeof(pts_be)))
         return false;
      if (!recv_all_blocking(sock, reinterpret_cast<guint8*>(&duration_be), sizeof(duration_be)))
         return false;
      if (!recv_all_blocking(sock, reinterpret_cast<guint8*>(&flags_be), sizeof(flags_be)))
         return false;

      guint32 payload_len = ntohl(payload_len_be);
      guint64 pts = be64_to_host(pts_be);
      guint64 duration = be64_to_host(duration_be);
      guint32 flags = ntohl(flags_be);

      payload.resize(payload_len);
      if (payload_len > 0)
      {
         if (!recv_all_blocking(sock, payload.data(), payload_len))
            return false;
      }

      GstBuffer* buffer = gst_buffer_new_allocate(nullptr, payload_len, nullptr);
      if (!buffer)
      {
         std::cerr << "Failed to allocate GstBuffer" << std::endl;
         return false;
      }

      if (payload_len > 0)
         gst_buffer_fill(buffer, 0, payload.data(), payload_len);

      if (pts == G_MAXUINT64)
         GST_BUFFER_PTS(buffer) = GST_CLOCK_TIME_NONE;
      else
         GST_BUFFER_PTS(buffer) = pts;

      if (duration == G_MAXUINT64)
         GST_BUFFER_DURATION(buffer) = GST_CLOCK_TIME_NONE;
      else
         GST_BUFFER_DURATION(buffer) = duration;

      GST_BUFFER_FLAGS(buffer) = static_cast<GstBufferFlags>(flags);

      GstFlowReturn flow = gst_app_src_push_buffer(GST_APP_SRC(ctx.appsrc), buffer);
      if (GST_FLOW_OK != flow)
      {
         std::cerr << "gst_app_src_push_buffer failed: " << gst_flow_get_name(flow) << std::endl;
         gst_buffer_unref(buffer);
         return false;
      }
   }

   return true;
}

bool negotiate_stream(UDTSOCKET sock, std::string& encoder, std::string& caps)
{
   guint32 encoder_len_be = 0;
   if (!recv_all_blocking(sock, reinterpret_cast<guint8*>(&encoder_len_be), sizeof(encoder_len_be)))
      return false;
   guint32 encoder_len = ntohl(encoder_len_be);

   if (encoder_len > 0)
   {
      std::vector<guint8> buf(encoder_len);
      if (!recv_all_blocking(sock, buf.data(), encoder_len))
         return false;
      encoder.assign(buf.begin(), buf.end());
   }
   else
      encoder.clear();

   guint32 caps_len_be = 0;
   if (!recv_all_blocking(sock, reinterpret_cast<guint8*>(&caps_len_be), sizeof(caps_len_be)))
      return false;
   guint32 caps_len = ntohl(caps_len_be);

   if (caps_len > 0)
   {
      std::vector<guint8> buf(caps_len);
      if (!recv_all_blocking(sock, buf.data(), caps_len))
         return false;
      caps.assign(buf.begin(), buf.end());
   }
   else
      caps.clear();

   return true;
}

void handle_connection(UDTSOCKET sock)
{
   std::string encoder_name;
   std::string caps_string;

   if (!negotiate_stream(sock, encoder_name, caps_string))
   {
      std::cerr << "Negotiation with client failed" << std::endl;
      UDT::close(sock);
      return;
   }

   std::cout << "Negotiated encoder: " << (encoder_name.empty() ? std::string("unknown") : encoder_name) << std::endl;
   if (!caps_string.empty())
      std::cout << "Received caps: " << caps_string << std::endl;

   GstCaps* caps = caps_string.empty() ? nullptr : gst_caps_from_string(caps_string.c_str());
   if (caps_string.size() && !caps)
   {
      std::cerr << "Failed to parse caps: " << caps_string << std::endl;
      guint32 status = htonl(1);
      send_all_blocking(sock, reinterpret_cast<const guint8*>(&status), sizeof(status));
      UDT::close(sock);
      return;
   }

   std::string sink_name = choose_video_sink();
   if (sink_name.empty())
   {
      std::cerr << "No suitable video sink available" << std::endl;
      guint32 status = htonl(2);
      send_all_blocking(sock, reinterpret_cast<const guint8*>(&status), sizeof(status));
      if (caps)
         gst_caps_unref(caps);
      UDT::close(sock);
      return;
   }

   ServerPipelineContext ctx;
   if (!setup_pipeline(ctx, caps, sink_name))
   {
      std::cerr << "Unable to setup pipeline" << std::endl;
      guint32 status = htonl(3);
      send_all_blocking(sock, reinterpret_cast<const guint8*>(&status), sizeof(status));
      if (caps)
         gst_caps_unref(caps);
      UDT::close(sock);
      return;
   }

   GstStateChangeReturn ret = gst_element_set_state(ctx.pipeline, GST_STATE_PLAYING);
   if (GST_STATE_CHANGE_FAILURE == ret)
   {
      std::cerr << "Failed to start pipeline" << std::endl;
      guint32 status = htonl(4);
      send_all_blocking(sock, reinterpret_cast<const guint8*>(&status), sizeof(status));
      gst_object_unref(ctx.pipeline);
      ctx.pipeline = nullptr;
      if (caps)
         gst_caps_unref(caps);
      UDT::close(sock);
      return;
   }

   guint32 status = htonl(0);
   if (!send_all_blocking(sock, reinterpret_cast<const guint8*>(&status), sizeof(status)))
   {
      gst_element_set_state(ctx.pipeline, GST_STATE_NULL);
      gst_object_unref(ctx.pipeline);
      if (caps)
         gst_caps_unref(caps);
      UDT::close(sock);
      return;
   }

   std::thread bus_thread(bus_watch, std::ref(ctx));

   if (!receive_stream(sock, ctx))
      ctx.running.store(false);

   gst_app_src_end_of_stream(GST_APP_SRC(ctx.appsrc));

   ctx.running.store(false);
   bus_thread.join();

   gst_element_set_state(ctx.pipeline, GST_STATE_NULL);
   gst_object_unref(ctx.pipeline);
   if (caps)
      gst_caps_unref(caps);

   UDT::close(sock);
}
}

int main(int argc, char* argv[])
{
   const char* usage =
      "usage: appgstserver [--verbose|--quiet]"
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
      std::string arg(argv[i]);
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
         // Legacy options retained
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

   UDTSOCKET serv = UDT::socket(AF_INET, SOCK_STREAM, 0);

   sockaddr_in any;
   any.sin_family = AF_INET;
   any.sin_port = 0;
   any.sin_addr.s_addr = INADDR_ANY;
   if (UDT::ERROR == UDT::bind(serv, reinterpret_cast<sockaddr*>(&any), sizeof(any)))
   {
      std::cout << "bind: " << UDT::getlasterror().getErrorMessage() << std::endl;
      return 0;
   }

#ifdef USE_LIBNICE
   if (!stun_option.empty())
   {
      std::string host;
      int port = 3478;
      if (!ParseHostPortSpec(stun_option, host, port))
      {
         std::cout << "Invalid STUN server specification: " << stun_option << std::endl;
         return 0;
      }
      if (UDT::ERROR == UDT::setICESTUNServer(serv, host, port))
      {
         std::cout << "setICESTUNServer: " << UDT::getlasterror().getErrorMessage() << std::endl;
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
         std::cout << "Invalid TURN relay specification: " << turn_option << std::endl;
         return 0;
      }
      if (UDT::ERROR == UDT::setICETURNServer(serv, server, port, username, password))
      {
         std::cout << "setICETURNServer: " << UDT::getlasterror().getErrorMessage() << std::endl;
         return 0;
      }
   }

   std::string ufrag, pwd;
   std::vector<std::string> candidates;
   if (UDT::ERROR == UDT::getICEInfo(serv, ufrag, pwd, candidates))
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
   if (UDT::ERROR == UDT::setICEInfo(serv, rem_ufrag, rem_pwd, rem_cand))
   {
      std::cout << "setICEInfo: " << UDT::getlasterror().getErrorMessage() << std::endl;
      return 0;
   }
#endif

   if (UDT::ERROR == UDT::listen(serv, 1))
   {
      std::cout << "listen: " << UDT::getlasterror().getErrorMessage() << std::endl;
      return 0;
   }

   while (true)
   {
      sockaddr_storage clientaddr;
      int addrlen = sizeof(clientaddr);
      UDTSOCKET recver = UDT::accept(serv, reinterpret_cast<sockaddr*>(&clientaddr), &addrlen);
      if (UDT::INVALID_SOCK == recver)
      {
         std::cout << "accept: " << UDT::getlasterror().getErrorMessage() << std::endl;
         continue;
      }

      std::thread(handle_connection, recver).detach();
   }

   UDT::close(serv);
   gst_deinit();
   return 0;
}
