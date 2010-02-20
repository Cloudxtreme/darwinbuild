/*
 * Copyright (c) 2010 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_BSD_LICENSE_HEADER_START@
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @APPLE_BSD_LICENSE_HEADER_END@
 */

#include "Database.h"

/**
 * sqlite3_trace callback for debugging
 */
void dbtrace(void* context, const char* sql) {
	IF_DEBUG("[TRACE] %s \n", sql);
}

void __blob_hex(uint8_t* data, uint32_t size) {
	if (!size) return;
	for (uint32_t i=0; i < size; i++) {
		IF_DEBUG("%02x", data[i]);
	}
	IF_DEBUG("\n");
}


Database::Database() {
	// XXX: make the initial allocation for 2 to tailor to darwinup usage
	m_table_max = 1;
	m_table_count = 0;
	m_tables = (Table**)malloc(sizeof(Table*) * m_table_max);
	this->init_cache();
	m_db = NULL;	
	m_path = NULL;
	m_error_size = ERROR_BUF_SIZE;
	m_error = (char*)malloc(m_error_size);
}

Database::Database(const char* path) {
	m_table_max = 1;
	m_table_count = 0;
	m_tables = (Table**)malloc(sizeof(Table*) * m_table_max);
	this->init_cache();
	m_db = NULL;		
	m_path = strdup(path);
	if (!m_path) {
		fprintf(stderr, "Error: ran out of memory when constructing database object.\n");
	}
	m_error_size = ERROR_BUF_SIZE;
	m_error = (char*)malloc(m_error_size);
}

Database::~Database() {
	for (uint32_t i = 0; i < m_table_count; i++) {
		delete m_tables[i];
	}
	this->destroy_cache();
	
	sqlite3_finalize(m_begin_transaction);
	sqlite3_finalize(m_rollback_transaction);
	sqlite3_finalize(m_commit_transaction);

	free(m_tables);
	free(m_path);
	free(m_error);
}


void Database::init_schema() {
	// do nothing... children should implement this
}


void Database::init_cache() {
	IF_DEBUG("CACHE: init_cache \n");
	cache_attributes_t attrs;
	attrs.version = CACHE_ATTRIBUTES_VERSION_2;
	attrs.key_hash_cb = cache_key_hash_cb_cstring;
	attrs.key_is_equal_cb = cache_key_is_equal;
	attrs.key_retain_cb = cache_key_retain;
	attrs.key_release_cb = cache_key_release;
	attrs.value_release_cb = cache_value_release;
	attrs.value_retain_cb = cache_value_retain;
	cache_create("org.macosforge.darwinbuild.darwinup.statements", &attrs, &m_statement_cache);
}

void Database::destroy_cache() {
	IF_DEBUG("CACHE: destroy_cache \n");
	cache_destroy(m_statement_cache);
}

bool cache_key_is_equal(void* key1, void* key2, void* user) {
	bool res = (strcmp((char*)key1, (char*)key2) == 0);
	IF_DEBUG("CACHE: key1: %s key2: %s res: %d\n", (char*)key1, (char*)key2, res);
	return res;
}

void cache_key_retain(void* key_in, void** key_out, void* user_data) {
	IF_DEBUG("CACHE: key_retain %s\n", (char*)key_in);
	*key_out = strdup((char*)key_in);
}

void cache_key_release(void* key, void* user_data) {
	IF_DEBUG("CACHE: key_release %s\n", (char*)key);
	free(key);
}

void cache_value_retain(void* value, void* user_data) {
	IF_DEBUG("CACHE: value_retain %p\n", value);
	// do nothing
}

void cache_value_release(void* value, void* user_data) {
	IF_DEBUG("CACHE: value_release %p\n", value);
	sqlite3_finalize((sqlite3_stmt*)value);
}


const char* Database::path() {
	return m_path;
}

const char* Database::error() {
	return m_error;
}

