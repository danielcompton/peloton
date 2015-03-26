/*-------------------------------------------------------------------------
 *
 * abstract_tuple.h
 * file description
 *
 * Copyright(c) 2015, CMU
 *
 * /n-store/src/storage/abstract_tuple.h
 *
 *-------------------------------------------------------------------------
 */

#pragma once

#include <memory>

#include "../catalog/schema.h"
#include "common/value.h"
#include "common/value_factory.h"
#include "common/value_peeker.h"
#include "common/types.h"

namespace nstore {
namespace storage {

//===--------------------------------------------------------------------===//
// Tuple class
//===--------------------------------------------------------------------===//

class Tuple {
	friend class catalog::Schema;
	friend class ValuePeeker;
	friend class Tile;

public:

	// Default constructor (don't use this)
	inline Tuple() : tuple_schema(nullptr), tuple_data(nullptr) {
	}

	// Setup the tuple given a table
	inline Tuple(const Tuple &rhs) : tuple_schema(rhs.tuple_schema), tuple_data(rhs.tuple_data) {
	}

	// Setup the tuple given a schema
	inline Tuple(catalog::Schema *schema) : tuple_schema(schema), tuple_data(nullptr) {
		assert(tuple_schema);
	}

	// Setup the tuple given a schema and location
	inline Tuple(catalog::Schema *schema, char* data) : tuple_schema(schema), tuple_data(data) {
		assert(tuple_schema);
		assert(tuple_data);
	}

	// Setup the tuple given a schema and allocate space
	inline Tuple(catalog::Schema *schema, bool allocate) : tuple_schema(schema), tuple_data(nullptr) {
		assert(tuple_schema);

		if(allocate) {
			tuple_data = new char[tuple_schema->GetLength()];
		}

	}

	// Deletes tuple data
	// Does not delete either SCHEMA or UNINLINED data
	// Tile or larger entities must take care of this
	~Tuple() {

	  // delete the tuple data
		delete[] tuple_data;
	}

	// Setup the tuple given the specified data location and schema
	Tuple(char *data, catalog::Schema *schema);

	// Assignment operator
	Tuple& operator=(const Tuple &rhs);

	void Copy(const void *source, Pool *pool = NULL);

	/**
	 * Set the tuple to point toward a given address in a table's
	 * backing store
	 */
	inline void Move(void *address) {
		tuple_data = reinterpret_cast<char*> (address);
	}

	bool operator==(const Tuple &other) const;
	bool operator!=(const Tuple &other) const;

	int Compare(const Tuple &other) const;

	//===--------------------------------------------------------------------===//
	// Getters and Setters
	//===--------------------------------------------------------------------===//

	// Get the value of a specified column (const)
	// (expensive) checks the schema to see how to return the Value.
	inline const Value GetValue(const id_t column_id) const;

	// Set appropriate column in tuple
	void SetValue(const id_t column_id, Value value);

	inline int GetLength() const {
		return tuple_schema->GetLength();
	}

	// Is the column value null ?
	inline bool IsNull(const uint64_t column_id) const {
		return GetValue(column_id).IsNull();
	}

	// Is the tuple null ?
	inline bool IsNull() const {
		return tuple_data == NULL;
	}

	// Get the type of a particular column in the tuple
	inline ValueType GetType(int column_id) const {
		return tuple_schema->GetType(column_id);
	}

	inline catalog::Schema *GetSchema() const {
		return tuple_schema;
	}

	// Get the address of this tuple in the table's backing store
	inline char* Location() const {
		return tuple_data;
	}

	// Return the number of columns in this tuple
	inline id_t GetColumnCount() const {
		return tuple_schema->GetColumnCount();
	}

	// Release to the heap any memory allocated for any uninlined columns.
	void FreeUninlinedData();

	bool EqualsNoSchemaCheck(const Tuple &other) const;

	// this does set NULL in addition to clear string count.
	void SetAllNulls();

	void SetNull() {
		tuple_data = NULL;
	}

	/**
	 * Determine the maximum number of bytes when serialized for Export.
	 * Excludes the bytes required by the row header (which includes
	 * the null bit indicators) and ignores the width of metadata columns.
	 */
	size_t ExportSerializationSize() const;

	// Return the amount of memory allocated for non-inlined objects
	size_t GetUninlinedMemorySize() const;

	/**
	 * Allocate space to copy strings that can't be inlined rather
	 * than copying the pointer.
	 *
	 * Used when setting a NValue that will go into permanent storage in a persistent table.
	 * It is also possible to provide NULL for stringPool in which case
	 * the strings will be allocated on the heap.
	 */
	void SetValueAllocate(const id_t column_id, Value value, Pool *dataPool);

	//===--------------------------------------------------------------------===//
	// Serialization utilities
	//===--------------------------------------------------------------------===//

	void SerializeTo(SerializeOutput &output);
	void SerializeToExport(ExportSerializeOutput &output, int col_offset,
			uint8_t *null_array);
	void SerializeWithHeaderTo(SerializeOutput &output);

	void DeserializeFrom(SerializeInput &input, Pool *pool);
	int64_t DeserializeWithHeaderFrom(SerializeInput &input);

	size_t HashCode(size_t seed) const;
	size_t HashCode() const;

	// Get a string representation of this tuple
	friend std::ostream& operator<< (std::ostream& os, const Tuple& tuple);

private:

	char* GetDataPtr(const id_t column_id);

	const char* GetDataPtr(const id_t column_id) const;

	//===--------------------------------------------------------------------===//
	// Data members
	//===--------------------------------------------------------------------===//

	// The types of the columns in the tuple
	catalog::Schema *tuple_schema;

	// The tuple data, padded at the front by the TUPLE_HEADER
	char *tuple_data;

};

//===--------------------------------------------------------------------===//
// Implementation
//===--------------------------------------------------------------------===//

// Setup the tuple given the specified data location and schema
inline Tuple::Tuple(char *data, catalog::Schema *schema) {
	assert(data);
	assert(schema);

	tuple_data = data;
	tuple_schema = schema;
}

inline Tuple& Tuple::operator=(const Tuple &rhs) {
	tuple_schema = rhs.tuple_schema;
	tuple_data = rhs.tuple_data;
	return *this;
}

// Get the value of a specified column (const)
// (expensive) checks the schema to see how to return the Value.
inline const Value Tuple::GetValue(const id_t column_id) const {
	assert(tuple_schema);
	assert(tuple_data);

  // NOTE: same logic used here as that used in
	// "Tile::GetValue(const id_t tuple_slot_id, const id_t column_id)"

	const ValueType column_type = tuple_schema->GetType(column_id);

	const char* data_ptr = GetDataPtr(column_id);
	const bool is_inlined = tuple_schema->IsInlined(column_id);

	return Value::Deserialize(data_ptr, column_type, is_inlined);
}

// Set scalars by value and uninlined columns by reference into this tuple.
inline void Tuple::SetValue(const id_t column_id, Value value) {
	assert(tuple_schema);
	assert(tuple_data);

	const ValueType type = tuple_schema->GetType(column_id);
	value = value.CastAs(type);

	const bool is_inlined = tuple_schema->IsInlined(column_id);
	char *dataPtr = GetDataPtr(column_id);
	int32_t column_length = tuple_schema->GetLength(column_id);

	if(is_inlined == false)
		column_length = tuple_schema->GetVariableLength(column_id);

	value.Serialize(dataPtr, is_inlined, column_length);
}

} // End storage namespace
} // End nstore namespace
