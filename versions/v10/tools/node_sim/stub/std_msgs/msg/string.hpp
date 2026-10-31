#pragma once
#include <memory>
#include <string>
namespace std_msgs { namespace msg { struct String { std::string data; using SharedPtr = std::shared_ptr<String>; using ConstSharedPtr = std::shared_ptr<const String>; }; } }
