/* <x0/server.hpp>
 *
 * This file is part of the x0 web server project and is released under LGPL-3.
 *
 * (c) 2009 Chrisitan Parpart <trapni@gentoo.org>
 */

#ifndef sw_x0_server_hpp
#define sw_x0_server_hpp (1)

#include <x0/datetime.hpp>
#include <x0/settings.hpp>
#include <x0/logger.hpp>
#include <x0/signal.hpp>
#include <x0/event_handler.hpp>
#include <x0/http/request_handler.hpp>
#include <x0/context.hpp>
#include <x0/types.hpp>
#include <x0/property.hpp>
#include <x0/io/fileinfo_service.hpp>
#include <x0/library.hpp>
#include <x0/api.hpp>
#include <x0/sysconfig.h>

#include <boost/signals.hpp>

#include <cstring>
#include <string>
#include <memory>
#include <list>
#include <map>

#include <ev.h>

namespace x0 {

struct plugin;

//! \addtogroup core
//@{

enum context_mask {
	SERVER = 0x01,
	HOST   = 0x02,
	PATH   = 0x04
};

inline context_mask operator|(context_mask a, context_mask b)
{
	return context_mask(int(a) | int(b));
}

/**
 * \brief implements the x0 web server.
 *
 * \see connection, request, response, plugin
 * \see server::run(), server::stop()
 */
class server :
	public boost::noncopyable
{
private:
	typedef std::pair<plugin_ptr, library> plugin_value_t;
	typedef std::map<std::string, plugin_value_t> plugin_map_t;

public:
	typedef x0::signal<void(connection *)> connection_hook;
	typedef x0::signal<void(request *)> request_parse_hook;
	typedef x0::signal<void(request *, response *)> request_post_hook;

public:
	explicit server(struct ::ev_loop *loop = 0);
	~server();

	// {{{ service control
	void configure(const std::string& configfile);
	void start();
	bool active() const;
	void run();
	void pause();
	void resume();
	void reload();
	void stop();
	// }}}

	// {{{ signals raised on request in order
	connection_hook connection_open;		//!< This hook is invoked once a new client has connected.
	request_parse_hook pre_process; 		//!< is called at the very beginning of a request.
	request_parse_hook resolve_document_root;//!< resolves document_root to use for this request.
	request_parse_hook resolve_entity;		//!< maps the request URI into local physical path.
	request_handler generate_content;		//!< generates response content for this request being processed.
	request_post_hook post_process;			//!< gets invoked right before serializing headers
	request_post_hook request_done;			//!< this hook is invoked once the request has been <b>fully</b> served to the client.
	connection_hook connection_close;		//!< is called before a connection gets closed / or has been closed by remote point.
	// }}}

	// {{{ context management
	/** create server context data for given plugin. */
	template<typename T>
	T *create_context(plugin *plug)
	{
		context_.set(plug, new T);
		return context_.get<T>(plug);
	}

	/** creates a virtual-host context for given plugin. */
	template<typename T>
	T *create_context(plugin *plug, const std::string& vhost)
	{
		auto i = vhosts_.find(vhost);
		if (i == vhosts_.end())
			vhosts_[vhost] = std::shared_ptr<x0::context>(new x0::context);

		vhosts_[vhost]->set(plug, new T);

		return vhosts_[vhost]->get<T>(plug);
	}

	void link_context(const std::string& master, const std::string& alias)
	{
		vhosts_[alias] = vhosts_[master];
	}

	/** retrieve the server configuration context. */
	x0::context *context()
	{
		return &context_;
	}

	/** retrieve server context data for given plugin
	 * \param plug plugin data for which we want to retrieve the data for.
	 */
	template<typename T>
	T *context(plugin *plug)
	{
		return context_.get<T>(plug);
	}

	/** retrieve virtual-host context data for given plugin
	 * \param plug plugin data for which we want to retrieve the data for.
	 * \param vhostid virtual-host ID given to the host queried for the configuration.
	 */
	template<typename T>
	T *context(plugin *plug, const std::string& vhostid)
	{
		auto vhost = vhosts_.find(vhostid);
		if (vhost == vhosts_.end())
			return NULL;

		auto ctx = *vhost->second;
		auto data = ctx.find(plug);
		if (data != ctx.end())
			return static_cast<T *>(data->second);

		return NULL;
	}

	template<typename T>
	T *free_context(plugin *plug)
	{
		/// \todo free all contexts owned by given plugin
		return context_.free<T>(plug);
	}
	// }}}

	/** 
	 * retrieves reference to server currently loaded configuration.
	 */
	x0::settings& config();

	/**
	 * writes a log entry into the server's error log.
	 */
	void log(severity s, const char *msg, ...);

	template<typename... Args>
	inline void debug(int level, const char *msg, Args&&... args)
	{
#if !defined(NDEBUG)
		if (level >= debug_level_)
			log(severity::debug, msg, args...);
#endif
	}

#if !defined(NDEBUG)
	int debug_level() const;
	void debug_level(int value);
#endif

	/**
	 * sets up a TCP/IP listener on given bind_address and port.
	 *
	 * If there is already a listener on this bind_address:port pair
	 * then no error will be raised.
	 */
	listener *setup_listener(int port, const std::string& bind_address = "0::0");

	/**
	 * loads a plugin into the server.
	 *
	 * \see plugin, unload_plugin(), loaded_plugins()
	 */
	void load_plugin(const std::string& name);

	/** safely unloads a plugin. */
	void unload_plugin(const std::string& name);

	/** retrieves a list of currently loaded plugins */
	std::vector<std::string> loaded_plugins() const;

	struct ::ev_loop *loop() const;

	/** retrieves the current server time. */
	const datetime& now() const;

	const std::list<listener *>& listeners() const;

	bool register_cvar_server(const std::string& key, std::function<void(const settings_value&)> callback, int priority = 0);
	bool register_cvar_host(const std::string& key, std::function<void(const settings_value&, const std::string&)> callback, int priority = 0);
	bool register_cvar_path(const std::string& key, std::function<void(const settings_value&, const std::string&, const std::string&)> callback, int priority = 0);

private:
	long long getrlimit(int resource);
	long long setrlimit(int resource, long long max);

	listener *listener_by_port(int port);

	void setup_logging(const settings_value& cvar);
	void setup_resources(const settings_value& cvar);
	void setup_modules(const settings_value& cvar);
	void setup_fileinfo(const settings_value& cvar);
	void setup_error_documents(const settings_value& cvar);
	void setup_hosts(const settings_value& cvar);
	void setup_advertise(const settings_value& cvar);

#if defined(WITH_SSL)
	static void gnutls_log(int level, const char *msg);
#endif

	friend class connection;

private:
	void handle_request(request *in, response *out);
	void loop_check(ev::check& w, int revents);

	x0::context context_;											//!< server context
	std::map<std::string, std::shared_ptr<x0::context>> vhosts_;	//!< vhost contexts
	std::list<listener *> listeners_;
	struct ::ev_loop *loop_;
	bool active_;
	x0::settings settings_;
	std::map<int, std::map<std::string, std::function<void(const settings_value&)>>> cvars_server_;
	std::map<int, std::map<std::string, std::function<void(const settings_value&, const std::string& hostid)>>> cvars_host_;
	std::map<int, std::map<std::string, std::function<void(const settings_value&, const std::string& hostid, const std::string& path)>>> cvars_path_;
	std::string configfile_;
	logger_ptr logger_;
	int debug_level_;
	bool colored_log_;
	plugin_map_t plugins_;
	datetime now_;
	ev::check loop_check_;

public:
	value_property<int> max_connections;
	value_property<int> max_keep_alive_idle;
	value_property<int> max_read_idle;
	value_property<int> max_write_idle;
	value_property<bool> tcp_cork;
	value_property<bool> tcp_nodelay;
	value_property<std::string> tag;
	value_property<bool> advertise;
	fileinfo_service fileinfo;

	property<unsigned long long> max_fds;
};

// {{{ inlines
inline struct ::ev_loop *server::loop() const
{
	return loop_;
}

inline const x0::datetime& server::now() const
{
	return now_;
}

inline const std::list<listener *>& server::listeners() const
{
	return listeners_;
}

#if !defined(NDEBUG)
inline int server::debug_level() const
{
	return debug_level_;
}

inline void server::debug_level(int value)
{
	if (value < 0)
		value = 0;
	else if (value > 9)
		value = 9;

	debug_level_ = value;
}
#endif
// }}}

//@}

} // namespace x0

#endif
// vim:syntax=cpp