int Database::connect() {
	int res = SQLITE_OK;
	this->init_schema();
	res = sqlite3_open(m_path, &m_db);
	if (res) {
		sqlite3_close(m_db);
		m_db = NULL;
		fprintf(stderr, "Error: unable to connect to database at: %s \n", m_path);
		return res;
	}	
	sqlite3_trace(m_db, dbtrace, NULL);
	if (this->is_empty()) {
		assert(this->create_tables() == 0);
	}
	
	// prepare transaction statements
	if (res == SQLITE_OK) res = sqlite3_prepare_v2(m_db, "BEGIN TRANSACTION", 18,
												   &m_begin_transaction, NULL);
	if (res == SQLITE_OK) res = sqlite3_prepare_v2(m_db, "ROLLBACK TRANSACTION", 21,
												   &m_rollback_transaction, NULL);
	if (res == SQLITE_OK) res = sqlite3_prepare_v2(m_db, "COMMIT TRANSACTION", 19,
												   &m_commit_transaction, NULL);
	
	return res;	
}

int Database::connect(const char* path) {
	this->m_path = strdup(path);
	if (!m_path) {
		fprintf(stderr, "Error: ran out of memory when trying to connect to database.\n");
		return 1;
	}
	return this->connect();
}


int Database::add_table(Table* t) {
	if (m_table_count >= m_table_max) {
		m_tables = (Table**)realloc(m_tables, m_table_max * sizeof(Table*) * REALLOC_FACTOR);
		if (!m_tables) {
			fprintf(stderr, "Error: unable to reallocate memory to add a table\n");
			return 1;
		}
		m_table_max *= REALLOC_FACTOR;
	}
	m_tables[m_table_count++] = t;
	
	return 0;
}

int Database::create_tables() {
	int res = SQLITE_OK;
	for (uint32_t i=0; i<m_table_count; i++) {
		IF_DEBUG("[DATABASE] creating table #%u \n", i);
		res = this->sql_once(m_tables[i]->create());
		if (res!=SQLITE_OK) {
			fprintf(stderr, "Error: sql error trying to create table: %s: %s\n", 
					m_tables[i]->name(), m_error);
			return res;
		}
	}
	return res;
}

/**
 * attempt to get a row count of the first table to detect if the schema
 * needs to be initialized
 */
bool Database::is_empty() {
	if (!m_tables[0]) {
		fprintf(stderr, "Error: Database has not had a schema initialized.\n");
		return false;
	}
	int res = SQLITE_OK;
	char* query;
	asprintf(&query, "SELECT count(*) FROM %s;", m_tables[0]->name());
	res = sqlite3_exec(this->m_db, query, NULL, NULL, NULL);
	free(query);
	return res != SQLITE_OK;
}

int Database::sql_once(const char* fmt, ...) {
	int res = 0;
    va_list args;
    va_start(args, fmt);
    char* error;
    if (this->m_db) {
        char *query = sqlite3_vmprintf(fmt, args);
        res = sqlite3_exec(this->m_db, query, NULL, NULL, &error);
        sqlite3_free(query);
    } else {
        fprintf(stderr, "Error: database not open.\n");
        res = SQLITE_ERROR;
    }
    va_end(args);
	if (error) {
		strlcpy(m_error, error, m_error_size);
		fprintf(stderr, "Error: sql(): %s \n", m_error);
		fprintf(stderr, "Error: fmt: %s \n", fmt);
		sqlite3_free(error);
	}
	return res;
}

int Database::execute(sqlite3_stmt* stmt) {
	int res = sqlite3_step(stmt);
	if (res == SQLITE_DONE) {
		res = SQLITE_OK;
	} else {
		strlcpy(m_error, sqlite3_errmsg(m_db), m_error_size);
		fprintf(stderr, "Error: execute() error: %s \n", m_error);
	}
	res = sqlite3_reset(stmt);
	return res;
}


