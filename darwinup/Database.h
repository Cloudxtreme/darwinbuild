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

#ifndef _DATABASE_H
#define _DATABASE_H


#include <stdint.h>
#include <sqlite3.h>
#include "Table.h"

/**
 * 
 * Generic sqlite abstraction
 *
 */
struct Database {
	Database();
	Database(const char* path);
	virtual ~Database();

	virtual void init_schema();
	const char* path();
	const char* error();
	bool connect();
	bool connect(const char* path);
	
	const char* get_value(const char* table, const char* column, const char* where);
	const char* get_row(const char* table, const char* where);
	const char* get_column(const char* table, const char* column, const char* where);
	const char* get_all(const char* table, const char* where);
	
	uint32_t count(const char* table, const char* where);
	
	bool update(const char* table, const char* set, const char* where, uint32_t &count);
	bool del(const char* table, const char* where, uint32_t &count);
	bool insert(const char* table, const char* columns, const char* values);
	
	bool begin_transaction();
	bool rollback_transaction();
	bool commit_transaction();

	bool add_table(Table*);
	
protected:
	
	bool empty();
	bool create_tables();
	int sql(const char* fmt, ...);
	
	char*         m_path;
	sqlite3*      m_db;

	Table**       m_tables;
	uint32_t      m_table_count;
	uint32_t      m_table_max;

	char*         m_error;
	size_t        m_error_size;
	
};



/**
 *
 * Darwinup database abstraction. This class is responsible
 *  for generating the Table and Column objects that make
 *  up the darwinup database schema, but the parent handles
 *  deallocation. 
 *
 */
struct DarwinupDatabase : Database {
	DarwinupDatabase();
	DarwinupDatabase(const char* path);
	virtual ~DarwinupDatabase();
	void init_schema();
};

#endif
