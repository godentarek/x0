/* <x0/connection.cpp>
 *
 * This file is part of the x0 web server, released under GPLv3.
 * (c) 2009 Chrisitan Parpart <trapni@gentoo.org>
 */

#include <x0/connection.hpp>
#include <x0/connection_manager.hpp>
#include <x0/types.hpp>
#include <boost/bind.hpp>

namespace x0 {

connection::connection(
	io_service& io_service, 
	connection_manager& manager, const request_handler_fn& handler)
  : socket_(io_service),
	connection_manager_(manager),
	request_handler_(handler)
{
	request_.connection = this;
}

connection::~connection()
{
}

/**
 * gets the system socket handle for this connection.
 */
ip::tcp::socket& connection::socket()
{
	return socket_;
}

/**
 * starts processing events from this connection.
 */
void connection::start()
{
	socket_.async_read_some(buffer(buffer_),
		bind(&connection::handle_read, shared_from_this(),
			placeholders::error,
			placeholders::bytes_transferred));
}

/**
 * Stop this connection.
 */
void connection::stop()
{
	socket_.close();
}

/**
 * This method gets invoked when there is data in our connection ready to read.
 *
 * We assume, that we are in request-parsing state.
 */
void connection::handle_read(const system::error_code& e, std::size_t bytes_transferred)
{
	if (!e)
	{
		// parse request (partial)
		tribool result;
		try
		{
			tie(result, tuples::ignore) = request_parser_.parse(
				request_, buffer_.data(), buffer_.data() + bytes_transferred);
		}
		catch (response_ptr reply)
		{
			fprintf(stderr, "response_ptr exception caught (%d %s)\n", reply->status, response::status_cstr(reply->status));;
			fflush(stderr);
			response_ = reply;

			// initiate response sending
			async_write(socket_, response_->to_buffers(),
				bind(&connection::handle_write, shared_from_this(),
					placeholders::error));
		}

		if (result) // request fully parsed
		{
			response_.reset(new response());
			// -> handle request
			try
			{
				request_handler_(request_, *response_);
			}
			catch (response_ptr reply)
			{
				fprintf(stderr, "response_ptr exception caught (%d %s)\n", reply->status, response::status_cstr(reply->status));;
				fflush(stderr);
				response_ = reply;
			}

			// initiate response sending
			async_write(socket_, response_->to_buffers(),
				bind(&connection::handle_write, shared_from_this(),
					placeholders::error));
		}
		else if (!result) // received an invalid request
		{
			// -> send stock response: BAD_REQUEST
			response_ = response::bad_request;

			// initiate response sending
			async_write(socket_, response_->to_buffers(),
				bind(&connection::handle_write, shared_from_this(),
					placeholders::error));
		}
		else // request still incomplete
		{
			// -> continue reading for request
			socket_.async_read_some(buffer(buffer_),
				bind(&connection::handle_read, shared_from_this(),
					placeholders::error,
					placeholders::bytes_transferred));
		}
	}
	else if (e != error::operation_aborted)
	{
		// some connection error (other than operation_aborted) happened
		// -> kill this connection.
		connection_manager_.stop(shared_from_this());
	}
}

/**
 * this method gets invoked when response write operation has finished.
 *
 * We will fully shutown the TCP connection.
 */
void connection::handle_write(const system::error_code& e)
{
	if (!e)
	{
		system::error_code ignored;
		socket_.shutdown(ip::tcp::socket::shutdown_both, ignored);
	}

	if (e != error::operation_aborted)
	{
		connection_manager_.stop(shared_from_this());
	}
}

} // namespace x0
