//
// request_handler.cpp
// ~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2020 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "request_handler.hpp"
#include <fstream>
#include <sstream>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <cstdlib>
#include <iostream>

#include <appdynamics.h>

#include "mime_types.hpp"
#include "reply.hpp"
#include "request.hpp"

namespace beast = boost::beast;     // from <boost/beast.hpp>
namespace net = boost::asio;        // from <boost/asio.hpp>
using tcp = net::ip::tcp;           // from <boost/asio/ip/tcp.hpp>


namespace http {
namespace server {


void make_exit_call(const appd::sdk::BT &bt, const std::string &backendName, const std::string &target, const std::string &host, const std::string &port)
{
	appd_exitcall_handle ecHandle = appd_exitcall_begin(bt.handle(), backendName.c_str());
	appd_exitcall_set_details(ecHandle, backendName.c_str());

	try
	{
		int version = 10;

		// The io_context is required for all I/O
		net::io_context ioc;

		// These objects perform our I/O
		tcp::resolver resolver(ioc);
		beast::tcp_stream stream(ioc);

		// Look up the domain name
		auto const results = resolver.resolve(host, port);

		// Make the connection on the IP address we get from a lookup
		stream.connect(results);

		// Set up an HTTP GET request message
		beast::http::request<beast::http::string_body> req{ beast::http::verb::get, target, version };
		req.set(beast::http::field::host, host);
		req.set(beast::http::field::user_agent, BOOST_BEAST_VERSION_STRING);

		// set AppD correlation header
		const char* hdr = appd_exitcall_get_correlation_header(ecHandle);
		req.set("singularityheader", hdr);
		//-----

		// Send the HTTP request to the remote host
		beast::http::write(stream, req);

		// This buffer is used for reading and must be persisted
		beast::flat_buffer buffer;

		// Declare a container to hold the response
		beast::http::response<beast::http::dynamic_body> res;

		// Receive the HTTP response
		beast::http::read(stream, buffer, res);

		// Gracefully close the socket
		beast::error_code ec;
		stream.socket().shutdown(tcp::socket::shutdown_both, ec);

		// not_connected happens sometimes
		// so don't bother reporting it.
		//
		if (ec && ec != beast::errc::not_connected)
		{
			throw beast::system_error{ ec };
		}
		// If we get here then the connection is closed gracefully
	}
	catch (std::exception const& e)
	{
		// add an error to the exit call
		appd_exitcall_add_error(ecHandle, APPD_LEVEL_ERROR, e.what(), true);
		// end the exit call
		appd_exitcall_end(ecHandle);
		throw e;
	}

	// end the exit call
	appd_exitcall_end(ecHandle);
}

request_handler::request_handler(const std::string& doc_root)
  : doc_root_(doc_root)
{
}

void request_handler::handle_request(const request& req, reply& rep)
{
  // Decode url to path.
  std::string request_path;
  if (!url_decode(req.uri, request_path))
  {
    rep = reply::stock_reply(reply::bad_request);
    return;
  }

  // Request path must be absolute and not contain "..".
  if (request_path.empty() || request_path[0] != '/'
      || request_path.find("..") != std::string::npos)
  {
    rep = reply::stock_reply(reply::bad_request);
    return;
  }


  std::string _singularityheader;
  for (http::server::Headers::const_iterator it = req.headers.begin();
	  it != req.headers.end();
	  ++it)
  {
	  if (it->name == "singularityheader") {
		  _singularityheader = it->value;
		  break;
	  }
  }

  // Start Appd BT
  appd::sdk::BT bt(request_path.substr(0, request_path.find_last_of("/")), _singularityheader);
  // Set BT URL
  bt.set_url(request_path);
  // Track BT Method
  bt.add_user_data("HTTP-Request-Method", req.method);

  // If path ends in slash (i.e. is a directory) then add "index.html".
  if (request_path[request_path.size() - 1] == '/')
  {
    request_path += "index.html";
  }

  // Determine the file extension.
  std::size_t last_slash_pos = request_path.find_last_of("/");
  std::size_t last_dot_pos = request_path.find_last_of(".");
  std::string extension;
  if (last_dot_pos != std::string::npos && last_dot_pos > last_slash_pos)
  {
    extension = request_path.substr(last_dot_pos + 1);
  }

  // Open the file to send back.
  std::string full_path = doc_root_ + request_path;
  std::ifstream is(full_path.c_str(), std::ios::in | std::ios::binary);
  if (!is)
  {
    rep = reply::stock_reply(reply::not_found);

	// Mark BT as Error
	bt.add_error(APPD_LEVEL_ERROR, "404 - Not found");
	// Track BT response status code
	bt.add_user_data("HTTP-Response-Status", "404");
    return;
  }

  // Fill out the reply to be sent to the client.
  rep.status = reply::ok;
  char buf[512];
  while (is.read(buf, sizeof(buf)).gcount() > 0)
    rep.content.append(buf, (unsigned int)is.gcount());
  rep.headers.resize(2);
  rep.headers[0].name = "Content-Length";
  rep.headers[0].value = std::to_string(rep.content.size());
  rep.headers[1].name = "Content-Type";
  rep.headers[1].value = mime_types::extension_to_type(extension);

  if (getenv("APPDYNAMICS_UPSTREAM_TIER") != NULL && 
	  strcmp(getenv("APPDYNAMICS_UPSTREAM_TIER"), "true") == 0) {
	  try {
		  make_exit_call(bt, "RabbitMQ", request_path, "localhost", "8081");
	  }
	  catch (std::exception const&) {
		  bt.add_user_data("HTTP-Response-Status", "502");
		  return;
	  }
  }
  else {
	  try {
		  make_exit_call(bt, "http://ext-api.stoloto.ru", "/index.html", "www.example.com", "80");
	  }
	  catch (std::exception const&) {
		  bt.add_user_data("HTTP-Response-Status", "501");
		  return;
	  }
  }

  // Track BT response status code
  bt.add_user_data("HTTP-Response-Status", "200");
}

bool request_handler::url_decode(const std::string& in, std::string& out)
{
  out.clear();
  out.reserve(in.size());
  for (std::size_t i = 0; i < in.size(); ++i)
  {
    if (in[i] == '%')
    {
      if (i + 3 <= in.size())
      {
        int value = 0;
        std::istringstream is(in.substr(i + 1, 2));
        if (is >> std::hex >> value)
        {
          out += static_cast<char>(value);
          i += 2;
        }
        else
        {
          return false;
        }
      }
      else
      {
        return false;
      }
    }
    else if (in[i] == '+')
    {
      out += ' ';
    }
    else
    {
      out += in[i];
    }
  }
  return true;
}

} // namespace server
} // namespace http
