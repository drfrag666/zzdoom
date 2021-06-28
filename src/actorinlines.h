#pragma once

#include "actor.h"
#include "r_defs.h"
#include "g_levellocals.h"
#include "d_player.h"
// These depend on both actor.h and r_defs.h so they cannot be in either file without creating a cross dependency.

inline DVector3 AActor::PosRelative(int portalgroup) const
{
	return Pos() + Displacements.getOffset(Sector->PortalGroup, portalgroup);
}

inline DVector3 AActor::PosRelative(const AActor *other) const
{
	return Pos() + Displacements.getOffset(Sector->PortalGroup, other->Sector->PortalGroup);
}

inline DVector3 AActor::PosRelative(sector_t *sec) const
{
	return Pos() + Displacements.getOffset(Sector->PortalGroup, sec->PortalGroup);
}

inline DVector3 AActor::PosRelative(line_t *line) const
{
	return Pos() + Displacements.getOffset(Sector->PortalGroup, line->frontsector->PortalGroup);
}

inline DVector3 PosRelative(const DVector3 &pos, line_t *line, sector_t *refsec = NULL)
{
	return pos + Displacements.getOffset(refsec->PortalGroup, line->frontsector->PortalGroup);
}


inline void AActor::ClearInterpolation()
{
	Prev = Pos();
	PrevAngles = Angles;
	if (Sector) PrevPortalGroup = Sector->PortalGroup;
	else PrevPortalGroup = 0;
}

inline double secplane_t::ZatPoint(const AActor *ac) const
{
	return (D + normal.X*ac->X() + normal.Y*ac->Y()) * negiC;
}

inline double sector_t::HighestCeilingAt(AActor *a, sector_t **resultsec)
{
	return HighestCeilingAt(a->Pos(), resultsec);
}

inline double sector_t::LowestFloorAt(AActor *a, sector_t **resultsec)
{
	return LowestFloorAt(a->Pos(), resultsec);
}

// Consolidated from all (incomplete) variants that check if a line should block.
inline bool P_IsBlockedByLine(AActor* actor, line_t* line)
{
	// Keep this stuff readable - so no chained and nested 'if's!

	// Unconditional blockers.
	if (line->flags & (ML_BLOCKING | ML_BLOCKEVERYTHING)) return true;

	// MBF considers that friendly monsters are not blocked by monster-blocking lines.
	// This is added here as a compatibility option. Note that monsters that are dehacked
	// into being friendly with the MBF flag automatically gain MF3_NOBLOCKMONST, so this
	// just optionally generalizes the behavior to other friendly monsters.

	if (!((actor->flags3 & MF3_NOBLOCKMONST)
		|| ((i_compatflags & COMPATF_NOBLOCKFRIENDS) && (actor->flags & MF_FRIENDLY))))
	{
		// the regular 'blockmonsters' flag.
		if (line->flags & ML_BLOCKMONSTERS) return true;
		// MBF21's flag for walking monsters
		if ((line->flags2 & ML2_BLOCKLANDMONSTERS) && !(actor->flags & MF_FLOAT)) return true;
	}

	// Blocking players
	if ((actor->player != nullptr) && (line->flags & ML_BLOCK_PLAYERS)) return true;

	// Blocking floaters.
	if ((actor->flags & MF_FLOAT) && (line->flags & ML_BLOCK_FLOATERS)) return true;

	return false;
}
