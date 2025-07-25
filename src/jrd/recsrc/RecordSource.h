/*
 *  The contents of this file are subject to the Initial
 *  Developer's Public License Version 1.0 (the "License");
 *  you may not use this file except in compliance with the
 *  License. You may obtain a copy of the License at
 *  http://www.ibphoenix.com/main.nfs?a=ibphoenix&page=ibp_idpl.
 *
 *  Software distributed under the License is distributed AS IS,
 *  WITHOUT WARRANTY OF ANY KIND, either express or implied.
 *  See the License for the specific language governing rights
 *  and limitations under the License.
 *
 *  The Original Code was created by Dmitry Yemanov
 *  for the Firebird Open Source RDBMS project.
 *
 *  Copyright (c) 2009 Dmitry Yemanov <dimitr@firebirdsql.org>
 *  and all contributors signed below.
 *
 *  All Rights Reserved.
 *  Contributor(s): ______________________________________.
 */

#ifndef JRD_RECORD_SOURCE_H
#define JRD_RECORD_SOURCE_H

#include <optional>
#include "../common/classes/array.h"
#include "../common/classes/objects_array.h"
#include "../common/classes/NestConst.h"
#include "../jrd/RecordSourceNodes.h"
#include "../jrd/req.h"
#include "../jrd/RecordBuffer.h"
#include "firebird/impl/inf_pub.h"
#include "../jrd/evl_proto.h"
#include "../jrd/vio_proto.h"

namespace Jrd
{
	class thread_db;
	class Request;
	class jrd_prc;
	class AggNode;
	class BoolExprNode;
	class DeclareLocalTableNode;
	class Sort;
	class CompilerScratch;
	class BtrPageGCLock;
	struct index_desc;
	struct record_param;
	struct temporary_key;
	struct win;
	class BaseBufferedStream;
	class BufferedStream;
	class PlanEntry;

	enum class JoinType { INNER, OUTER, SEMI, ANTI };

	// Common base for record sources, sub-queries and cursors.
	class AccessPath
	{
	public:
		explicit AccessPath(CompilerScratch* csb);
		virtual ~AccessPath() = default;

	public:
		ULONG getCursorId() const
		{
			return m_cursorId;
		}

		ULONG getRecSourceId() const
		{
			return m_recSourceId;
		}

		double getCardinality() const
		{
			return m_cardinality;
		}

		void getPlan(thread_db* tdbb, PlanEntry& planEntry, unsigned level, bool recurse) const;

		virtual void getLegacyPlan(thread_db* tdbb, Firebird::string& plan, unsigned level) const = 0;

	protected:
		virtual void internalGetPlan(thread_db* tdbb, PlanEntry& planEntry,
			unsigned level, bool recurse) const = 0;

	protected:
		double m_cardinality = 0.0;

	private:
		const ULONG m_cursorId;
		const ULONG m_recSourceId;
	};

	class PlanEntry final : public Firebird::AutoStorage
	{
	public:
		struct Line
		{
			explicit Line(MemoryPool& pool)
				: text(pool)
			{}

			unsigned level = 0;
			Firebird::string text;
		};

	public:
		explicit PlanEntry(MemoryPool& pool)
			: AutoStorage(pool)
		{
		}

		PlanEntry() = default;

	public:
		void getDescriptionAsString(Firebird::string& str, bool initialIndentation = false) const;
		void asFlatList(Firebird::Array<Firebird::NonPooledPair<const PlanEntry*, const PlanEntry*>>& list) const;
		void asString(Firebird::string& str) const;

	public:
		Firebird::string className{getPool()};
		Firebird::ObjectsArray<Line> lines{getPool()};
		Firebird::ObjectsArray<PlanEntry> children{getPool()};
		std::optional<ObjectType> objectType;
		QualifiedName objectName;
		Firebird::string alias{getPool()};
		const AccessPath* accessPath = nullptr;
		ULONG recordLength = 0;
		ULONG keyLength = 0;
		unsigned level = 0;
	};

	// Abstract base class for record sources.
	class RecordSource : public AccessPath
	{
	public:
		virtual void close(thread_db* tdbb) const = 0;

		virtual bool refetchRecord(thread_db* tdbb) const = 0;
		virtual WriteLockResult lockRecord(thread_db* tdbb) const = 0;

		virtual void markRecursive() = 0;
		virtual void invalidateRecords(Request* request) const = 0;

		virtual void findUsedStreams(StreamList& streams, bool expandAll = false) const = 0;
		virtual bool isDependent(const StreamList& streams) const = 0;
		virtual void nullRecords(thread_db* tdbb) const = 0;

		virtual void setAnyBoolean(BoolExprNode* /*anyBoolean*/, bool /*ansiAny*/, bool /*ansiNot*/)
		{
			fb_assert(false);
		}

		static bool rejectDuplicate(const UCHAR* /*data1*/, const UCHAR* /*data2*/, void* /*userArg*/)
		{
			return true;
		}

		void open(thread_db* tdbb) const;

		bool getRecord(thread_db* tdbb) const;

	protected:
		// Generic impure block
		struct Impure
		{
			ULONG irsb_flags;
		};

		static const ULONG irsb_open = 1;
		static const ULONG irsb_first = 2;
		static const ULONG irsb_joined = 4;
		static const ULONG irsb_mustread = 8;
		static const ULONG irsb_singular_processed = 16;

		RecordSource(CompilerScratch* csb);

		static Firebird::string printName(thread_db* tdbb, const Firebird::string& name,
										  const Firebird::string& alias = {});

		static void printInversion(thread_db* tdbb, const InversionNode* inversion,
								   Firebird::ObjectsArray<PlanEntry::Line>& planLines,
								   bool detailed, unsigned level, bool navigation);

		static void printLegacyInversion(thread_db* tdbb, const InversionNode* inversion, Firebird::string& plan);

		void printOptInfo(Firebird::ObjectsArray<PlanEntry::Line>& plan) const;

		static void saveRecord(thread_db* tdbb, record_param* rpb);
		static void restoreRecord(thread_db* tdbb, record_param* rpb);

		virtual void internalOpen(thread_db* tdbb) const = 0;
		virtual bool internalGetRecord(thread_db* tdbb) const = 0;

		ULONG m_impure = 0;
		bool m_recursive = false;
	};


	// Helper class implementing some common methods

	class RecordStream : public RecordSource
	{
	public:
		RecordStream(CompilerScratch* csb, StreamType stream, const Format* format = NULL);

		bool refetchRecord(thread_db* tdbb) const override;
		WriteLockResult lockRecord(thread_db* tdbb) const override;

		void markRecursive() override;
		void invalidateRecords(Request* request) const override;

