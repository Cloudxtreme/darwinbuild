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

#ifndef _DB_H
#define _DB_H

#include <stdint.h>
#include <assert.h>
#include <uuid/uuid.h>
#include <time.h>
#include "Database.h"
#include "Table.h"
#include "Archive.h"
#include "Digest.h"
#include "File.h"


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
	
	uint64_t count_files(Archive* archive, const char* path);
	
	// Archives table modifications
	uint64_t insert_archive(uuid_t uuid, uint32_t info, const char* name, time_t date);
	bool     delete_archive(Archive* archive);
	bool     delete_archive(uint64_t serial);

	// Files table modifications
	bool     update_file(Archive* archive, const char* path, uint32_t info, mode_t mode, 
						 uid_t uid, gid_t gid, Digest* digest);
	uint64_t insert_file(uint32_t info, mode_t mode, uid_t uid, gid_t gid, 
						 Digest* digest, Archive* archive, const char* path);
	bool     delete_file(uint64_t serial);
	bool     delete_file(File* file);
	bool     delete_files(Archive* archive);
	

protected:
	
	Table*        m_archives_table;
	Table*        m_files_table;
	
};

#endif