#define __bind_all_columns(_lastarg) \
    va_list args; \
    va_start(args, _lastarg); \
	for (uint32_t i=0; i<table->column_count(); i++) { \
		Column* col = table->column(i); \
		if (col->is_pk()) continue; \
		uint8_t* bdata = NULL; \
		uint32_t bsize = 0; \
		switch(col->type()) { \
			case TYPE_INTEGER: \
				res = sqlite3_bind_int64(stmt, param++, va_arg(args, uint64_t)); \
				break; \
			case TYPE_TEXT: \
				res = sqlite3_bind_text(stmt, param++, va_arg(args, char*), -1, SQLITE_STATIC); \
				break; \
			case TYPE_BLOB: \
				bdata = va_arg(args, uint8_t*); \
				bsize = va_arg(args, uint32_t); \
                res = sqlite3_bind_blob(stmt, param++, \
										bdata, \
										bsize, \
										SQLITE_STATIC); \
				break; \
		} \
		if (res != SQLITE_OK) { \
			fprintf(stderr, "Error: failed to bind parameter #%d with column #%d of type %d " \
					"table %s \n", \
					param, i, col->type(), table->name()); \
			return res; \
		} \
	} \
    va_end(args);

#define __bind_va_columns(_lastarg) \
    va_list args; \
    va_start(args, _lastarg); \
    fprintf(stderr, "DEBUG: sql: %s \n", sqlite3_sql(stmt)); \
    for (uint32_t i=0; i<count; i++) { \
        Column* col = va_arg(args, Column*); \
        uint8_t* bdata = NULL; \
        uint32_t bsize = 0; \
        char* tval; \
        switch(col->type()) { \
            case TYPE_INTEGER: \
                res = sqlite3_bind_int64(stmt, param++, va_arg(args, uint64_t)); \
                break; \
            case TYPE_TEXT: \
                tval = va_arg(args, char*); \
                if (tval[0] == '!' || tval[0] == '>' || tval[0] == '<') tval++; \
                res = sqlite3_bind_text(stmt, param++, tval, -1, SQLITE_STATIC); \
                break; \
            case TYPE_BLOB: \
                bdata = va_arg(args, uint8_t*); \
                bsize = va_arg(args, uint32_t); \
                res = sqlite3_bind_blob(stmt, param++, \
                                        bdata, \
										bsize, \
                                        SQLITE_STATIC); \
                break; \
		} \
        if (res != SQLITE_OK) { \
            fprintf(stderr, "Error: failed to bind parameter #%d with column #%d of type %d " \
                            "table %s res %d: %s  \n", \
							param-1, i, col->type(), table->name(), res, sqlite3_errmsg(m_db)); \
            return res; \
        } \
    } \
    va_end(args);

#define __get_stmt(expr) \
	sqlite3_stmt* stmt; \
	char* key = strdup(name); \
	cache_get_and_retain(m_statement_cache, key, (void**)&stmt); \
	if (!stmt) { \
		va_list args; \
		va_start(args, count); \
		stmt = expr; \
		va_end(args); \
		cache_set_and_retain(m_statement_cache, key, stmt, sizeof(stmt)); \
	} \
	free(key);

size_t Database::store_column(sqlite3_stmt* stmt, int column, uint8_t* output) {
	IF_DEBUG("store_column: column = %d  output = %p \n", column, output);
	size_t used;
	int type = sqlite3_column_type(stmt, column);
	const void* blob;
	int blobsize;
	IF_DEBUG("column type = %d \n", type);
	switch(type) {
		case SQLITE_INTEGER:
			*(uint64_t*)output = (uint64_t)sqlite3_column_int64(stmt, column);
			used = sizeof(uint64_t);
			IF_DEBUG("store_column used=%u output(%p) = %llu \n", 
					 (uint32_t)used, output, *(uint64_t*)output);
			break;
		case SQLITE_TEXT:
			*(const char**)output = strdup((const char*)sqlite3_column_text(stmt, column));
			IF_DEBUG("[ALLOC] text: %p %s \n", *(char**)output, *(char**)output);
			used = sizeof(char*);
			IF_DEBUG("store_column used=%u output(%p) = %s \n", 
					 (uint32_t)used, output, *(char**)output);
			break;
		case SQLITE_BLOB:
			blob = sqlite3_column_blob(stmt, column);
			blobsize = sqlite3_column_bytes(stmt, column);
			IF_DEBUG("blob(%p) size=%d \n", blob, blobsize);
			*(void**)output = malloc(blobsize);
			IF_DEBUG("[ALLOC] blob %p \n", *(void**)output);
			if (*output && blobsize) memcpy(*(void**)output, blob, blobsize);
			used = sizeof(void*);
			IF_DEBUG("store_column used=%u output(%p) = %s \n", 
					 (uint32_t)used, output, *(char**)output);
			break;
		default:
			fprintf(stderr, "Error: unhandled column type in Database::store_column(): %d \n", 
					type);
			return 0;
	}

	return used;
}