		void findUsedStreams(StreamList& streams, bool expandAll = false) const override;
		void nullRecords(thread_db* tdbb) const override;

		bool isDependent(const StreamList& /*streams*/) const override
		{
			return false;
		}

	protected:
		const StreamType m_stream;
		const Format* const m_format;
	};


	// Primary (table scan) access methods

	class FullTableScan final : public RecordStream
	{
		struct Impure : public RecordSource::Impure
		{
			RecordNumber irsb_lower;
			RecordNumber irsb_upper;
		};

	public:
		FullTableScan(CompilerScratch* csb, const Firebird::string& alias,
					  StreamType stream, jrd_rel* relation,
					  const Firebird::Array<DbKeyRangeNode*>& dbkeyRanges);

		void close(thread_db* tdbb) const override;

		void getLegacyPlan(thread_db* tdbb, Firebird::string& plan, unsigned level) const override;

	protected:
		void internalGetPlan(thread_db* tdbb, PlanEntry& planEntry, unsigned level, bool recurse) const override;
		void internalOpen(thread_db* tdbb) const override;
		bool internalGetRecord(thread_db* tdbb) const override;

	private:
		const Firebird::string m_alias;
		jrd_rel* const m_relation;
		Firebird::Array<DbKeyRangeNode*> m_dbkeyRanges;
	};

	class BitmapTableScan final : public RecordStream
	{
		struct Impure : public RecordSource::Impure
		{
			RecordBitmap** irsb_bitmap;
		};

	public:
		BitmapTableScan(CompilerScratch* csb, const Firebird::string& alias,
						StreamType stream, jrd_rel* relation,
						InversionNode* inversion, double selectivity);

		void close(thread_db* tdbb) const override;

		void getLegacyPlan(thread_db* tdbb, Firebird::string& plan, unsigned level) const override;

	protected:
		void internalGetPlan(thread_db* tdbb, PlanEntry& planEntry, unsigned level, bool recurse) const override;
		void internalOpen(thread_db* tdbb) const override;
		bool internalGetRecord(thread_db* tdbb) const override;

	private:
		const Firebird::string m_alias;
		jrd_rel* const m_relation;
		NestConst<InversionNode> const m_inversion;
	};

	class IndexTableScan final : public RecordStream
	{
		struct Impure : public RecordSource::Impure
		{
			RecordNumber irsb_nav_number;				// last record number
			ULONG irsb_nav_page;						// index page number
			SLONG irsb_nav_incarnation;					// buffer/page incarnation counter
			RecordBitmap** irsb_nav_bitmap;				// bitmap for inversion tree
			RecordBitmap* irsb_nav_records_visited;		// bitmap of records already retrieved
			BtrPageGCLock* irsb_nav_btr_gc_lock;		// lock to prevent removal of currently walked index page
			temporary_key* irsb_nav_lower;				// lower (possible multiple) key
			temporary_key* irsb_nav_upper;				// upper (possible multiple) key
			temporary_key* irsb_nav_current_lower;		// current lower key
			temporary_key* irsb_nav_current_upper;		// current upper key
			IndexScanListIterator* irsb_iterator;		// key list iterator
			USHORT irsb_nav_offset;						// page offset of current index node
			USHORT irsb_nav_upper_length;				// length of upper key value
			USHORT irsb_nav_length;						// length of expanded key
			UCHAR irsb_nav_data[1];						// expanded key, upper bound, and index desc
		};

	public:
		IndexTableScan(CompilerScratch* csb, const Firebird::string& alias,
					   StreamType stream, jrd_rel* relation,
					   InversionNode* index, USHORT keyLength,
					   double selectivity);

		void close(thread_db* tdbb) const override;

		void getLegacyPlan(thread_db* tdbb, Firebird::string& plan, unsigned level) const override;

		void setInversion(InversionNode* inversion, BoolExprNode* condition)
		{
			fb_assert(!m_inversion && !m_condition);
			m_inversion = inversion;
			m_condition = condition;
		}

	protected:
		void internalGetPlan(thread_db* tdbb, PlanEntry& planEntry, unsigned level, bool recurse) const override;
		void internalOpen(thread_db* tdbb) const override;
		bool internalGetRecord(thread_db* tdbb) const override;

	private:
		int compareKeys(const index_desc*, const UCHAR*, USHORT, const temporary_key*, USHORT) const;
		bool findSavedNode(thread_db* tdbb, Impure* impure, win* window, UCHAR**) const;
		void advanceStream(thread_db* tdbb, Impure* impure, win* window) const;
		UCHAR* getPosition(thread_db* tdbb, Impure* impure, win* window) const;
		UCHAR* getStreamPosition(thread_db* tdbb, Impure* impure, win* window) const;
		UCHAR* openStream(thread_db* tdbb, Impure* impure, win* window) const;
		void setPage(thread_db* tdbb, Impure* impure, win* window) const;
		void setPosition(thread_db* tdbb, Impure* impure, record_param*,
						 win* window, const UCHAR*, const temporary_key&) const;
		bool setupBitmaps(thread_db* tdbb, Impure* impure) const;

		const Firebird::string m_alias;
		jrd_rel* const m_relation;
		NestConst<InversionNode> const m_index;
		NestConst<InversionNode> m_inversion;
		NestConst<BoolExprNode> m_condition;
		const FB_SIZE_T m_length;
		FB_SIZE_T m_offset;
	};

	class ExternalTableScan final : public RecordStream
	{
		struct Impure : public RecordSource::Impure
		{
			FB_UINT64 irsb_position;
		};

	public:
		ExternalTableScan(CompilerScratch* csb, const Firebird::string& alias,
						  StreamType stream, jrd_rel* relation);

		void close(thread_db* tdbb) const override;

		bool refetchRecord(thread_db* tdbb) const override;
		WriteLockResult lockRecord(thread_db* tdbb) const override;

		void getLegacyPlan(thread_db* tdbb, Firebird::string& plan, unsigned level) const override;

	protected:
		void internalOpen(thread_db* tdbb) const override;
		bool internalGetRecord(thread_db* tdbb) const override;
		void internalGetPlan(thread_db* tdbb, PlanEntry& planEntry, unsigned level, bool recurse) const override;

	private:
		jrd_rel* const m_relation;
		const Firebird::string m_alias;
	};

	class VirtualTableScan : public RecordStream
	{
	public:
		VirtualTableScan(CompilerScratch* csb, const Firebird::string& alias,
						 StreamType stream, jrd_rel* relation);

		void close(thread_db* tdbb) const override;

		bool refetchRecord(thread_db* tdbb) const override;
		WriteLockResult lockRecord(thread_db* tdbb) const override;

		void getLegacyPlan(thread_db* tdbb, Firebird::string& plan, unsigned level) const override;

