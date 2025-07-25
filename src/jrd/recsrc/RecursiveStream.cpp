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
 *  The Original Code was created by Vlad Khorsun
 *  for the Firebird Open Source RDBMS project.
 *
 *  Copyright (c) 2008 Vlad Khorsun <hvlad@users.sourceforge.net>
 *  and all contributors signed below.
 *
 *  All Rights Reserved.
 *  Contributor(s): ______________________________________.
 */

#include "firebird.h"
#include "../jrd/jrd.h"
#include "../jrd/req.h"
#include "../jrd/cmp_proto.h"
#include "../jrd/exe_proto.h"
#include "../jrd/vio_proto.h"

#include "RecordSource.h"

using namespace Firebird;
using namespace Jrd;

// ----------------------------
// Data access: recursive union
// ----------------------------

RecursiveStream::RecursiveStream(CompilerScratch* csb, StreamType stream, StreamType mapStream,
							     RecordSource* root, RecordSource* inner,
							     const MapNode* rootMap, const MapNode* innerMap,
							     const StreamList& innerStreams,
							     ULONG saveOffset)
	: RecordStream(csb, stream),
	  m_mapStream(mapStream),
	  m_root(root), m_inner(inner),
	  m_rootMap(rootMap), m_innerMap(innerMap),
	  m_innerStreams(csb->csb_pool, innerStreams),
	  m_saveOffset(saveOffset)
{
	fb_assert(m_root && m_inner && m_rootMap && m_innerMap);

	m_impure = csb->allocImpure<Impure>();
	m_cardinality = root->getCardinality() * inner->getCardinality();
	m_saveSize = csb->csb_impure - saveOffset;

	// To make aggregates, unions and nested recursions inside the inner stream work correctly,
	// we need to add all the child streams as well. See CORE-3683 for the test case.
	m_inner->findUsedStreams(m_innerStreams, true);

	m_root->markRecursive();
	m_inner->markRecursive();
}

void RecursiveStream::internalOpen(thread_db* tdbb) const
{
	Request* const request = tdbb->getRequest();
	Impure* const impure = request->getImpure<Impure>(m_impure);

	impure->irsb_flags = irsb_open;

	VIO_record(tdbb, &request->req_rpb[m_stream], m_format, tdbb->getDefaultPool());
	VIO_record(tdbb, &request->req_rpb[m_mapStream], m_format, tdbb->getDefaultPool());

	// Set up our instance data

	impure->irsb_mode = ROOT;
	impure->irsb_level = 1;
	impure->irsb_stack = NULL;
	impure->irsb_data = NULL;

	// Initialize the record number of each stream in the union

	for (const auto stream : m_innerStreams)
		request->req_rpb[stream].rpb_number.setValue(BOF_NUMBER);

	m_root->open(tdbb);
}

void RecursiveStream::close(thread_db* tdbb) const
{
	Request* const request = tdbb->getRequest();
	Impure* const impure = request->getImpure<Impure>(m_impure);

	invalidateRecords(request);

	if (impure->irsb_flags & irsb_open)
	{
		impure->irsb_flags &= ~irsb_open;

		while (impure->irsb_level > 1)
		{
			m_inner->close(tdbb);
			cleanupLevel(request, impure);
		}

		m_root->close(tdbb);
	}
}

