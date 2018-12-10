//-----------------------------------------------------------------------------
//
// Copyright 1993-1996 id Software
// Copyright 1994-1996 Raven Software
// Copyright 1999-2016 Randy Heit
// Copyright 2002-2018 Christoph Oelckers
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/
//
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//
//-----------------------------------------------------------------------------


#include <math.h>
#include <float.h>
#ifdef _MSC_VER
#include <malloc.h>		// for alloca()
#endif

#include "templates.h"
#include "d_player.h"
#include "m_argv.h"
#include "m_swap.h"
#include "m_bbox.h"
#include "g_game.h"
#include "i_system.h"
#include "x86.h"
#include "w_wad.h"
#include "doomdef.h"
#include "p_local.h"
#include "p_effect.h"
#include "p_terrain.h"
#include "nodebuild.h"
#include "s_sound.h"
#include "doomstat.h"
#include "p_lnspec.h"
#include "v_palette.h"
#include "c_console.h"
#include "c_cvars.h"
#include "p_acs.h"
#include "announcer.h"
#include "wi_stuff.h"
#include "stats.h"
#include "doomerrors.h"
#include "gi.h"
#include "p_conversation.h"
#include "a_keys.h"
#include "s_sndseq.h"
#include "sbar.h"
#include "p_setup.h"
#include "r_data/r_translate.h"
#include "r_data/r_interpolate.h"
#include "r_sky.h"
#include "cmdlib.h"
#include "g_level.h"
#include "md5.h"
#include "po_man.h"
#include "r_renderer.h"
#include "r_data/colormaps.h"
#include "p_blockmap.h"
#include "r_utility.h"
#include "p_spec.h"
#include "p_saveg.h"
#include "g_levellocals.h"
#include "events.h"
#include "p_destructible.h"
#include "types.h"
#include "i_time.h"
#include "scripting/vm/vm.h"
#include "fragglescript/t_fs.h"
#include "maploader/maploader.h"

void P_ClearUDMFKeys();

extern AActor *SpawnMapThing (int index, FMapThing *mthing, int position);

extern unsigned int R_OldBlend;

static void P_Shutdown ();

static void AddToList(uint8_t *hitlist, FTextureID texid, int bitmask)
{
	if (hitlist[texid.GetIndex()] & bitmask) return;	// already done, no need to process everything again.
	hitlist[texid.GetIndex()] |= (uint8_t)bitmask;

	for (auto anim : TexMan.mAnimations)
	{
		if (texid == anim->BasePic || (!anim->bDiscrete && anim->BasePic < texid && texid < anim->BasePic + anim->NumFrames))
		{
			for (int i = anim->BasePic.GetIndex(); i < anim->BasePic.GetIndex() + anim->NumFrames; i++)
			{
				hitlist[i] |= (uint8_t)bitmask;
			}
		}
	}

	auto switchdef = TexMan.FindSwitch(texid);
	if (switchdef)
	{
		for (int i = 0; i < switchdef->NumFrames; i++)
		{
			hitlist[switchdef->frames[i].Texture.GetIndex()] |= (uint8_t)bitmask;
		}
		for (int i = 0; i < switchdef->PairDef->NumFrames; i++)
		{
			hitlist[switchdef->frames[i].Texture.GetIndex()] |= (uint8_t)bitmask;
		}
	}

	auto adoor = TexMan.FindAnimatedDoor(texid);
	if (adoor)
	{
		for (int i = 0; i < adoor->NumTextureFrames; i++)
		{
			hitlist[adoor->TextureFrames[i].GetIndex()] |= (uint8_t)bitmask;
		}
	}
}

//===========================================================================
//
// P_PrecacheLevel
//
// Preloads all relevant graphics for the Level->
//
//===========================================================================