	protected:
		void internalGetPlan(thread_db* tdbb, PlanEntry& planEntry, unsigned level, bool recurse) const override;
		void internalOpen(thread_db* tdbb) const override;
		bool internalGetRecord(thread_db* tdbb) const override;

		virtual const Format* getFormat(thread_db* tdbb, jrd_rel* relation) const = 0;
		virtual bool retrieveRecord(thread_db* tdbb, jrd_rel* relation,
									FB_UINT64 position, Record* record) const = 0;

	private:
		jrd_rel* const m_relation;
		const Firebird::string m_alias;
	};

	class ProcedureScan final : public RecordStream
	{
		struct Impure : public RecordSource::Impure
		{
			Request* irsb_req_handle;
			UCHAR* irsb_message;
		};

	public:
		ProcedureScan(CompilerScratch* csb, const Firebird::string& alias, StreamType stream,
					  const jrd_prc* procedure, const ValueListNode* sourceList,
					  const ValueListNode* targetList, MessageNode* message);

		void close(thread_db* tdbb) const override;

		bool refetchRecord(thread_db* tdbb) const override;
		WriteLockResult lockRecord(thread_db* tdbb) const override;
		bool isDependent(const StreamList& streams) const override;

		void getLegacyPlan(thread_db* tdbb, Firebird::string& plan, unsigned level) const override;

	protected:
		void internalGetPlan(thread_db* tdbb, PlanEntry& planEntry, unsigned level, bool recurse) const override;
		void internalOpen(thread_db* tdbb) const override;
		bool internalGetRecord(thread_db* tdbb) const override;

	private:
		void assignParams(thread_db* tdbb, const dsc* from_desc, const dsc* flag_desc,
						  const UCHAR* msg, const dsc* to_desc, SSHORT to_id, Record* record) const;

		const Firebird::string m_alias;
		const jrd_prc* const m_procedure;
		const ValueListNode* m_sourceList;
		const ValueListNode* m_targetList;
		NestConst<MessageNode> const m_message;
	};


	// Filtering (one -> one) access methods

	class SingularStream : public RecordSource
	{
	public:
		SingularStream(CompilerScratch* csb, RecordSource* next);

		void close(thread_db* tdbb) const override;

		bool refetchRecord(thread_db* tdbb) const override;
		WriteLockResult lockRecord(thread_db* tdbb) const override;

		void getLegacyPlan(thread_db* tdbb, Firebird::string& plan, unsigned level) const override;

		void markRecursive() override;
		void invalidateRecords(Request* request) const override;

		void findUsedStreams(StreamList& streams, bool expandAll = false) const override;
		bool isDependent(const StreamList& streams) const override;
		void nullRecords(thread_db* tdbb) const override;

	protected:
		void internalGetPlan(thread_db* tdbb, PlanEntry& planEntry, unsigned level, bool recurse) const override;
		void internalOpen(thread_db* tdbb) const override;
		bool internalGetRecord(thread_db* tdbb) const override;

	private:
		void process(thread_db* tdbb) const;

		NestConst<RecordSource> m_next;
		StreamList m_streams;
	};

	class LockedStream : public RecordSource
	{
	public:
		LockedStream(CompilerScratch* csb, RecordSource* next);

		void close(thread_db* tdbb) const override;

		bool refetchRecord(thread_db* tdbb) const override;
		WriteLockResult lockRecord(thread_db* tdbb) const override;

		void getLegacyPlan(thread_db* tdbb, Firebird::string& plan, unsigned level) const override;

		void markRecursive() override;
		void invalidateRecords(Request* request) const override;

		void findUsedStreams(StreamList& streams, bool expandAll = false) const override;
		bool isDependent(const StreamList& streams) const override;
		void nullRecords(thread_db* tdbb) const override;

	protected:
		void internalGetPlan(thread_db* tdbb, PlanEntry& planEntry, unsigned level, bool recurse) const override;
		void internalOpen(thread_db* tdbb) const override;
		bool internalGetRecord(thread_db* tdbb) const override;

	private:
		NestConst<RecordSource> m_next;
	};

	class FirstRowsStream : public RecordSource
	{
		struct Impure : public RecordSource::Impure
		{
			SINT64 irsb_count;
		};

	public:
		FirstRowsStream(CompilerScratch* csb, RecordSource* next, ValueExprNode* value);

		void close(thread_db* tdbb) const override;

		bool refetchRecord(thread_db* tdbb) const override;
		WriteLockResult lockRecord(thread_db* tdbb) const override;

		void getLegacyPlan(thread_db* tdbb, Firebird::string& plan, unsigned level) const override;

		void markRecursive() override;
		void invalidateRecords(Request* request) const override;

		void findUsedStreams(StreamList& streams, bool expandAll = false) const override;
		bool isDependent(const StreamList& streams) const override;
		void nullRecords(thread_db* tdbb) const override;

		void setAnyBoolean(BoolExprNode* anyBoolean, bool ansiAny, bool ansiNot) override
		{
			m_next->setAnyBoolean(anyBoolean, ansiAny, ansiNot);
		}

	protected:
		void internalGetPlan(thread_db* tdbb, PlanEntry& planEntry, unsigned level, bool recurse) const override;
		void internalOpen(thread_db* tdbb) const override;
		bool internalGetRecord(thread_db* tdbb) const override;

	private:
		NestConst<RecordSource> m_next;
		NestConst<ValueExprNode> const m_value;
	};

	class SkipRowsStream : public RecordSource
	{
		struct Impure : public RecordSource::Impure
		{
			SINT64 irsb_count;
		};

	public:
		SkipRowsStream(CompilerScratch* csb, RecordSource* next, ValueExprNode* value);

		void close(thread_db* tdbb) const override;

		bool refetchRecord(thread_db* tdbb) const override;
		WriteLockResult lockRecord(thread_db* tdbb) const override;

		void getLegacyPlan(thread_db* tdbb, Firebird::string& plan, unsigned level) const override;

		void markRecursive() override;
		void invalidateRecords(Request* request) const override;

		void findUsedStreams(StreamList& streams, bool expandAll = false) const override;
		bool isDependent(const StreamList& streams) const override;
		void nullRecords(thread_db* tdbb) const override;

		void setAnyBoolean(BoolExprNode* anyBoolean, bool ansiAny, bool ansiNot) override
		{
			m_next->setAnyBoolean(anyBoolean, ansiAny, ansiNot);
		}

	protected:
		void internalGetPlan(thread_db* tdbb, PlanEntry& planEntry, unsigned level, bool recurse) const override;
		void internalOpen(thread_db* tdbb) const override;
		bool internalGetRecord(thread_db* tdbb) const override;

	private:
		NestConst<RecordSource> m_next;
		NestConst<ValueExprNode> const m_value;
	};

