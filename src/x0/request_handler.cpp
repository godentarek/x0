/* <x0/request_handler.cpp>
 *
 * This file is part of the x0 web server, released under GPLv3.
 * (c) 2009 Chrisitan Parpart <trapni@gentoo.org>
 */

#include <x0/request_handler.hpp>

#include <x0/mime_types.hpp>
#include <x0/request.hpp>
#include <x0/response.hpp>
#include <boost/lexical_cast.hpp>
#include <fstream>
#include <sstream>
#include <string>

namespace x0 {
#if 1 == 0
request_handler::request_handler(const std::string& docroot)
  : docroot_(docroot)
{
}

void request_handler::handle_request(const request& req, response& rep)
{
	// decode url to path
	std::string request_path;
	if (!url_decode(req.uri, request_path))
	{
		rep = response::stock_response(response::bad_request);
		return;
	}

	// request path must be absolute and not contain ".."
	if (request_path.empty() || request_path[0] != '/'
		|| request_path.find("..") != std::string::npos)
	{
		rep = response::stock_response(response::bad_request);
		return;
	}

	// mimic DirectoryIndex
	if (request_path[request_path.size() - 1] == '/')
	{
		request_path += "index.html";
	}

	// determine the file extension.
	std::size_t last_slash_pos = request_path.find_last_of("/");
	std::size_t last_dot_pos = request_path.find_last_of(".");
	std::string extension;
	if (last_dot_pos != std::string::npos && last_dot_pos > last_slash_pos)
	{
		extension = request_path.substr(last_dot_pos + 1);
	}

	// open the file to send
	std::string full_path = docroot_ + request_path;
	std::ifstream is(full_path.c_str(), std::ios::in | std::ios::binary);
	if (!is)
	{
		rep = response::stock_response(response::not_found);
		return;
	}

	// fill response
	rep.status = response::ok;
	char buf[512];
	while (is.read(buf, sizeof(buf)).gcount() > 0)
		rep.content.append(buf, is.gcount());

	rep.headers.resize(2);
	rep.headers[0].name = "Content-Length";
	rep.headers[0].value = lexical_cast<std::string>(rep.content.size());
	rep.headers[1].name = "Content-Type";
	rep.headers[1].value = mime_types::extension_to_type(extension);
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
				int value;
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

#endif
} // namespace x0