/**
 *
 *
 *   will not realloc memory for output since caller should know how
 *   much to alloc in the first place. Sets used to be how many bytes
 *   were written to output
 */
int Database::step_once(sqlite3_stmt* stmt, uint8_t* output, uint32_t* used) {
	int res = sqlite3_step(stmt);
	uint8_t* current = output;
	IF_DEBUG("output = %p current = %p \n", output, current);
	if (used) *used = 0;
	if (used) IF_DEBUG("step_once used(%p) = %u \n", used, *used);

	if (res == SQLITE_ROW) {
		int count = sqlite3_column_count(stmt);
		for (int i = 0; i < count; i++) {
			IF_DEBUG("loop current ptr before = %p \n", current);
			current += this->store_column(stmt, i, current);
			IF_DEBUG("loop current ptr after = %p \n", current);
		}
		if (used) {
			*used = current - output;
			IF_DEBUG("step_once after store used(%p) = %u \n", used, *used);
		}
		res = SQLITE_OK;
	}

	return res;
}

int Database::step_column(sqlite3_stmt* stmt, void** output, uint32_t size, uint32_t* count) {
	uint32_t used = 0;
	uint32_t total_used = used;
	uint32_t rowsize = size / INITIAL_ROWS;
	uint8_t* current = *(uint8_t**)output;
	*count = 0;
	IF_DEBUG("rowsize = %u \n", rowsize);
	int res = SQLITE_OK;
	while (res == SQLITE_OK) {
		current = *(uint8_t**)output + total_used;
		IF_DEBUG("calling step_once with current(%p) \n", current);
		res = this->step_once(stmt, current, &used);
		if (res == SQLITE_OK) (*count)++;
		total_used += used;
		IF_DEBUG("stepped: used = %u total_used = %u size = %u \n", used, total_used, size);
		if (total_used >= (size - rowsize)) {
			size *= REALLOC_FACTOR;
			*output = realloc(*output, size);
			IF_DEBUG("reallocating: output = %p  size = %u \n", *output, size);
			if (!*output) {
				fprintf(stderr, "Error: ran out of memory in Database::step_once \n");
				return SQLITE_ERROR;
			}
		}		
	}
    sqlite3_reset(stmt);
	return res;
}

int Database::get_value(const char* name, void** output, Table* table, Column* value_column, 
						uint32_t count, ...) {
	__get_stmt(table->get_column(m_db, value_column, count, args));
	int res = SQLITE_OK;
	uint32_t param = 1;
	__bind_va_columns(count);
	uint32_t size = value_column->size();
	*output = malloc(size);
	res = this->step_once(stmt, (uint8_t*)*output, NULL);
	IF_DEBUG("get_value output(%p) = %llu \n", *output, **(uint64_t**)output);
	sqlite3_reset(stmt);
	cache_release_value(m_statement_cache, &stmt);
	return res;
}