static void PrecacheLevel(FLevelLocals *Level)
{
	if (demoplayback)
		return;

	int i;
	TMap<PClassActor *, bool> actorhitlist;
	int cnt = TexMan.NumTextures();
	TArray<uint8_t> hitlist(cnt, true);

	memset(hitlist.Data(), 0, cnt);

	AActor *actor;
	TThinkerIterator<AActor> iterator;

	while ((actor = iterator.Next()))
	{
		actorhitlist[actor->GetClass()] = true;
	}

	for (auto n : gameinfo.PrecachedClasses)
	{
		PClassActor *cls = PClass::FindActor(n);
		if (cls != nullptr) actorhitlist[cls] = true;
	}
	for (unsigned i = 0; i < Level->info->PrecacheClasses.Size(); i++)
	{
		// Level->info can only store names, no class pointers.
		PClassActor *cls = PClass::FindActor(Level->info->PrecacheClasses[i]);
		if (cls != nullptr) actorhitlist[cls] = true;
	}

	for (i = Level->sectors.Size() - 1; i >= 0; i--)
	{
		AddToList(hitlist.Data(), Level->sectors[i].GetTexture(sector_t::floor), FTextureManager::HIT_Flat);
		AddToList(hitlist.Data(), Level->sectors[i].GetTexture(sector_t::ceiling), FTextureManager::HIT_Flat);
	}

	for (i = Level->sides.Size() - 1; i >= 0; i--)
	{
		AddToList(hitlist.Data(), Level->sides[i].GetTexture(side_t::top), FTextureManager::HIT_Wall);
		AddToList(hitlist.Data(), Level->sides[i].GetTexture(side_t::mid), FTextureManager::HIT_Wall);
		AddToList(hitlist.Data(), Level->sides[i].GetTexture(side_t::bottom), FTextureManager::HIT_Wall);
	}

	// Sky texture is always present.
	// Note that F_SKY1 is the name used to
	//	indicate a sky floor/ceiling as a flat,
	//	while the sky texture is stored like
	//	a wall texture, with an episode dependant
	//	name.

	if (sky1texture.isValid())
	{
		AddToList(hitlist.Data(), sky1texture, FTextureManager::HIT_Sky);
	}
	if (sky2texture.isValid())
	{
		AddToList(hitlist.Data(), sky2texture, FTextureManager::HIT_Sky);
	}

	for (auto n : gameinfo.PrecachedTextures)
	{
		FTextureID tex = TexMan.CheckForTexture(n, ETextureType::Wall, FTextureManager::TEXMAN_Overridable | FTextureManager::TEXMAN_TryAny | FTextureManager::TEXMAN_ReturnFirst);
		if (tex.Exists()) AddToList(hitlist.Data(), tex, FTextureManager::HIT_Wall);
	}
	for (unsigned i = 0; i < Level->info->PrecacheTextures.Size(); i++)
	{
		FTextureID tex = TexMan.CheckForTexture(Level->info->PrecacheTextures[i], ETextureType::Wall, FTextureManager::TEXMAN_Overridable | FTextureManager::TEXMAN_TryAny | FTextureManager::TEXMAN_ReturnFirst);
		if (tex.Exists()) AddToList(hitlist.Data(), tex, FTextureManager::HIT_Wall);
	}

	Renderer->Precache(hitlist.Data(), actorhitlist);

}

//==========================================================================
//
//
//
//===========================================================================