	class FilteredStream : public RecordSource
	{
	public:
		FilteredStream(CompilerScratch* csb, RecordSource* next,
					   BoolExprNode* boolean, double selectivity = 0);

		void close(thread_db* tdbb) const override;

		bool refetchRecord(thread_db* tdbb) const override;
		WriteLockResult lockRecord(thread_db* tdbb) const override;

		void getLegacyPlan(thread_db* tdbb, Firebird::string& plan, unsigned level) const override;

		void markRecursive() override;
		void invalidateRecords(Request* request) const override;

		void findUsedStreams(StreamList& streams, bool expandAll = false) const override;
		bool isDependent(const StreamList& streams) const override;
		void nullRecords(thread_db* tdbb) const override;

		void setAnyBoolean(BoolExprNode* anyBoolean, bool ansiAny, bool ansiNot) override
		{
			fb_assert(!m_anyBoolean);
			m_anyBoolean = anyBoolean;

			m_ansiAny = ansiAny;
			m_ansiAll = !ansiAny;
			m_ansiNot = ansiNot;
		}

	protected:
		void internalGetPlan(thread_db* tdbb, PlanEntry& planEntry, unsigned level, bool recurse) const override;
		void internalOpen(thread_db* tdbb) const override;
		bool internalGetRecord(thread_db* tdbb) const override;

		bool m_invariant = false;

	private:
		bool evaluateBoolean(thread_db* tdbb) const;

		NestConst<RecordSource> m_next;
		NestConst<BoolExprNode> const m_boolean;
		NestConst<BoolExprNode> m_anyBoolean;
		bool m_ansiAny;
		bool m_ansiAll;
		bool m_ansiNot;
	};

	class PreFilteredStream : public FilteredStream
	{
	public:
		PreFilteredStream(CompilerScratch* csb, RecordSource* next,
						  BoolExprNode* boolean)
			: FilteredStream(csb, next, boolean)
		{
			m_invariant = true;
		}
	};

	class SortedStream : public RecordSource
	{
		struct Impure : public RecordSource::Impure
		{
			Sort* irsb_sort;
		};

	public:
		static const USHORT FLAG_PROJECT	= 0x1;	// sort is really a project
		static const USHORT FLAG_UNIQUE		= 0x2;	// sorts using unique key - for distinct and group by
		static const USHORT FLAG_KEY_VARY	= 0x4;	// sort key contains varying length string(s)
		static const USHORT FLAG_REFETCH	= 0x8;	// refetch data after sorting

		// Special values for SortMap::Item::fieldId.
		static const SSHORT ID_DBKEY		= -1;	// dbkey value
		static const SSHORT ID_DBKEY_VALID	= -2;	// dbkey valid flag
		static const SSHORT ID_TRANS 		= -3;	// transaction id of record

		// Sort map block
		class SortMap : public Firebird::PermanentStorage
		{
		public:
			struct Item
			{
				void reset(NestConst<ValueExprNode> _node, ULONG _flagOffset = 0)
				{
					desc.clear();
					stream = fieldId = 0;
					node = _node;
					flagOffset = _flagOffset;
				}

				void reset(StreamType _stream, SSHORT _fieldId, ULONG _flagOffset = 0)
				{
					desc.clear();
					node = nullptr;
					stream = _stream;
					fieldId = _fieldId;
					flagOffset = _flagOffset;
				}

				StreamType stream;			// stream for field id
				dsc desc;					// relative descriptor
				ULONG flagOffset;			// offset of missing flag
				SSHORT fieldId;				// id for field (or ID constants)
				NestConst<ValueExprNode> node;	// expression node
			};

			explicit SortMap(MemoryPool& p)
				: PermanentStorage(p),
				  length(0),
				  keyLength(0),
				  flags(0),
				  keyItems(p),
				  items(p)
			{
			}

			ULONG length;			// sort record length
			ULONG keyLength;		// key length
			USHORT flags;			// misc sort flags
			Firebird::Array<sort_key_def> keyItems;	// address of key descriptors
			Firebird::Array<Item> items;
		};

		SortedStream(CompilerScratch* csb, RecordSource* next, SortMap* map);

		void close(thread_db* tdbb) const override;

		bool refetchRecord(thread_db* tdbb) const override;
		WriteLockResult lockRecord(thread_db* tdbb) const override;

		void getLegacyPlan(thread_db* tdbb, Firebird::string& plan, unsigned level) const override;

		void markRecursive() override;
		void invalidateRecords(Request* request) const override;

		void findUsedStreams(StreamList& streams, bool expandAll = false) const override;
		bool isDependent(const StreamList& streams) const override;
		void nullRecords(thread_db* tdbb) const override;

		void setAnyBoolean(BoolExprNode* anyBoolean, bool ansiAny, bool ansiNot) override
		{
			m_next->setAnyBoolean(anyBoolean, ansiAny, ansiNot);
		}

		ULONG getLength() const
		{
			return m_map->length;
		}

		ULONG getKeyLength() const
		{
			return m_map->keyLength;
		}

		bool compareKeys(const UCHAR* p, const UCHAR* q) const;

		UCHAR* getData(thread_db* tdbb) const;
		void mapData(thread_db* tdbb, Request* request, UCHAR* data) const;

		bool isKey(const dsc* desc) const
		{
			return ((ULONG)(IPTR) desc->dsc_address < m_map->keyLength);
		}

		static bool hasVolatileKey(const dsc* desc)
		{
			// International type text has a computed key.
			// Different decimal float values sometimes have same keys.
			// The same for date/time with time zones.
			return (IS_INTL_DATA(desc) || desc->isDecFloat() || desc->isDateTimeTz());
		}

	protected:
		void internalGetPlan(thread_db* tdbb, PlanEntry& planEntry, unsigned level, bool recurse) const override;
		void internalOpen(thread_db* tdbb) const override;
		bool internalGetRecord(thread_db* tdbb) const override;

	private:
		Sort* init(thread_db* tdbb) const;

		NestConst<RecordSource> m_next;
		const SortMap* const m_map;
	};

	// Make moves in a window without going out of partition boundaries.
	class SlidingWindow
	{
	public:
		SlidingWindow(thread_db* aTdbb, const BaseBufferedStream* aStream, Request* request,
			FB_UINT64 aPartitionStart, FB_UINT64 aPartitionEnd,
			FB_UINT64 aFrameStart, FB_UINT64 aFrameEnd);
		~SlidingWindow();

		FB_UINT64 getPartitionStart() const
		{
			return partitionStart;
		}

		FB_UINT64 getPartitionEnd() const
		{
			return partitionEnd;
		}

		FB_UINT64 getPartitionSize() const
		{
			return partitionEnd - partitionStart + 1;
		}