int Database::get_column(const char* name, void** output, uint32_t* result_count, 
						 Table* table, Column* column, uint32_t count, ...) {
	__get_stmt(table->get_column(m_db, column, count, args));
	IF_DEBUG("stmt = %s \n", sqlite3_sql(stmt));
	int res = SQLITE_OK;
	uint32_t param = 1;
	__bind_va_columns(count);
	uint32_t size = INITIAL_ROWS * column->size();
	*output = malloc(size);
	IF_DEBUG("get_column output = %p  size = %u \n", *output, size);
	res = this->step_column(stmt, output, size, result_count);
	IF_DEBUG("get_colu output(%p) = %llu \n", *output, **(uint64_t**)output);
	sqlite3_reset(stmt);
	cache_release_value(m_statement_cache, &stmt);
	return res;
}

int Database::get_row(const char* name, uint8_t** output, Table* table, uint32_t count, ...) {
	__get_stmt(table->get_row(m_db, count, args));
	IF_DEBUG("stmt = %s \n", sqlite3_sql(stmt));
	int res = SQLITE_OK;
	uint32_t param = 1;
	__bind_va_columns(count);
	*output = table->alloc_result();
	IF_DEBUG("Table::alloc_result = %p \n", *output);
	res = this->step_once(stmt, *output, NULL);
	IF_DEBUG("get_row output(%p) = %llu \n", *output, **(uint64_t**)output);
	sqlite3_reset(stmt);
	cache_release_value(m_statement_cache, &stmt);
	return res;
}

int Database::get_row_ordered(const char* name, uint8_t** output, Table* table, Column* order_by,
							  int order, uint32_t count, ...) {
	__get_stmt(table->get_row_ordered(m_db, order_by, order, count, args));
	IF_DEBUG("stmt = %s \n", sqlite3_sql(stmt));
	int res = SQLITE_OK;
	uint32_t param = 1;
	__bind_va_columns(count);
	*output = table->alloc_result();
	IF_DEBUG("Table::alloc_result = %p \n", *output);
	res = this->step_once(stmt, *output, NULL);
	IF_DEBUG("get_row_ordered output(%p) = %llu \n", *output, **(uint64_t**)output);
	sqlite3_reset(stmt);
	cache_release_value(m_statement_cache, &stmt);
	return res;
}


int Database::sql(const char* name, const char* fmt, ...) {
	sqlite3_stmt* stmt;
	char* key = strdup(name);
	cache_get_and_retain(m_statement_cache, key, (void**)&stmt);
	if (!stmt) {
		va_list args;
		va_start(args, fmt);
		char* query = sqlite3_vmprintf(fmt, args);
		int res = sqlite3_prepare_v2(m_db, query, strlen(query), &stmt, NULL);
		va_end(args);
		if (res != SQLITE_OK) {
			fprintf(stderr, "Error: unable to prepare statement for query: %s\nError: %s\n",
					query, sqlite3_errmsg(m_db));
			free(key);
			return res;
		}
		cache_set_and_retain(m_statement_cache, key, stmt, sizeof(stmt)); \
		free(key);
	}
	return this->execute(stmt);
}

int Database::count(const char* name, void** output, Table* table, uint32_t count, ...) {
	__get_stmt(table->count(m_db, count, args));
	int res = SQLITE_OK;
	uint32_t param = 1;
	__bind_va_columns(count);
	*output = malloc(sizeof(uint64_t));
	res = this->step_once(stmt, *(uint8_t**)output, NULL);
	sqlite3_reset(stmt);
	cache_release_value(m_statement_cache, &stmt);	
	return res;
}

/**
 * Given a table and an arg list in the same order as Table::add_column() calls,
 * binds and executes a sql update. The Table is responsible for preparing the
 * statement in Table::update()
 *
 * All integer args must be cast to uint64_t
 * All blob columns must provide 2 args in the list. The first arg is a uint8_t* of data
 * and then the uint32_t value for size of the data. 
 *
 */