void P_FreeLevelData ()
{
	// [ZZ] delete per-map event handlers
	E_Shutdown(true);
	FCanvasTextureInfo::EmptyList();
	R_FreePastViewers();
	P_ClearUDMFKeys();

	// [RH] Clear all ThingID hash chains.
	AActor::ClearTIDHashes();

	interpolator.ClearInterpolations();	// [RH] Nothing to interpolate on a fresh level.
	Renderer->CleanLevelData();
	FPolyObj::ClearAllSubsectorLinks(); // can't be done as part of the polyobj deletion process.
	SN_StopAllSequences ();
	DThinker::DestroyAllThinkers ();
	P_ClearPortals();
	tagManager.Clear();
	level.total_monsters = level.total_items = level.total_secrets =
		level.killed_monsters = level.found_items = level.found_secrets =
		wminfo.maxfrags = 0;
		
	if (level.sectors.Size() > 0)
	{
		delete[] level.sectors[0].e;
	}
	for (auto &sub : level.subsectors)
	{
		if (sub.BSP != nullptr) delete sub.BSP;
	}

	FBehavior::StaticUnloadModules ();
	level.sections.Clear();
	level.segs.Clear();
	level.sectors.Clear();
	level.linebuffer.Clear();
	level.subsectorbuffer.Clear();
	level.lines.Clear();
	level.sides.Clear();
	level.segbuffer.Clear();
	level.loadsectors.Clear();
	level.loadlines.Clear();
	level.loadsides.Clear();
	level.vertexes.Clear();
	level.nodes.Clear();
	level.gamenodes.Reset();
	level.subsectors.Clear();
	level.gamesubsectors.Reset();
	level.rejectmatrix.Clear();
	level.Zones.Clear();
	level.blockmap.Clear();
	level.Polyobjects.Clear();

	for(auto &pb : level.PolyBlockMap)
	{
		polyblock_t *link = pb;
		while (link != nullptr)
		{
			polyblock_t *next = link->next;
			delete link;
			link = next;
		}
	}
	level.PolyBlockMap.Reset();

	level.deathmatchstarts.Clear();
	level.AllPlayerStarts.Clear();
	memset(level.playerstarts, 0, sizeof(level.playerstarts));

	P_FreeStrifeConversations ();
	level.Scrolls.Clear();
	P_ClearUDMFKeys();
}

//===========================================================================
//
// P_SetupLevel
//
// [RH] position indicates the start spot to spawn at
//
//===========================================================================