		FB_UINT64 getFrameStart() const
		{
			return frameStart;
		}

		FB_UINT64 getFrameEnd() const
		{
			return frameEnd;
		}

		FB_UINT64 getFrameSize() const
		{
			return frameEnd - frameStart + 1;
		}

		FB_UINT64 getRecordPosition() const
		{
			return savedPosition;
		}

		SINT64 getInFrameOffset() const
		{
			return savedPosition - frameStart;
		}

		void restore()
		{
			if (!moved)
				return;

			// Position the stream where we received it.
			moveWithinPartition(0);
		}

		bool moveWithinPartition(SINT64 delta);
		bool moveWithinFrame(SINT64 delta);

	private:
		thread_db* tdbb;
		const BaseBufferedStream* const stream;
		FB_UINT64 partitionStart;
		FB_UINT64 partitionEnd;
		FB_UINT64 frameStart;
		FB_UINT64 frameEnd;
		FB_UINT64 savedPosition;
		bool moved = false;
	};

	template <typename ThisType, typename NextType>
	class BaseAggWinStream : public RecordStream
	{
	protected:
		enum State
		{
			STATE_EOF,			// We processed everything now process EOF
			STATE_FETCHED,		// Values are pending from a prior fetch
			STATE_GROUPING		// Entering EVL group before fetching the first record
		};

		struct Impure : public RecordSource::Impure
		{
			impure_value* groupValues;
			State state;
		};

		struct DummyAdjustFunctor
		{
			void operator ()(thread_db* /*tdbb*/, impure_value* /*target*/)
			{
			}
		};

	public:
		BaseAggWinStream(thread_db* tdbb, CompilerScratch* csb, StreamType stream,
			const NestValueArray* group, MapNode* groupMap, bool oneRowWhenEmpty, NextType* next);

	public:
		void close(thread_db* tdbb) const override;

		bool refetchRecord(thread_db* tdbb) const override;
		WriteLockResult lockRecord(thread_db* tdbb) const override;

		void markRecursive() override;
		void invalidateRecords(Request* request) const override;

		void findUsedStreams(StreamList& streams, bool expandAll = false) const override;
		bool isDependent(const StreamList& streams) const override;

	protected:
		void internalOpen(thread_db* tdbb) const override;

		Impure* getImpure(Request* request) const
		{
			return request->getImpure<typename ThisType::Impure>(m_impure);
		}

		bool evaluateGroup(thread_db* tdbb) const;

		void aggInit(thread_db* tdbb, Request* request, const MapNode* map) const;
		bool aggPass(thread_db* tdbb, Request* request,
			const NestValueArray& sourceList, const NestValueArray& targetList) const;
		void aggExecute(thread_db* tdbb, Request* request,
			const NestValueArray& sourceList, const NestValueArray& targetList) const;
		void aggFinish(thread_db* tdbb, Request* request, const MapNode* map) const;

		// Cache the values of a group/order in the impure.
		template <typename AdjustFunctor>
		void cacheValues(thread_db* tdbb, Request* request,
			const NestValueArray* group, impure_value* values,
			AdjustFunctor adjustFunctor) const
		{
			if (!group)
				return;

			for (const NestConst<ValueExprNode>* ptrValue = group->begin(), *endValue = group->end();
				 ptrValue != endValue;
				 ++ptrValue)
			{
				const ValueExprNode* from = *ptrValue;
				impure_value* target = &values[ptrValue - group->begin()];

				dsc* desc = EVL_expr(tdbb, request, from);

				if (request->req_flags & req_null)
					target->vlu_desc.dsc_address = NULL;
				else
				{
					EVL_make_value(tdbb, desc, target);
					adjustFunctor(tdbb, target);
				}
			}
		}

		int lookForChange(thread_db* tdbb, Request* request,
			const NestValueArray* group, const SortNode* sort, impure_value* values) const;

	private:
		bool getNextRecord(thread_db* tdbb, Request* request) const;

	protected:
		NestConst<NextType> m_next;
		const NestValueArray* const m_group;
		NestConst<MapNode> m_groupMap;
		bool m_oneRowWhenEmpty;
	};

	class AggregatedStream final : public BaseAggWinStream<AggregatedStream, RecordSource>
	{
	public:
		AggregatedStream(thread_db* tdbb, CompilerScratch* csb, StreamType stream,
			const NestValueArray* group, MapNode* map, RecordSource* next);

	public:
		void getLegacyPlan(thread_db* tdbb, Firebird::string& plan, unsigned level) const override;

	protected:
		void internalGetPlan(thread_db* tdbb, PlanEntry& planEntry, unsigned level, bool recurse) const override;
		bool internalGetRecord(thread_db* tdbb) const override;
	};

	class WindowedStream : public RecordSource
	{
	public:
		using Frame = WindowClause::Frame;
		using FrameExtent = WindowClause::FrameExtent;
		using Exclusion = WindowClause::Exclusion;

		class WindowStream final : public BaseAggWinStream<WindowStream, BaseBufferedStream>
		{
		private:
			struct AdjustFunctor
			{
				AdjustFunctor(const ArithmeticNode* aArithNode, const dsc* aOffsetDesc)
					: arithNode(aArithNode),
					  offsetDesc(aOffsetDesc)
				{
				}

				void operator ()(thread_db* tdbb, impure_value* target)
				{
					ArithmeticNode::add(tdbb, offsetDesc, &target->vlu_desc, target, arithNode->blrOp, false,
						arithNode->nodScale, arithNode->nodFlags);
				}

				const ArithmeticNode* arithNode;
				const dsc* offsetDesc;
			};

			struct Block
			{
				SINT64 startPosition;
				SINT64 endPosition;

				void invalidate()
				{
					startPosition = endPosition = MIN_SINT64;
				}

				bool isValid() const
				{
					return !(startPosition == MIN_SINT64 && endPosition == MIN_SINT64);
				}
			};

		public:
			struct Impure final : public BaseAggWinStream::Impure
			{
				impure_value* orderValues;
				SINT64 partitionPending, rangePending;
				Block partitionBlock, windowBlock;
				impure_value_ex startOffset, endOffset;
			};

		public:
			WindowStream(thread_db* tdbb, CompilerScratch* csb, StreamType stream,
				const NestValueArray* group, BaseBufferedStream* next,
				SortNode* order, MapNode* windowMap,
				FrameExtent* frameExtent,
				Exclusion exclusion);

		public:
			void close(thread_db* tdbb) const override;

			void getLegacyPlan(thread_db* tdbb, Firebird::string& plan, unsigned level) const override;

			void findUsedStreams(StreamList& streams, bool expandAll = false) const override;
			bool isDependent(const StreamList& streams) const override;
			void nullRecords(thread_db* tdbb) const override;

