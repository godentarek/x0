#include <x0/flow/FlowType.h>

namespace x0 {

std::string tos(FlowType type)
{
	switch (type) {
		case FlowType::Void: return "void";
		case FlowType::Boolean: return "bool";
		case FlowType::Number: return "int";
		case FlowType::String: return "string";
		case FlowType::IPAddress: return "IPAddress";
		case FlowType::Cidr: return "Cidr";
		case FlowType::RegExp: return "RegExp";
		case FlowType::Handler: return "HandlerRef";
		case FlowType::IntArray: return "IntArray";
		case FlowType::StringArray: return "StringArray";
		default:
			return "";
	}
}

template<> class X0_API GCObject<std::vector<FlowNumber>>;
template<> class X0_API GCObject<std::vector<FlowString>>;
template<> class X0_API GCObject<FlowString>;

} // namespace x0