int Database::update(Table* table, uint64_t pkvalue, ...) {
	int res = SQLITE_OK;
	
	// get the prepared statement
	sqlite3_stmt* stmt = table->update(m_db);
	if (!stmt) {
		fprintf(stderr, "Error: %s table gave a NULL statement when trying to update.\n", table->name());
		return 1;
	}
	
	uint32_t param = 1; // counter to track placeholders in sql statement
	__bind_all_columns(pkvalue);
	
	// bind the primary key in the WHERE clause
	if (res==SQLITE_OK) res = sqlite3_bind_int64(stmt, param++, pkvalue);
	if (res==SQLITE_OK) res = this->execute(stmt);
	return res;
}

/**
 * Given a table, value_column, and value to set, plus a va_list of Column,value for WHERE clause
 *  set the value_column to value when WHERE is true
 */
int Database::update_value(const char* name, Table* table, Column* value_column, void** value, 
							uint32_t count, ...) {
	__get_stmt(table->update_value(m_db, value_column, count, args));
	int res = SQLITE_OK;
	uint32_t param = 1;
	switch(value_column->type()) {
		case TYPE_INTEGER:
			res = sqlite3_bind_int64(stmt, param++, (uint64_t)*value);
			break;
		case TYPE_TEXT:
			res = sqlite3_bind_text(stmt, param++, (char*)*value, -1, SQLITE_STATIC);
			break;
		// XXX: support blob columns here
		case TYPE_BLOB:
			fprintf(stderr, "Error: Database::update_value() not implemented for BLOB columns.\n");
			assert(false);
	}
	if (res != SQLITE_OK) {
		fprintf(stderr, "Error: update_value failed to bind value with value_column type %d in "
				"table %s. \n",
				value_column->type(), table->name());
		return res;
	}
	__bind_va_columns(count);
	res = sqlite3_step(stmt);
	sqlite3_reset(stmt);
    cache_release_value(m_statement_cache, &stmt);
	return (res == SQLITE_DONE ? SQLITE_OK : res);
}

/**
 * Given a table and an arg list in the same order as Table::add_column() calls,
 * minus any primary key columns, 
 * binds and executes a sql insertion. The Table is responsible for preparing the
 * statement in Table::insert()
 *
 * All integer args must be cast to uint64_t
 * All blob columns must provide 2 args in the list. The first arg is a uint8_t* of data
 * and then the uint32_t value for size of the data. 
 *
 */
int Database::insert(Table* table, ...) {
	int res = SQLITE_OK;
	// get the prepared statement
	sqlite3_stmt* stmt = table->insert(m_db);
	if (!stmt) {
		fprintf(stderr, "Error: %s table gave a NULL statement when trying to insert.\n", table->name());
		return 1;
	}
	uint32_t param = 1; // counter to track placeholders in sql statement
	__bind_all_columns(table);
	if (res == SQLITE_OK) res = this->execute(stmt);
	return res;
}


int Database::del(Table* table, uint64_t serial) {
	int res = SQLITE_OK;
	sqlite3_stmt* stmt = table->del(m_db);
	if (!stmt) {
		fprintf(stderr, "Error: %s table gave a NULL statement when trying to delete.\n", table->name());
		return res;
	}
	if (res == SQLITE_OK) res = sqlite3_bind_int64(stmt, 1, serial);
	if (res == SQLITE_OK) res = this->execute(stmt);
	return res;
}

int Database::del(const char* name, Table* table, uint32_t count, ...) {
	__get_stmt(table->del(m_db, count, args));
	int res = SQLITE_OK;
	uint32_t param = 1;
	__bind_va_columns(count);
	if (res == SQLITE_OK) res = this->execute(stmt);
	return res;
	
}

#undef __bind_all_columns
#undef __get_stmt

uint64_t Database::last_insert_id() {
	return (uint64_t)sqlite3_last_insert_rowid(m_db);
}


int Database::begin_transaction() {
	return this->execute(m_begin_transaction);
}

int Database::rollback_transaction() {
	return this->execute(m_rollback_transaction);
}

int Database::commit_transaction() {
	return this->execute(m_commit_transaction);
}