		protected:
			void internalGetPlan(thread_db* tdbb, PlanEntry& planEntry, unsigned level, bool recurse) const override;
			void internalOpen(thread_db* tdbb) const override;
			bool internalGetRecord(thread_db* tdbb) const override;

			Impure* getImpure(Request* request) const
			{
				return request->getImpure<Impure>(m_impure);
			}

		private:
			void getFrameValue(thread_db* tdbb, Request* request,
				const Frame* frame, impure_value_ex* impureValue) const;

			SINT64 locateFrameRange(thread_db* tdbb, Request* request, Impure* impure,
				const Frame* frame, const dsc* offsetDesc, SINT64 position) const;

		private:
			NestConst<SortNode> m_order;
			const MapNode* m_windowMap;
			NestConst<FrameExtent> m_frameExtent;
			Firebird::Array<NestConst<ArithmeticNode> > m_arithNodes;
			NestValueArray m_aggSources, m_aggTargets;
			NestValueArray m_winPassSources, m_winPassTargets;
			Exclusion m_exclusion;
			UCHAR m_invariantOffsets;	// 0x1 | 0x2 bitmask
		};

	public:
		WindowedStream(thread_db* tdbb, Optimizer* opt,
			Firebird::ObjectsArray<WindowSourceNode::Window>& windows, RecordSource* next);

		void close(thread_db* tdbb) const override;

		bool refetchRecord(thread_db* tdbb) const override;
		WriteLockResult lockRecord(thread_db* tdbb) const override;

		void getLegacyPlan(thread_db* tdbb, Firebird::string& plan, unsigned level) const override;

		void markRecursive() override;
		void invalidateRecords(Request* request) const override;

		void findUsedStreams(StreamList& streams, bool expandAll = false) const override;
		bool isDependent(const StreamList& streams) const override;
		void nullRecords(thread_db* tdbb) const override;

	protected:
		void internalGetPlan(thread_db* tdbb, PlanEntry& planEntry, unsigned level, bool recurse) const override;
		void internalOpen(thread_db* tdbb) const override;
		bool internalGetRecord(thread_db* tdbb) const override;

	private:
		NestConst<BufferedStream> m_next;
		NestConst<RecordSource> m_joinedStream;
	};

	// Abstract class for different implementations of buffered streams.
	class BaseBufferedStream : public RecordSource
	{
	public:
		BaseBufferedStream(CompilerScratch* csb)
			: RecordSource(csb)
		{}

		virtual void locate(thread_db* tdbb, FB_UINT64 position) const = 0;
		virtual FB_UINT64 getCount(thread_db* tdbb) const = 0;
		virtual FB_UINT64 getPosition(Request* request) const = 0;
	};

	class BufferedStream : public BaseBufferedStream
	{
		struct FieldMap
		{
			static const UCHAR REGULAR_FIELD = 1;
			static const UCHAR TRANSACTION_ID = 2;
			static const UCHAR DBKEY_NUMBER = 3;
			static const UCHAR DBKEY_VALID = 4;

			FieldMap() : map_stream(0), map_id(0), map_type(0)
			{}

			FieldMap(UCHAR type, StreamType stream, ULONG id)
				: map_stream(stream), map_id(id), map_type(type)
			{}

			StreamType map_stream;
			USHORT map_id;
			UCHAR map_type;
		};

		struct Impure : public RecordSource::Impure
		{
			RecordBuffer* irsb_buffer;
			FB_UINT64 irsb_position;
		};

	public:
		BufferedStream(CompilerScratch* csb, RecordSource* next);

		void close(thread_db* tdbb) const override;

		bool refetchRecord(thread_db* tdbb) const override;
		WriteLockResult lockRecord(thread_db* tdbb) const override;

		void getLegacyPlan(thread_db* tdbb, Firebird::string& plan, unsigned level) const override;

		void markRecursive() override;
		void invalidateRecords(Request* request) const override;

		void findUsedStreams(StreamList& streams, bool expandAll = false) const override;
		bool isDependent(const StreamList& streams) const override;
		void nullRecords(thread_db* tdbb) const override;

		void locate(thread_db* tdbb, FB_UINT64 position) const override;
		FB_UINT64 getCount(thread_db* tdbb) const override;

		FB_UINT64 getPosition(Request* request) const override
		{
			Impure* const impure = request->getImpure<Impure>(m_impure);
			return impure->irsb_position;
		}

	protected:
		void internalGetPlan(thread_db* tdbb, PlanEntry& planEntry, unsigned level, bool recurse) const override;
		void internalOpen(thread_db* tdbb) const override;
		bool internalGetRecord(thread_db* tdbb) const override;

	private:
		NestConst<RecordSource> m_next;
		Firebird::HalfStaticArray<FieldMap, OPT_STATIC_ITEMS> m_map;
		const Format* m_format;
	};

	// Multiplexing (many -> one) access methods

	template <class Arg>
	class Join : public RecordSource
	{
	public:
		Join(CompilerScratch* csb, FB_SIZE_T count, JoinType joinType, BoolExprNode* boolean = nullptr)
			: RecordSource(csb), m_joinType(joinType), m_boolean(boolean),
			  m_args(csb->csb_pool, count)
		{
			fb_assert(!m_boolean || m_joinType == JoinType::OUTER);
		}

		void close(thread_db* tdbb) const override
		{
			for (const auto& arg : m_args)
				arg->close(tdbb);
		}

		bool refetchRecord(thread_db* /*tdbb*/) const override
		{
			return true;
		}

		WriteLockResult lockRecord(thread_db* /*tdbb*/) const override
		{
			Firebird::status_exception::raise(Firebird::Arg::Gds(isc_record_lock_not_supp));
		}

		void markRecursive() override
		{
			for (auto& arg : m_args)
				arg->markRecursive();
		}

		void findUsedStreams(StreamList& streams, bool expandAll) const override
		{
			for (const auto& arg : m_args)
				arg->findUsedStreams(streams, expandAll);
		}

		bool isDependent(const StreamList& streams) const override
		{
			for (const auto& arg : m_args)
			{
				if (arg->isDependent(streams))
					return true;
			}

			return (m_boolean && m_boolean->containsAnyStream(streams));
		}

		void invalidateRecords(Request* request) const override
		{
			for (const auto& arg : m_args)
				arg->invalidateRecords(request);
		}

		void nullRecords(thread_db* tdbb) const override
		{
			for (const auto& arg : m_args)
				arg->nullRecords(tdbb);
		}

		void getLegacyPlan(thread_db* tdbb, Firebird::string& plan, unsigned level) const override
		{
			for (const auto& arg : m_args)
			{
				if (arg != m_args.front())
					plan += ", ";

				arg->getLegacyPlan(tdbb, plan, level);
			}
		}

