/* <plugins/director/HealthMonitor.h>
 *
 * This file is part of the x0 web server project and is released under AGPL-3.
 * http://www.xzero.io/
 *
 * (c) 2009-2014 Christian Parpart <trapni@gmail.com>
 */

#include "HealthMonitor.h"
#include "Backend.h"
#include "Director.h"
#include <cassert>
#include <cstdarg>

/*
 * TODO proper error reporting.
 * TODO properly support all modes ( Opportunistic, Lazy )
 */

using namespace x0;

#if !defined(XZERO_NDEBUG)
#	define TRACE(msg...) (this->debug(msg))
#else
#	define TRACE(msg...) do { } while (0)
#endif

HealthMonitor::HealthMonitor(HttpWorker& worker, HttpMessageProcessor::ParseMode parseMode) :
	Logging("HealthMonitor"),
	HttpMessageProcessor(parseMode),
	mode_(Mode::Paranoid),
	backend_(nullptr),
	worker_(worker),
	interval_(TimeSpan::fromSeconds(2)),
	state_(HealthState::Undefined),
	onStateChange_(),
	expectCode_(HttpStatus::Ok),
	timer_(worker_.loop()),
	successThreshold(2),
	failCount_(0),
	successCount_(0),
	responseCode_(HttpStatus::Undefined),
	processingDone_(false)
{
	timer_.set<HealthMonitor, &HealthMonitor::onCheckStart>(this);
}

HealthMonitor::~HealthMonitor()
{
	stop();
}

const std::string& HealthMonitor::mode_str() const
{
	static const std::string modeStr[] = {
		"paranoid", "opportunistic", "lazy"
	};

	return modeStr[static_cast<size_t>(mode_)];
}

/**
 * Sets monitoring mode.
 */
void HealthMonitor::setMode(Mode value)
{
	if (mode_ == value)
		return;

	mode_ = value;
}

const std::string& HealthMonitor::state_str() const
{
	static const std::string stateStr[] = {
		"undefined", "offline", "online"
	};

	return stateStr[static_cast<size_t>(state_)];
}

/**
 * Forces a health-state change.
 */
void HealthMonitor::setState(HealthState value)
{
	assert(value != HealthState::Undefined && "Setting state to Undefined is not allowed.");
	if (state_ == value)
		return;

	HealthState oldState = state_;
	state_ = value;

	TRACE("setState: %s", state_str().c_str());

	if (onStateChange_) {
		onStateChange_(this, oldState);
	}

	if (state_ == HealthState::Offline) {
		worker_.post<HealthMonitor, &HealthMonitor::start>(this);
	}
}

/**
 * Sets the callback to be invoked on health state changes.
 */
void HealthMonitor::setStateChangeCallback(const std::function<void(HealthMonitor*, HealthState)>& callback)
{
	onStateChange_ = callback;
}

void HealthMonitor::setBackend(Backend* backend)
{
	backend_ = backend;

#ifndef XZERO_NDEBUG
	setLoggingPrefix("HealthMonitor/%s", backend_->socketSpec().str().c_str());
#endif

	update();

	start();
}

void HealthMonitor::update()
{
	Director* director = static_cast<Director*>(backend_->manager());

	setRequest(
		"GET %s HTTP/1.1\r\n"
		"Host: %s\r\n"
		"x0-Health-Check: yes\r\n"
		"x0-Director: %s\r\n"
		"x0-Backend: %s\r\n"
		"\r\n",
		director->healthCheckRequestPath().c_str(),
		director->healthCheckHostHeader().c_str(),
		director->name().c_str(),
		backend_->name().c_str()
	);
}

void HealthMonitor::setInterval(const TimeSpan& value)
{
	interval_ = value;
}

void HealthMonitor::reset()
{
	HttpMessageProcessor::reset();

	responseCode_ = HttpStatus::Undefined;
	processingDone_ = false;
}

/**
 * Starts health-monitoring an HTTP server.
 */
void HealthMonitor::start()
{
	TRACE("start()");

	reset();

	timer_.start(interval_.value(), 0.0);
}

void HealthMonitor::onCheckStart()
{
	// XXX onCheckStart not overridden,
	// XXX so health-check is kind of useless.
}

/**
 * Stops any active timer or health-check operation.
 */
void HealthMonitor::stop()
{
	TRACE("stop()");

	if (timer_.is_active()) {
		TRACE("stop: stopping active timer");
		timer_.stop();
	}

	reset();
}

void HealthMonitor::recheck()
{
	TRACE("recheck()");
	start();
}

void HealthMonitor::logSuccess()
{
	++successCount_;

	if (successCount_ >= successThreshold) {
		TRACE("onMessageEnd: successThreshold reached.");
		setState(HealthState::Online);
	}

	recheck();
}

void HealthMonitor::logFailure()
{
	++failCount_;
	successCount_ = 0;

	setState(HealthState::Offline);

	recheck();
}

void HealthMonitor::log(x0::LogMessage&& msg)
{
	msg.addTag("monitor");
	backend_->manager()->log(std::move(msg));
}

/**
 * Callback, invoked on successfully parsed response status line.
 */
bool HealthMonitor::onMessageBegin(int versionMajor, int versionMinor, int code, const BufferRef& text)
{
	TRACE("onMessageBegin: (HTTP/%d.%d, %d, '%s')", versionMajor, versionMinor, code, text.str().c_str());

	responseCode_ = static_cast<HttpStatus>(code);

	return true;
}

/**
 * Callback, invoked on each successfully parsed response header key/value pair.
 */
bool HealthMonitor::onMessageHeader(const BufferRef& name, const BufferRef& value)
{
	TRACE("onResponseHeader(name:%s, value:%s)", name.str().c_str(), value.str().c_str());

	if (x0::iequals(name, "Status")) {
		int status = value.ref(0, value.find(' ')).toInt();
		responseCode_ = static_cast<x0::HttpStatus>(status);
	}

	return true;
}

/**
 * Callback, invoked on each partially or fully parsed response body chunk.
 */
bool HealthMonitor::onMessageContent(const BufferRef& chunk)
{
	// do nothing with response body chunk
	return true;
}

/**
 * Callback, invoked when the response message has been fully parsed.
 */
bool HealthMonitor::onMessageEnd()
{
	TRACE("onMessageEnd() state:%s", state_str().c_str());
	processingDone_ = true;

	if (responseCode_ == expectCode_) {
		logSuccess();
	} else {
		logFailure();
	}

	// stop processing
	return false;
}

JsonWriter& operator<<(JsonWriter& json, const HealthMonitor& monitor)
{
	json.beginObject()
		.name("mode")(monitor.mode_str())
		.name("state")(monitor.state_str())
		.name("interval")(monitor.interval().totalMilliseconds())
		.endObject();

	return json;
}
