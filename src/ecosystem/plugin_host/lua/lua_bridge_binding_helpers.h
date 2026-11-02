#pragma once

#include <sol/sol.hpp>
#include <utility>

// clang-format off
template <typename Class, typename Member>
auto lua_member(Member Class::* member) {
    return sol::property([member](const Class& object) { return object.*member; },
                         [member](Class& object, Member value) { object.*member = std::move(value); });
}
// clang-format on