	protected:
		void internalGetPlan(thread_db* tdbb, PlanEntry& planEntry, unsigned level, bool recurse) const override
		{
			if (recurse)
			{
				++level;

				for (const auto& arg : m_args)
					arg->getPlan(tdbb, planEntry.children.add(), level, recurse);
			}
		}

		const Firebird::string printType() const
		{
			switch (m_joinType)
			{
				case JoinType::INNER:
					return "(inner)";

				case JoinType::OUTER:
					return "(outer)";

				case JoinType::SEMI:
					return "(semi)";

				case JoinType::ANTI:
					return "(anti)";

				default:
					fb_assert(false);
			}

			return "";
		}

	protected:
		const JoinType m_joinType;
		const NestConst<BoolExprNode> m_boolean;
		Firebird::Array<NestConst<Arg>> m_args;
	};

	class NestedLoopJoin : public Join<RecordSource>
	{
	public:
		NestedLoopJoin(CompilerScratch* csb, JoinType joinType,
					   FB_SIZE_T count, RecordSource* const* args);
		NestedLoopJoin(CompilerScratch* csb, RecordSource* outer, RecordSource* inner,
					   BoolExprNode* boolean);

		void close(thread_db* tdbb) const override;
		void getLegacyPlan(thread_db* tdbb, Firebird::string& plan, unsigned level) const override;

	protected:
		void internalGetPlan(thread_db* tdbb, PlanEntry& planEntry, unsigned level, bool recurse) const override;
		void internalOpen(thread_db* tdbb) const override;
		bool internalGetRecord(thread_db* tdbb) const override;

	private:
		bool fetchRecord(thread_db*, FB_SIZE_T) const;
	};

	class FullOuterJoin : public Join<RecordSource>
	{
	public:
		FullOuterJoin(CompilerScratch* csb, RecordSource* arg1, RecordSource* arg2,
					  const StreamList& checkStreams);

		void close(thread_db* tdbb) const override;
		void getLegacyPlan(thread_db* tdbb, Firebird::string& plan, unsigned level) const override;

	protected:
		void internalGetPlan(thread_db* tdbb, PlanEntry& planEntry, unsigned level, bool recurse) const override;
		void internalOpen(thread_db* tdbb) const override;
		bool internalGetRecord(thread_db* tdbb) const override;

	private:
		const StreamList m_checkStreams;
	};

	class HashJoin : public Join<RecordSource>
	{
		class HashTable;

		struct SubStream
		{
			union
			{
				RecordSource* source;
				BufferedStream* buffer;
			};

			NestValueArray* keys;
			ULONG* keyLengths;
			ULONG totalKeyLength;
		};

		struct Impure : public RecordSource::Impure
		{
			HashTable* irsb_hash_table;
			UCHAR* irsb_leader_buffer;
			ULONG irsb_leader_hash;
		};

	public:
		HashJoin(thread_db* tdbb, CompilerScratch* csb, JoinType joinType,
				 FB_SIZE_T count, RecordSource* const* args, NestValueArray* const* keys,
				 double selectivity = 0);
		HashJoin(thread_db* tdbb, CompilerScratch* csb,
				 BoolExprNode* boolean,
				 RecordSource* const* args, NestValueArray* const* keys,
				 double selectivity = 0);

		void close(thread_db* tdbb) const override;
		void getLegacyPlan(thread_db* tdbb, Firebird::string& plan, unsigned level) const override;

		static unsigned maxCapacity();

	protected:
		void internalGetPlan(thread_db* tdbb, PlanEntry& planEntry, unsigned level, bool recurse) const override;
		void internalOpen(thread_db* tdbb) const override;
		bool internalGetRecord(thread_db* tdbb) const override;

	private:
		void init(thread_db* tdbb, CompilerScratch* csb, FB_SIZE_T count,
				  RecordSource* const* args, NestValueArray* const* keys,
				  double selectivity);
		ULONG computeHash(thread_db* tdbb, Request* request,
						  const SubStream& sub, UCHAR* buffer) const;
		bool fetchRecord(thread_db* tdbb, Impure* impure, FB_SIZE_T stream) const;

		SubStream m_leader;
		Firebird::Array<SubStream> m_subs;
	};

	class MergeJoin : public Join<SortedStream>
	{
		struct MergeFile
		{
			TempSpace* mfb_space;				// merge file uses SORT I/O routines
			ULONG mfb_equal_records;			// equality group cardinality
			ULONG mfb_record_size;				// matches sort map length
			ULONG mfb_current_block;			// current merge block in buffer
			ULONG mfb_block_size;				// merge block I/O size
			ULONG mfb_blocking_factor;			// merge equality records per block
			UCHAR* mfb_block_data;				// merge block I/O buffer
		};

		struct Impure : public RecordSource::Impure
		{
			// CVC: should this value exist for compatibility? It's not used.
			USHORT irsb_mrg_count;				// next stream in group
			struct irsb_mrg_repeat
			{
				SLONG irsb_mrg_equal;			// queue of equal records
				SLONG irsb_mrg_equal_end;		// end of the equal queue
				SLONG irsb_mrg_equal_current;	// last fetched record from equal queue
				SLONG irsb_mrg_last_fetched;	// first sort merge record of next group
				SSHORT irsb_mrg_order;			// logical merge order by substream
				MergeFile irsb_mrg_file;		// merge equivalence file
			} irsb_mrg_rpt[1];
		};

		static const FB_SIZE_T MERGE_BLOCK_SIZE = 65536;

	public:
		MergeJoin(CompilerScratch* csb, FB_SIZE_T count,
				  SortedStream* const* args,
				  const NestValueArray* const* keys);

		void close(thread_db* tdbb) const override;
		void getLegacyPlan(thread_db* tdbb, Firebird::string& plan, unsigned level) const override;

	protected:
		void internalGetPlan(thread_db* tdbb, PlanEntry& planEntry, unsigned level, bool recurse) const override;
		void internalOpen(thread_db* tdbb) const override;
		bool internalGetRecord(thread_db* tdbb) const override;

	private:
		int compare(thread_db* tdbb, const NestValueArray* node1,
			const NestValueArray* node2) const;
		UCHAR* getData(thread_db* tdbb, MergeFile* mfb, SLONG record) const;
		SLONG getRecordByIndex(thread_db* tdbb, FB_SIZE_T index) const;
		bool fetchRecord(thread_db* tdbb, FB_SIZE_T index) const;

		Firebird::Array<const NestValueArray*> m_keys;
	};

	class LocalTableStream final : public RecordStream
	{
	public:
		LocalTableStream(CompilerScratch* csb, StreamType stream, const DeclareLocalTableNode* table);

		void close(thread_db* tdbb) const override;

