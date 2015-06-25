/*-------------------------------------------------------------------------
 *
 * nested_loop_join.cpp
 * file description
 *
 * Copyright(c) 2015, CMU
 *
 * /peloton/src/executor/nested_loop_join_executor.cpp
 *
 *-------------------------------------------------------------------------
 */

#include "backend/executor/nested_loop_join_executor.h"

#include <vector>

#include "backend/common/types.h"
#include "backend/common/logger.h"
#include "backend/executor/logical_tile_factory.h"
#include "backend/expression/abstract_expression.h"
#include "backend/expression/container_tuple.h"

namespace nstore {
namespace executor {

/**
 * @brief Constructor for nested loop join executor.
 * @param node Nested loop join node corresponding to this executor.
 */
NestedLoopJoinExecutor::NestedLoopJoinExecutor(planner::AbstractPlanNode *node)
: AbstractExecutor(node, nullptr) {
}

/**
 * @brief Do some basic checks and create the schema for the output logical tiles.
 * @return true on success, false otherwise.
 */
bool NestedLoopJoinExecutor::DInit() {
  assert(children_.size() == 2);

  // Grab data from plan node.
  const planner::NestedLoopJoinNode &node = GetNode<planner::NestedLoopJoinNode>();

  // NOTE: predicate can be null for cartesian product
  predicate_ = node.GetPredicate();
  left_scan_start = true;

  return true;
}

/**
 * @brief Creates logical tiles from the two input logical tiles after applying join predicate.
 * @return true on success, false otherwise.
 */
bool NestedLoopJoinExecutor::DExecute() {

  LOG_TRACE("********** Nested Loop Join executor :: 2 children \n");

  bool right_scan_end = false;
  // Try to get next tile from RIGHT child
  if (children_[1]->Execute() == false) {
    LOG_TRACE("Did not get right tile \n");
    right_scan_end = true;
  }

  if(right_scan_end == true){
    LOG_TRACE("Resetting scan for right tile \n");
    children_[1]->Init();
    if (children_[1]->Execute() == false) {
      LOG_ERROR("Did not get right tile on second try\n");
      return false;
    }
  }

  LOG_TRACE("Got right tile \n");

  if(left_scan_start == true || right_scan_end == true) {
    left_scan_start = false;
    // Try to get next tile from LEFT child
    if (children_[0]->Execute() == false) {
      LOG_TRACE("Did not get left tile \n");
      return false;
    }
    LOG_TRACE("Got left tile \n");
  }
  else {
    LOG_TRACE("Already have left tile \n");
  }

  std::unique_ptr<LogicalTile> left_tile(children_[0]->GetOutput());
  std::unique_ptr<LogicalTile> right_tile(children_[1]->GetOutput());

  // Check the input logical tiles.
  assert(left_tile.get() != nullptr);
  assert(right_tile.get() != nullptr);

  // Construct output logical tile.
  std::unique_ptr<LogicalTile> output_tile(LogicalTileFactory::GetTile());

  auto output_tile_schema = left_tile.get()->GetSchema();
  auto right_tile_schema = right_tile.get()->GetSchema();

  output_tile_schema.insert(output_tile_schema.end(),
                            right_tile_schema.begin(), right_tile_schema.end());

  // Set the output logical tile schema
  output_tile.get()->SetSchema(std::move(output_tile_schema));

  // Now, let's compute the position lists for the output tile

  // Cartesian product

  // Add everything from two logical tiles
  auto left_tile_position_lists = left_tile.get()->GetPositionLists();
  auto right_tile_position_lists = right_tile.get()->GetPositionLists();

  // Compute output tile column count
  size_t left_tile_column_count = left_tile_position_lists.size();
  size_t right_tile_column_count = right_tile_position_lists.size();
  size_t output_tile_column_count = left_tile_column_count + right_tile_column_count;

  assert(left_tile_column_count > 0);
  assert(right_tile_column_count > 0);

  // Compute output tile row count
  size_t left_tile_row_count = left_tile_position_lists[0].size();
  size_t right_tile_row_count = right_tile_position_lists[0].size();

  // Construct position lists for output tile
  std::vector< std::vector<oid_t> > position_lists;
  for(size_t column_itr = 0; column_itr < output_tile_column_count; column_itr++)
    position_lists.push_back(std::vector<oid_t>());

  // Go over every pair of tuples in left and right logical tiles
  for(size_t left_tile_row_itr = 0 ; left_tile_row_itr < left_tile_row_count; left_tile_row_itr++){
    for(size_t right_tile_row_itr = 0 ; right_tile_row_itr < right_tile_row_count; right_tile_row_itr++){

      // TODO: OPTIMIZATION : Can split the control flow into two paths -
      // one for cartesian product and one for join
      // Then, we can skip this branch atleast for the cartesian product path.

      // Join predicate exists
      if (predicate_ != nullptr) {

        expression::ContainerTuple<executor::LogicalTile> left_tuple(left_tile.get(), left_tile_row_itr);
        expression::ContainerTuple<executor::LogicalTile> right_tuple(right_tile.get(), right_tile_row_itr);

        // Join predicate is false. Skip pair and continue.
        if(predicate_->Evaluate(&left_tuple, &right_tuple).IsFalse()){
          continue;
        }
      }

      // Insert a tuple into the output logical tile

      // First, copy the elements in left logical tile's tuple
      for(size_t output_tile_column_itr = 0 ;
          output_tile_column_itr < left_tile_column_count;
          output_tile_column_itr++){
        position_lists[output_tile_column_itr].push_back(
            left_tile_position_lists[output_tile_column_itr][left_tile_row_itr]);
      }

      // Then, copy the elements in left logical tile's tuple
      for(size_t output_tile_column_itr = 0 ;
          output_tile_column_itr < right_tile_column_count;
          output_tile_column_itr++){
        position_lists[left_tile_column_count + output_tile_column_itr].push_back(
            right_tile_position_lists[output_tile_column_itr][right_tile_row_itr]);
      }

    }
  }

  // Check if we have any matching tuples.
  if(position_lists[0].size() > 0){
    output_tile.get()->SetPositionLists(std::move(position_lists));
    std::cout << *(output_tile.get());
    SetOutput(output_tile.release());
    return true;
  }
  // Try again
  else{
    // If we are out of any more pairs of child tiles to examine,
    // then we will return false earlier in this function.
    // So, we don't have to return false here.
    DExecute();
  }

  return true;
}

} // namespace executor
} // namespace nstore