void P_SetupLevel(const char *lumpname, int position, bool newGame)
{
	int i;

	level.ShaderStartTime = I_msTimeFS(); // indicate to the shader system that the level just started

	// This is motivated as follows:

	level.maptype = MAPTYPE_UNKNOWN;
	wminfo.partime = 180;

	if (!savegamerestore)
	{
		level.SetMusicVolume(level.MusicVolume);
		for (i = 0; i < MAXPLAYERS; ++i)
		{
			players[i].killcount = players[i].secretcount
				= players[i].itemcount = 0;
		}
	}
	for (i = 0; i < MAXPLAYERS; ++i)
	{
		players[i].mo = nullptr;
	}
	// [RH] Clear any scripted translation colors the previous level may have set.
	for (i = 0; i < int(translationtables[TRANSLATION_LevelScripted].Size()); ++i)
	{
		FRemapTable *table = translationtables[TRANSLATION_LevelScripted][i];
		if (table != nullptr)
		{
			delete table;
			translationtables[TRANSLATION_LevelScripted][i] = nullptr;
		}
	}
	translationtables[TRANSLATION_LevelScripted].Clear();

	// Initial height of PointOfView will be set by player think.
	players[consoleplayer].viewz = NO_VALUE;

	// Make sure all sounds are stopped before Z_FreeTags.
	S_Start();

	// [RH] clear out the mid-screen message
	C_MidPrint(nullptr, nullptr);

	// Free all level data from the previous map
	P_FreeLevelData();

	MapData *map = P_OpenMapData(lumpname, true);
	if (map == nullptr)
	{
		I_Error("Unable to open map '%s'\n", lumpname);
	}

	// [ZZ] init per-map static handlers. we need to call this before everything is set up because otherwise scripts don't receive PlayerEntered event
	//      (which happens at god-knows-what stage in this function, but definitely not the last part, because otherwise it'd work to put E_InitStaticHandlers before the player spawning)
	E_InitStaticHandlers(true);

	// generate a checksum for the level, to be included and checked with savegames.
	map->GetChecksum(level.md5);
	// find map num
	level.lumpnum = map->lumpnum;

	if (newGame)
	{
		E_NewGame(EventHandlerType::PerMap);
	}

	MapLoader loader(&level);
	loader.LoadLevel(map, lumpname, position);
	delete map;

	// if deathmatch, randomly spawn the active players
	if (deathmatch)
	{
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (playeringame[i])
			{
				players[i].mo = nullptr;
				G_DeathMatchSpawnPlayer(i);
			}
		}
	}
	// the same, but for random single/coop player starts
	else if (level.flags2 & LEVEL2_RANDOMPLAYERSTARTS)
	{
		for (i = 0; i < MAXPLAYERS; ++i)
		{
			if (playeringame[i])
			{
				players[i].mo = nullptr;
				FPlayerStart *mthing = G_PickPlayerStart(i);
				P_SpawnPlayer(mthing, i, (level.flags2 & LEVEL2_PRERAISEWEAPON) ? SPF_WEAPONFULLYUP : 0);
			}
		}
	}

	// [SP] move unfriendly players around
	// horribly hacky - yes, this needs rewritten.
	if (level.deathmatchstarts.Size() > 0)
	{
		for (i = 0; i < MAXPLAYERS; ++i)
		{
			if (playeringame[i] && players[i].mo != nullptr)
			{
				if (!(players[i].mo->flags & MF_FRIENDLY))
				{
					AActor * oldSpawn = players[i].mo;
					G_DeathMatchSpawnPlayer(i);
					oldSpawn->Destroy();
				}
			}
		}
	}


	// Don't count monsters in end-of-level sectors if option is on
	if (dmflags2 & DF2_NOCOUNTENDMONST)
	{
		TThinkerIterator<AActor> it;
		AActor * mo;

		while ((mo = it.Next()))
		{
			if (mo->flags & MF_COUNTKILL)
			{
				if (mo->Sector->damageamount > 0 && (mo->Sector->Flags & (SECF_ENDGODMODE | SECF_ENDLEVEL)) == (SECF_ENDGODMODE | SECF_ENDLEVEL))
				{
					mo->ClearCounters();
				}
			}
		}
	}

	T_PreprocessScripts();        // preprocess FraggleScript scripts

	// build subsector connect matrix
	//	UNUSED P_ConnectSubsectors ();

	R_OldBlend = 0xffffffff;

	// [RH] Remove all particles
	P_ClearParticles();

	// preload graphics and sounds
	if (precache)
	{
		PrecacheLevel(&level);
		S_PrecacheLevel();
	}

	if (deathmatch)
	{
		AnnounceGameStart();
	}

	// This check was previously done at run time each time the heightsec was checked.
	// However, since 3D floors are static data, we can easily precalculate this and store it in the sector's flags for quick access.
	for (auto &s : level.sectors)
	{
		if (s.heightsec != nullptr)
		{
			// If any of these 3D floors render their planes, ignore heightsec.
			for (auto &ff : s.e->XFloor.ffloors)
			{
				if ((ff->flags & (FF_EXISTS | FF_RENDERPLANES)) == (FF_EXISTS | FF_RENDERPLANES))
				{
					s.MoreFlags |= SECMF_IGNOREHEIGHTSEC;	// mark the heightsec inactive.
				}
			}
		}
	}

	P_ResetSightCounters(true);

	// Create a backup of the map data so the savegame code can toss out all fields that haven't changed in order to reduce processing time and file size.
	// Note that we want binary identity here, so assignment is not sufficient because it won't initialize any padding bytes.
	// Note that none of these structures may contain non POD fields anyway.
	level.loadsectors.Resize(level.sectors.Size());
	memcpy(&level.loadsectors[0], &level.sectors[0], level.sectors.Size() * sizeof(level.sectors[0]));
	level.loadlines.Resize(level.lines.Size());
	memcpy(&level.loadlines[0], &level.lines[0], level.lines.Size() * sizeof(level.lines[0]));
	level.loadsides.Resize(level.sides.Size());
	memcpy(&level.loadsides[0], &level.sides[0], level.sides.Size() * sizeof(level.sides[0]));
}

//
// P_Init
//
void P_Init ()
{
	atterm (P_Shutdown);

	P_InitEffects ();		// [RH]
	P_InitTerrainTypes ();
	P_InitKeyMessages ();
	R_InitSprites ();
}

static void P_Shutdown ()
{	
	DThinker::DestroyThinkersInList(STAT_STATIC);	
	P_DeinitKeyMessages ();
	P_FreeLevelData ();
	// [ZZ] delete global event handlers
	E_Shutdown(false);
	ST_Clear();
	FS_Close();
	for (auto &p : players)
	{
		if (p.psprites != nullptr) p.psprites->Destroy();
	}
}