		bool refetchRecord(thread_db* tdbb) const override;
		WriteLockResult lockRecord(thread_db* tdbb) const override;

		void getLegacyPlan(thread_db* tdbb, Firebird::string& plan, unsigned level) const override;

	protected:
		void internalGetPlan(thread_db* tdbb, PlanEntry& planEntry, unsigned level, bool recurse) const override;
		void internalOpen(thread_db* tdbb) const override;
		bool internalGetRecord(thread_db* tdbb) const override;

	private:
		const DeclareLocalTableNode* m_table;
	};

	class Union final : public RecordStream
	{
		struct Impure : public RecordSource::Impure
		{
			USHORT irsb_count;
		};

	public:
		Union(CompilerScratch* csb, StreamType stream,
			  FB_SIZE_T argCount, RecordSource* const* args, NestConst<MapNode>* maps,
			  const StreamList& streams);

		void close(thread_db* tdbb) const override;

		bool refetchRecord(thread_db* tdbb) const override;
		WriteLockResult lockRecord(thread_db* tdbb) const override;

		void getLegacyPlan(thread_db* tdbb, Firebird::string& plan, unsigned level) const override;

		void markRecursive() override;
		void invalidateRecords(Request* request) const override;
		void findUsedStreams(StreamList& streams, bool expandAll = false) const override;
		bool isDependent(const StreamList& streams) const override;

	protected:
		void internalGetPlan(thread_db* tdbb, PlanEntry& planEntry, unsigned level, bool recurse) const override;
		void internalOpen(thread_db* tdbb) const override;
		bool internalGetRecord(thread_db* tdbb) const override;

	private:
		Firebird::Array<NestConst<RecordSource> > m_args;
		Firebird::Array<NestConst<MapNode> > m_maps;
		StreamList m_streams;
	};

	class RecursiveStream final : public RecordStream
	{
		static const FB_SIZE_T MAX_RECURSE_LEVEL = 1024;

		enum Mode { ROOT, RECURSE };

		struct Impure: public RecordSource::Impure
		{
			USHORT irsb_level;
			Mode irsb_mode;
			UCHAR* irsb_stack;
			UCHAR* irsb_data;
		};

	public:
		RecursiveStream(CompilerScratch* csb, StreamType stream, StreamType mapStream,
					    RecordSource* root, RecordSource* inner,
					    const MapNode* rootMap, const MapNode* innerMap,
					    const StreamList& innerStreams,
					    ULONG saveOffset);

		void close(thread_db* tdbb) const override;

		bool refetchRecord(thread_db* tdbb) const override;
		WriteLockResult lockRecord(thread_db* tdbb) const override;

		void getLegacyPlan(thread_db* tdbb, Firebird::string& plan, unsigned level) const override;

		void markRecursive() override;
		void invalidateRecords(Request* request) const override;
		void findUsedStreams(StreamList& streams, bool expandAll = false) const override;
		bool isDependent(const StreamList& streams) const override;

	protected:
		void internalGetPlan(thread_db* tdbb, PlanEntry& planEntry, unsigned level, bool recurse) const override;
		void internalOpen(thread_db* tdbb) const override;
		bool internalGetRecord(thread_db* tdbb) const override;

	private:
		void cleanupLevel(Request* request, Impure* impure) const;

		const StreamType m_mapStream;
		NestConst<RecordSource> m_root;
		NestConst<RecordSource> m_inner;
		const MapNode* const m_rootMap;
		const MapNode* const m_innerMap;
		StreamList m_innerStreams;
		const ULONG m_saveOffset;
		FB_SIZE_T m_saveSize;
	};

	class ConditionalStream : public RecordSource
	{
		struct Impure : public RecordSource::Impure
		{
			const RecordSource* irsb_next;
		};

	public:
		ConditionalStream(CompilerScratch* csb, RecordSource* first, RecordSource* second,
						  BoolExprNode* boolean);

		void close(thread_db* tdbb) const override;

		bool refetchRecord(thread_db* tdbb) const override;
		WriteLockResult lockRecord(thread_db* tdbb) const override;

		void getLegacyPlan(thread_db* tdbb, Firebird::string& plan, unsigned level) const override;

		void markRecursive() override;
		void invalidateRecords(Request* request) const override;

		void findUsedStreams(StreamList& streams, bool expandAll = false) const override;
		bool isDependent(const StreamList& streams) const override;
		void nullRecords(thread_db* tdbb) const override;

	protected:
		void internalGetPlan(thread_db* tdbb, PlanEntry& planEntry, unsigned level, bool recurse) const override;
		void internalOpen(thread_db* tdbb) const override;
		bool internalGetRecord(thread_db* tdbb) const override;

	private:
		NestConst<RecordSource> m_first;
		NestConst<RecordSource> m_second;
		NestConst<BoolExprNode> const m_boolean;
	};

	class TableValueFunctionScan : public RecordStream
	{
	protected:
		struct Impure : public RecordSource::Impure
		{
			RecordBuffer* m_recordBuffer;
		};

	public:
		TableValueFunctionScan(CompilerScratch* csb, StreamType stream,
							   const Firebird::string& alias);

		bool refetchRecord(thread_db* tdbb) const override;
		WriteLockResult lockRecord(thread_db* tdbb) const override;
		void getLegacyPlan(thread_db* tdbb, Firebird::string& plan, unsigned level) const override;

	protected:
		bool internalGetRecord(thread_db* tdbb) const override;
		void assignParameter(thread_db* tdbb, dsc* fromDesc, const dsc* toDesc, SSHORT toId,
							 Record* record) const;

		virtual bool nextBuffer(thread_db* tdbb) const = 0;

		MetaName m_name;
		const Firebird::string m_alias;
	};

	class UnlistFunctionScan final : public TableValueFunctionScan
	{
		enum UnlistTypeItemIndex : unsigned
		{
			UNLIST_INDEX_STRING = 0,
			UNLIST_INDEX_SEPARATOR = 1,
			UNLIST_INDEX_LAST = 2
		};

		struct Impure : public TableValueFunctionScan::Impure
		{
			blb* m_blob;
			Firebird::string* m_separatorStr;
			Firebird::string* m_resultStr;
		};

	public:
		UnlistFunctionScan(CompilerScratch* csb, StreamType stream, const Firebird::string& alias,
						   ValueListNode* list);

	protected:
		void close(thread_db* tdbb) const final;
		void internalOpen(thread_db* tdbb) const final;
		void internalGetPlan(thread_db* tdbb, PlanEntry& planEntry, unsigned level,
							 bool recurse) const final;

		bool nextBuffer(thread_db* tdbb) const final;

	private:
		NestConst<ValueListNode> m_inputList;
	};

} // namespace

#endif // JRD_RECORD_SOURCE_H
