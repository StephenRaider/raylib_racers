#pragma once
// Car specifications as data, and the development (token) system on top of them.
//
//  - A spec file (specs/<name>.json) sets any CarParams field by name; fields it
//    leaves out keep the built-in defaults (the 2004-2010 F1 car).
//  - The development rules (specs/development.json) define categories such as
//    top speed or pit crew, what one token in each does to the car, and how many
//    tokens a team may spend. A car's development is a string like
//    "top_speed=3,downforce=-1,pit_crew=2".
#include <string>
#include <vector>

#include "car.hpp"

namespace rr {

struct DevEffect {
    std::string field;   // CarParams field
    float perToken = 0;  // relative change per token: value *= (1 + perToken)^tokens
};

struct DevCategory {
    std::string key;     // e.g. "top_speed"
    std::string label;   // e.g. "Top speed"
    std::string about;
    std::vector<DevEffect> effects;
};

struct DevRules {
    int budget = 0;      // total tokens a team may spend (sum of positive minus refunds)
    int minTokens = 0, maxTokens = 0;  // per category
    std::vector<DevCategory> categories;
    bool empty() const { return categories.empty(); }
};

// Searches dirs for name, name + ".json"; returns "" if not found. A path that exists is returned as is.
std::string findDataFile(const std::string& name, const std::vector<std::string>& dirs);

bool loadCarSpec(const std::string& path, CarParams& out, std::string* err);
bool loadDevRules(const std::string& path, DevRules& out, std::string* err);

// Parses "key=n,key=n" against the rules: unknown keys, tokens outside
// [minTokens, maxTokens] or a total over the budget are errors.
bool parseDevelopment(const DevRules& rules, const std::string& dev, std::vector<int>& tokens, std::string* err);
void applyDevelopment(const DevRules& rules, const std::vector<int>& tokens, CarParams& p);

}  // namespace rr
