#pragma once

#include <x0/flow2/FlowLocation.h>
#include <x0/flow2/FlowToken.h>
#include <x0/IPAddress.h>
#include <x0/Api.h>

#include <utility>
#include <memory>
#include <fstream>
#include <list>

namespace x0 {

class X0_API FlowLexer {
public:
	FlowLexer();
	~FlowLexer();

	bool open(const std::string& filename);
	size_t depth() const { return contexts_.size(); }
	bool eof() const;

	// processing
	FlowToken nextToken();

	// current parser state
	FlowToken token() const { return token_; }
	const FlowLocation& location() const { return location_; }

	const std::string& filename() const { return location_.filename; }
	size_t line() const { return location_.end.line; }
	size_t column() const { return location_.end.column; }

	std::string stringValue() const { return stringValue_; }
	const IPAddress& ipValue() const { return ipValue_; }
	long long numberValue() const { return numberValue_; }

private:
	struct Scope;

	Scope* enterScope(const std::string& filename);
	Scope* scope() const;
	void leaveScope();

	bool isHexChar() const;
	int currentChar() const;
	int peekChar();
	int nextChar(bool interscope = true);
	bool advanceUntil(char value);
	bool consume(char ch);
	bool consumeSpace();           // potentially enters new or leaves current context
	void processCommand(const std::string& line);

	FlowToken parseNumber();
	FlowToken parseString(bool raw);
	FlowToken parseString(char delimiter, FlowToken result);
	FlowToken parseInterpolationFragment(bool start);
	FlowToken parseIdent();

	FlowToken continueParseIPv6(bool firstComplete);
	bool ipv6HexPart();
	bool ipv6HexSeq();
	bool ipv6HexDigit4();

private:
	std::list<std::unique_ptr<Scope>> contexts_;

	int currentChar_;
	size_t ipv6HexDigits_;

	FlowLocation location_;
	FlowToken token_;
	std::string stringValue_;
	IPAddress ipValue_;
	long long numberValue_;

	int interpolationDepth_;
};

struct FlowLexer::Scope {
	std::string filename;
	std::string basedir;
	std::ifstream stream;
	FilePos currPos;
	FilePos nextPos;
	int backupChar;       //!< backup of the outer scope's currentChar

	Scope();
};

// {{{ inlines
inline bool FlowLexer::isHexChar() const
{
	return (currentChar_ >= '0' && currentChar_ <= '9')
		|| (currentChar_ >= 'a' && currentChar_ <= 'f')
		|| (currentChar_ >= 'A' && currentChar_ <= 'F');
}

inline int FlowLexer::currentChar() const
{
	return currentChar_;
}

inline FlowLexer::Scope* FlowLexer::scope() const
{
	return !contexts_.empty() ? contexts_.front().get() : nullptr;
}
// }}}

} // namespace x0