bool RecursiveStream::internalGetRecord(thread_db* tdbb) const
{
	JRD_reschedule(tdbb);

	Request* const request = tdbb->getRequest();
	record_param* const rpb = &request->req_rpb[m_stream];
	Impure* const impure = request->getImpure<Impure>(m_impure);
	Impure* const saveImpure = request->getImpure<Impure>(m_saveOffset);

	if (!(impure->irsb_flags & irsb_open))
	{
		rpb->rpb_number.setValid(false);
		return false;
	}

	const size_t rpbsSize = sizeof(record_param) * m_innerStreams.getCount();
	Record* const record = request->req_rpb[m_stream].rpb_record;
	Record* const mapRecord = request->req_rpb[m_mapStream].rpb_record;

	const RecordSource* rsb;

	switch (impure->irsb_mode)
	{
	case ROOT:
		rsb = m_root;
		break;

	case RECURSE:
		{
			// Stop infinite recursion of bad queries
			if (impure->irsb_level > MAX_RECURSE_LEVEL)
				status_exception::raise(Arg::Gds(isc_req_max_clones_exceeded));

			// Save where we are
			UCHAR* const tmp = FB_NEW_POOL(*tdbb->getDefaultPool()) UCHAR[m_saveSize + rpbsSize];
			memcpy(tmp, saveImpure, m_saveSize);

			UCHAR* p = tmp + m_saveSize;
			for (FB_SIZE_T i = 0; i < m_innerStreams.getCount(); i++)
			{
				const StreamType stream = m_innerStreams[i];
				record_param* const rpb = &request->req_rpb[stream];
				memmove(p, rpb, sizeof(record_param));
				p += sizeof(record_param);

				// Don't overwrite record contents at next level of recursion.
				// RSE_open() and company will allocate new record if needed.
				rpb->rpb_record = NULL;
			}
			impure->irsb_stack = tmp;

			impure->irsb_data = FB_NEW_POOL(*request->req_pool) UCHAR[record->getLength()];
			record->copyDataTo(impure->irsb_data);

			const Impure r = *impure;
			memset(saveImpure, 0, m_saveSize);
			*impure = r;

			// (Re-)Open a new child stream & reset record number
			m_inner->open(tdbb);
			rsb = m_inner;

			impure->irsb_level++;
		}
		break;

	default:
		fb_assert(false);
	}

	// Get the data -- if there is none go back one level and when
	// there isn't a previous level, we're done
	while (!rsb->getRecord(tdbb))
	{
		if (impure->irsb_level == 1)
		{
			rpb->rpb_number.setValid(false);
			return false;
		}

		rsb->close(tdbb);
		cleanupLevel(request, impure);

		if (impure->irsb_level > 1)
		{
			rsb = m_inner;

			// Reset our record data so that recursive WHERE clauses work
			record->copyDataFrom(impure->irsb_data);
		}
		else
		{
			rsb = m_root;
		}
	}

	impure->irsb_mode = RECURSE;

	// We've got a record, map it into the target record
	const MapNode* const map = (rsb == m_root) ? m_rootMap : m_innerMap;
	const NestConst<ValueExprNode>* const sourceEnd = map->sourceList.end();

	for (const NestConst<ValueExprNode>* source = map->sourceList.begin(),
			*target = map->targetList.begin();
		 source != sourceEnd;
		 ++source, ++target)
	{
		EXE_assignment(tdbb, *source, *target);
	}

	// copy target (next level) record into main (current level) record
	record->copyDataFrom(mapRecord, true);

	rpb->rpb_number.setValid(true);
	return true;
}

bool RecursiveStream::refetchRecord(thread_db* /*tdbb*/) const
{
	return true;
}

WriteLockResult RecursiveStream::lockRecord(thread_db* /*tdbb*/) const
{
	status_exception::raise(Arg::Gds(isc_record_lock_not_supp));
}

void RecursiveStream::getLegacyPlan(thread_db* tdbb, string& plan, unsigned level) const
{
	if (!level)
		plan += "(";

	m_root->getLegacyPlan(tdbb, plan, level + 1);

	plan += ", ";

	m_inner->getLegacyPlan(tdbb, plan, level + 1);

	if (!level)
		plan += ")";
}

void RecursiveStream::internalGetPlan(thread_db* tdbb, PlanEntry& planEntry, unsigned level, bool recurse) const
{
	planEntry.className = "RecursiveStream";

	planEntry.lines.add().text = "Recursion";
	printOptInfo(planEntry.lines);

	if (recurse)
	{
		++level;
		m_root->getPlan(tdbb, planEntry.children.add(), level, recurse);
		m_inner->getPlan(tdbb, planEntry.children.add(), level, recurse);
	}
}

void RecursiveStream::markRecursive()
{
	m_root->markRecursive();
	m_inner->markRecursive();
}

void RecursiveStream::invalidateRecords(Request* request) const
{
	m_root->invalidateRecords(request);
	m_inner->invalidateRecords(request);
}

void RecursiveStream::findUsedStreams(StreamList& streams, bool expandAll) const
{
	RecordStream::findUsedStreams(streams);

	if (expandAll)
	{
		if (!streams.exist(m_mapStream))
			streams.add(m_mapStream);

		m_root->findUsedStreams(streams, true);
		m_inner->findUsedStreams(streams, true);
	}
}

bool RecursiveStream::isDependent(const StreamList& streams) const
{
	return m_root->isDependent(streams) || m_inner->isDependent(streams);
}

void RecursiveStream::cleanupLevel(Request* request, Impure* impure) const
{
	Impure* const saveImpure = request->getImpure<Impure>(m_saveOffset);

	delete[] impure->irsb_data;

	UCHAR* const tmp = impure->irsb_stack;
	memcpy(saveImpure, tmp, m_saveSize);

	const UCHAR* p = tmp + m_saveSize;

	for (const auto stream : m_innerStreams)
	{
		record_param* const rpb = &request->req_rpb[stream];
		Record* const tempRecord = rpb->rpb_record;
		memmove(static_cast<void*>(rpb), p, sizeof(record_param));
		p += sizeof(record_param);

		// We just restored record of current recursion level, delete record
		// from upper level. It may be optimized in the future if needed.
		delete tempRecord;
	}

	delete[] tmp;
}
