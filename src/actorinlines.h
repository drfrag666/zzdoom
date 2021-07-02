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

inline DVector3 AActor::PosRelative(const line_t *line) const
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

// For Dehacked modified actors we need to dynamically check the bounce factors because MBF didn't bother to implement this properly and with other flags changing this must adjust.
inline double GetMBFBounceFactor(AActor* actor)
{
	if (actor->BounceFlags & BOUNCE_DEH) // only when modified through Dehacked. 
	{
		constexpr double MBF_BOUNCE_NOGRAVITY = 1;				// With NOGRAVITY: full momentum
		constexpr double MBF_BOUNCE_FLOATDROPOFF = 0.85;		// With FLOAT and DROPOFF: 85%
		constexpr double MBF_BOUNCE_FLOAT = 0.7;				// With FLOAT alone: 70%
		constexpr double MBF_BOUNCE_DEFAULT = 0.45;				// Without the above flags: 45%

		if (actor->flags & MF_NOGRAVITY) return MBF_BOUNCE_NOGRAVITY;
		if (actor->flags & MF_FLOAT) return (actor->flags & MF_DROPOFF) ? MBF_BOUNCE_FLOATDROPOFF : MBF_BOUNCE_FLOAT;
		return MBF_BOUNCE_DEFAULT;
	}
	return actor->bouncefactor;
}

inline double GetWallBounceFactor(AActor* actor)
{
	if (actor->BounceFlags & BOUNCE_DEH) // only when modified through Dehacked. 
	{
		constexpr double MBF_BOUNCE_NOGRAVITY = 1;				// With NOGRAVITY: full momentum
		constexpr double MBF_BOUNCE_WALL = 0.5;					// Bouncing off walls: 50%
		return ((actor->flags & MF_NOGRAVITY) ? MBF_BOUNCE_NOGRAVITY : MBF_BOUNCE_WALL);
	}
	return actor->wallbouncefactor;
}

// Yet another hack for MBF...
inline bool CanJump(AActor* actor)
{
	return (actor->flags6 & MF6_CANJUMP) || (
		(actor->BounceFlags & BOUNCE_MBF) && actor->IsSentient() && (actor->flags & MF_FLOAT));
}