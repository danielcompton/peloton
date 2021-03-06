//===----------------------------------------------------------------------===//
//
//                         Peloton
//
// group.h
//
// Identification: src/include/optimizer/group.h
//
// Copyright (c) 2015-16, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include "optimizer/group_expression.h"
#include "optimizer/operator_node.h"
#include "optimizer/property.h"

#include <unordered_map>
#include <vector>

namespace peloton {
namespace optimizer {

using GroupID = int32_t;

const GroupID UNDEFINED_GROUP = -1;

//===--------------------------------------------------------------------===//
// Group
//===--------------------------------------------------------------------===//
class Group {
 public:
  Group(GroupID id);
  void add_item(Operator op);

  void AddExpression(std::shared_ptr<GroupExpression> expr);

  void SetExpressionCost(std::shared_ptr<GroupExpression> expr, double cost,
                         PropertySet properties);

  std::shared_ptr<GroupExpression> GetBestExpression(PropertySet properties);

  const std::vector<std::shared_ptr<GroupExpression>> &GetExpressions() const;

  inline void SetExplorationFlag() { has_explored_ = true; }
  inline bool HasExplored() { return has_explored_; }

  inline void SetImplementationFlag() { has_implemented_ = true; }
  inline bool HasImplemented() { return has_implemented_; }

 private:
  GroupID id_;
  std::vector<Operator> items_;
  std::vector<std::shared_ptr<GroupExpression>> expressions_;
  std::unordered_map<PropertySet,
                     std::tuple<double, std::shared_ptr<GroupExpression>>>
      lowest_cost_expressions_;

  // Whether equivalent logical expressions have been explored for this group
  bool has_explored_;

  // Whether physical operators have been implemented for this group
  bool has_implemented_;
};

} /* namespace optimizer */
} /* namespace peloton */
