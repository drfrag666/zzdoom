/*
** _gl_dynlight.cpp
** Light definitions for actors.
**
**---------------------------------------------------------------------------
** Copyright 2003 Timothy Stump
** Copyright 2005 Christoph Oelckers
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/


#include <ctype.h>
#include "i_system.h"
#include "doomtype.h"
#include "c_cvars.h"
#include "c_dispatch.h"
#include "m_random.h"
#include "sc_man.h"
#include "templates.h"
#include "w_wad.h"
#include "gi.h"
#include "r_state.h"
#include "stats.h"
#include "zstring.h"
#include "d_dehacked.h"
#include "v_text.h"
#include "g_levellocals.h"
#include "a_dynlight.h"
#include "serializer.h"
#include "textures/skyboxtexture.h"

int ScriptDepth;

//==========================================================================
//
// Dehacked aliasing
//
//==========================================================================

inline PClassActor * GetRealType(PClassActor * ti)
{
	PClassActor *rep = ti->GetReplacement(nullptr, false);
	if (rep != ti && rep != NULL && rep->IsDescendantOf(NAME_DehackedPickup))
	{
		return rep;
	}
	return ti;
}

TArray<FLightAssociation> LightAssociations;
static double lightSizeFactor = 1.;

TDeletingArray<FLightDefaults *> LightDefaults;
int AttenuationIsSet = -1;

//-----------------------------------------------------------------------------
//
//
//
//-----------------------------------------------------------------------------

FSerializer &Serialize(FSerializer &arc, const char *key, FLightDefaults &value, FLightDefaults *def)
{
	if (arc.BeginObject(key))
	{
		arc("name", value.m_Name)
			.Array("args", value.m_Args, 5)
			("param", value.m_Param)
			("pos", value.m_Pos)
			("type", value.m_type)
			("attenuate", value.m_attenuate)
			("flags", value.m_lightFlags)
			("swapped", value.m_swapped)
			("spot", value.m_spot)
			("explicitpitch", value.m_explicitPitch)
			("spotinner", value.m_spotInnerAngle)
			("spotouter", value.m_spotOuterAngle)
			("pitch", value.m_pitch)
		.EndObject();
	}
	return arc;
}

FSerializer &Serialize(FSerializer &arc, const char *key, TDeletingArray<FLightDefaults *> &value, TDeletingArray<FLightDefaults *> *def)
{
	if (arc.isWriting())
	{
		if (value.Size() == 0) return arc;	// do not save empty arrays
	}
	bool res = arc.BeginArray(key);
	if (arc.isReading())
	{
		if (!res)
		{
			value.Clear();
			return arc;
		}
		value.Resize(arc.ArraySize());
		for (auto &entry : value) entry = new FLightDefaults(NAME_None);
	}
	for (unsigned i = 0; i < value.Size(); i++)
	{
		Serialize(arc, nullptr, *value[i], nullptr);
	}
	arc.EndArray();
	return arc;
}


//-----------------------------------------------------------------------------
//
//
//
//-----------------------------------------------------------------------------

void FLightDefaults::ApplyProperties(FDynamicLight * light) const
{
	auto oldtype = light->lighttype;

	light->m_active = true;
	light->lighttype = m_type;
	light->specialf1 = m_Param;
	light->pArgs = m_Args;
	light->pLightFlags = &m_lightFlags;
	if (m_lightFlags & LF_SPOT)
	{
		light->pSpotInnerAngle = &m_spotInnerAngle;
		light->pSpotOuterAngle = &m_spotOuterAngle;
		if (m_explicitPitch) light->pPitch = &m_pitch;
		else light->pPitch = &light->target->Angles.Pitch;
	}
	light->m_tickCount = 0;
	if (m_type == PulseLight)
	{
		float pulseTime = float(m_Param / TICRATE);

		light->m_lastUpdate = light->Level->maptime;
		if (m_swapped) light->m_cycler.SetParams(float(m_Args[LIGHT_SECONDARY_INTENSITY]), float(m_Args[LIGHT_INTENSITY]), pulseTime, oldtype == PulseLight);
		else light->m_cycler.SetParams(float(m_Args[LIGHT_INTENSITY]), float(m_Args[LIGHT_SECONDARY_INTENSITY]), pulseTime, oldtype == PulseLight);
		light->m_cycler.ShouldCycle(true);
		light->m_cycler.SetCycleType(CYCLE_Sin);
		light->m_currentRadius = (float)light->m_cycler.GetVal();
		if (light->m_currentRadius <= 0) light->m_currentRadius = 1;
		light->swapped = m_swapped;
	}
	light->SetOffset(m_Pos);	// this must be the last thing to do.
}

void FLightDefaults::SetAttenuationForLevel(bool yes)
{
	if (AttenuationIsSet != int(yes))
	{
		for (auto ldef : LightDefaults)
		{
			if (ldef->m_attenuate == -1)
			{
				if (yes)  ldef->m_lightFlags |= LF_ATTENUATE; else ldef->m_lightFlags &= ~LF_ATTENUATE;
			}
		}
		AttenuationIsSet = yes;
	}
}

//==========================================================================
//
// light definition file parser
//
//==========================================================================


static const char *LightTags[]=
{
   "color",
   "size",
   "secondarySize",
   "offset",
   "chance",
   "interval",
   "scale",
   "frame",
   "light",
   "{",
   "}",
   "subtractive",
   "additive",
   "halo",
   "dontlightself",
   "attenuate",
   "dontlightactors",
   "spot",
   "noshadowmap",
   nullptr
};


enum {
   LIGHTTAG_COLOR,
   LIGHTTAG_SIZE,
   LIGHTTAG_SECSIZE,
   LIGHTTAG_OFFSET,
   LIGHTTAG_CHANCE,
   LIGHTTAG_INTERVAL,
   LIGHTTAG_SCALE,
   LIGHTTAG_FRAME,
   LIGHTTAG_LIGHT,
   LIGHTTAG_OPENBRACE,
   LIGHTTAG_CLOSEBRACE,
   LIGHTTAG_SUBTRACTIVE,
   LIGHTTAG_ADDITIVE,
   LIGHTTAG_HALO,
   LIGHTTAG_DONTLIGHTSELF,
   LIGHTTAG_ATTENUATE,
   LIGHTTAG_DONTLIGHTACTORS,
   LIGHTTAG_SPOT,
   LIGHTTAG_NOSHADOWMAP,
};


extern int ScriptDepth;


//==========================================================================
//
//
//
//==========================================================================

inline float ParseFloat(FScanner &sc)
{
   sc.MustGetFloat();

   return float(sc.Float);
}


inline int ParseInt(FScanner &sc)
{
   sc.MustGetNumber();

   return sc.Number;
}


inline char *ParseString(FScanner &sc)
{
   sc.GetString();

   return sc.String;
}


static void ParseTriple(FScanner &sc, float floatVal[3])
{
   for (int i = 0; i < 3; i++)
   {
      floatVal[i] = ParseFloat(sc);
   }
}


static void AddLightDefaults(FLightDefaults *defaults)
{
   FLightDefaults *temp;
   unsigned int i;

   // remove duplicates
   for (i = 0; i < LightDefaults.Size(); i++)
   {
      temp = LightDefaults[i];
	  if (temp->GetName() == defaults->GetName())
      {
         delete temp;
         LightDefaults.Delete(i);
         break;
      }
   }

   // If the current renderer cannot handle attenuated lights we need to reduce the radius here to account for the far more bright lights this would create.
   if (/*!Renderer->CanAttenuate() &&*/ (defaults->GetAttenuate()))
   {
	   defaults->SetArg(LIGHT_INTENSITY, defaults->GetArg(LIGHT_INTENSITY) * 2 / 3);
	   defaults->SetArg(LIGHT_SECONDARY_INTENSITY, defaults->GetArg(LIGHT_SECONDARY_INTENSITY) * 2 / 3);
   }

   LightDefaults.Push(defaults);
}


//-----------------------------------------------------------------------------
//
//
//
//-----------------------------------------------------------------------------

static void ParsePointLight(FScanner &sc)
{
	int type;
	float floatTriple[3];
	int intVal;
	FLightDefaults *defaults;

	// get name
	sc.GetString();
	FName name = sc.String;

	// check for opening brace
	sc.GetString();
	if (sc.Compare("{"))
	{
		defaults = new FLightDefaults(name, PointLight);
		ScriptDepth++;
		while (ScriptDepth)
		{
			sc.GetString();
			type = sc.MatchString(LightTags);
			switch (type)
			{
			case LIGHTTAG_OPENBRACE:
				ScriptDepth++;
				break;
			case LIGHTTAG_CLOSEBRACE:
				ScriptDepth--;
				break;
			case LIGHTTAG_COLOR:
				ParseTriple(sc, floatTriple);
				defaults->SetArg(LIGHT_RED, clamp<int>((int)(floatTriple[0] * 255), 0, 255));
				defaults->SetArg(LIGHT_GREEN, clamp<int>((int)(floatTriple[1] * 255), 0, 255));
				defaults->SetArg(LIGHT_BLUE, clamp<int>((int)(floatTriple[2] * 255), 0, 255));
				break;
			case LIGHTTAG_OFFSET:
				ParseTriple(sc, floatTriple);
				defaults->SetOffset(floatTriple);
				break;
			case LIGHTTAG_SIZE:
				intVal = clamp<int>(ParseInt(sc), 1, 1024);
				defaults->SetArg(LIGHT_INTENSITY, intVal);
				break;
			case LIGHTTAG_SUBTRACTIVE:
				defaults->SetSubtractive(ParseInt(sc) != 0);
				break;
			case LIGHTTAG_ADDITIVE:
				defaults->SetAdditive(ParseInt(sc) != 0);
				break;
			case LIGHTTAG_HALO:	// old development garbage
				ParseInt(sc);
				break;
			case LIGHTTAG_NOSHADOWMAP:
				defaults->SetNoShadowmap(ParseInt(sc) != 0);
				break;
			case LIGHTTAG_DONTLIGHTSELF:
				defaults->SetDontLightSelf(ParseInt(sc) != 0);
				break;
			case LIGHTTAG_ATTENUATE:
				defaults->SetAttenuate(ParseInt(sc) != 0);
				break;
			case LIGHTTAG_DONTLIGHTACTORS:
				defaults->SetDontLightActors(ParseInt(sc) != 0);
				break;
			case LIGHTTAG_SPOT:
				{
					float innerAngle = ParseFloat(sc);
					float outerAngle = ParseFloat(sc);
					defaults->SetSpot(true);
					defaults->SetSpotInnerAngle(innerAngle);
					defaults->SetSpotOuterAngle(outerAngle);
				}
				break;
			default:
				sc.ScriptError("Unknown tag: %s\n", sc.String);
			}
		}
		AddLightDefaults(defaults);
	}
	else
	{
		sc.ScriptError("Expected '{'.\n");
	}
}


//-----------------------------------------------------------------------------
//
//
//
//-----------------------------------------------------------------------------

static void ParsePulseLight(FScanner &sc)
{
	int type;
	float floatVal, floatTriple[3];
	int intVal;
	FLightDefaults *defaults;

	// get name
	sc.GetString();
	FName name = sc.String;

	// check for opening brace
	sc.GetString();
	if (sc.Compare("{"))
	{
		defaults = new FLightDefaults(name, PulseLight);
		ScriptDepth++;
		while (ScriptDepth)
		{
			sc.GetString();
			type = sc.MatchString(LightTags);
			switch (type)
			{
			case LIGHTTAG_OPENBRACE:
				ScriptDepth++;
				break;
			case LIGHTTAG_CLOSEBRACE:
				ScriptDepth--;
				break;
			case LIGHTTAG_COLOR:
				ParseTriple(sc, floatTriple);
				defaults->SetArg(LIGHT_RED, clamp<int>((int)(floatTriple[0] * 255), 0, 255));
				defaults->SetArg(LIGHT_GREEN, clamp<int>((int)(floatTriple[1] * 255), 0, 255));
				defaults->SetArg(LIGHT_BLUE, clamp<int>((int)(floatTriple[2] * 255), 0, 255));
				break;
			case LIGHTTAG_OFFSET:
				ParseTriple(sc, floatTriple);
				defaults->SetOffset(floatTriple);
				break;
			case LIGHTTAG_SIZE:
				intVal = clamp<int>(ParseInt(sc), 1, 1024);
				defaults->SetArg(LIGHT_INTENSITY, intVal);
				break;
			case LIGHTTAG_SECSIZE:
				intVal = clamp<int>(ParseInt(sc), 1, 1024);
				defaults->SetArg(LIGHT_SECONDARY_INTENSITY, intVal);
				break;
			case LIGHTTAG_INTERVAL:
				floatVal = ParseFloat(sc);
				defaults->SetParameter(floatVal * TICRATE);
				break;
			case LIGHTTAG_SUBTRACTIVE:
				defaults->SetSubtractive(ParseInt(sc) != 0);
				break;
			case LIGHTTAG_HALO:	// old development garbage
				ParseInt(sc);
				break;
			case LIGHTTAG_NOSHADOWMAP:
				defaults->SetNoShadowmap(ParseInt(sc) != 0);
				break;
			case LIGHTTAG_DONTLIGHTSELF:
				defaults->SetDontLightSelf(ParseInt(sc) != 0);
				break;
			case LIGHTTAG_ATTENUATE:
				defaults->SetAttenuate(ParseInt(sc) != 0);
				break;
			case LIGHTTAG_DONTLIGHTACTORS:
				defaults->SetDontLightActors(ParseInt(sc) != 0);
				break;
			case LIGHTTAG_SPOT:
				{
					float innerAngle = ParseFloat(sc);
					float outerAngle = ParseFloat(sc);
					defaults->SetSpot(true);
					defaults->SetSpotInnerAngle(innerAngle);
					defaults->SetSpotOuterAngle(outerAngle);
				}
				break;
			default:
				sc.ScriptError("Unknown tag: %s\n", sc.String);
			}
		}
		defaults->OrderIntensities();

		AddLightDefaults(defaults);
	}
	else
	{
		sc.ScriptError("Expected '{'.\n");
	}
}


//-----------------------------------------------------------------------------
//
//
//
//-----------------------------------------------------------------------------

void ParseFlickerLight(FScanner &sc)
{
	int type;
	float floatVal, floatTriple[3];
	int intVal;
	FLightDefaults *defaults;

	// get name
	sc.GetString();
	FName name = sc.String;

	// check for opening brace
	sc.GetString();
	if (sc.Compare("{"))
	{
		defaults = new FLightDefaults(name, FlickerLight);
		ScriptDepth++;
		while (ScriptDepth)
		{
			sc.GetString();
			type = sc.MatchString(LightTags);
			switch (type)
			{
			case LIGHTTAG_OPENBRACE:
				ScriptDepth++;
				break;
			case LIGHTTAG_CLOSEBRACE:
				ScriptDepth--;
				break;
			case LIGHTTAG_COLOR:
				ParseTriple(sc, floatTriple);
				defaults->SetArg(LIGHT_RED, clamp<int>((int)(floatTriple[0] * 255), 0, 255));
				defaults->SetArg(LIGHT_GREEN, clamp<int>((int)(floatTriple[1] * 255), 0, 255));
				defaults->SetArg(LIGHT_BLUE, clamp<int>((int)(floatTriple[2] * 255), 0, 255));
				break;
			case LIGHTTAG_OFFSET:
				ParseTriple(sc, floatTriple);
				defaults->SetOffset(floatTriple);
				break;
			case LIGHTTAG_SIZE:
				intVal = clamp<int>(ParseInt(sc), 1, 1024);
				defaults->SetArg(LIGHT_INTENSITY, intVal);
				break;
			case LIGHTTAG_SECSIZE:
				intVal = clamp<int>(ParseInt(sc), 1, 1024);
				defaults->SetArg(LIGHT_SECONDARY_INTENSITY, intVal);
				break;
			case LIGHTTAG_CHANCE:
				floatVal = ParseFloat(sc);
				defaults->SetParameter(floatVal*360.);
				break;
			case LIGHTTAG_SUBTRACTIVE:
				defaults->SetSubtractive(ParseInt(sc) != 0);
				break;
			case LIGHTTAG_HALO:	// old development garbage
				ParseInt(sc);
				break;
			case LIGHTTAG_NOSHADOWMAP:
				defaults->SetNoShadowmap(ParseInt(sc) != 0);
				break;
			case LIGHTTAG_DONTLIGHTSELF:
				defaults->SetDontLightSelf(ParseInt(sc) != 0);
				break;
			case LIGHTTAG_ATTENUATE:
				defaults->SetAttenuate(ParseInt(sc) != 0);
				break;
			case LIGHTTAG_DONTLIGHTACTORS:
				defaults->SetDontLightActors(ParseInt(sc) != 0);
				break;
			case LIGHTTAG_SPOT:
				{
					float innerAngle = ParseFloat(sc);
					float outerAngle = ParseFloat(sc);
					defaults->SetSpot(true);
					defaults->SetSpotInnerAngle(innerAngle);
					defaults->SetSpotOuterAngle(outerAngle);
				}
				break;
			default:
				sc.ScriptError("Unknown tag: %s\n", sc.String);
			}
		}
		defaults->OrderIntensities();
		AddLightDefaults(defaults);
	}
	else
	{
		sc.ScriptError("Expected '{'.\n");
	}
}


//-----------------------------------------------------------------------------
//
//
//
//-----------------------------------------------------------------------------

void ParseFlickerLight2(FScanner &sc)
{
	int type;
	float floatVal, floatTriple[3];
	int intVal;
	FLightDefaults *defaults;

	// get name
	sc.GetString();
	FName name = sc.String;

	// check for opening brace
	sc.GetString();
	if (sc.Compare("{"))
	{
		defaults = new FLightDefaults(name, RandomFlickerLight);
		ScriptDepth++;
		while (ScriptDepth)
		{
			sc.GetString();
			type = sc.MatchString(LightTags);
			switch (type)
			{
			case LIGHTTAG_OPENBRACE:
				ScriptDepth++;
				break;
			case LIGHTTAG_CLOSEBRACE:
				ScriptDepth--;
				break;
			case LIGHTTAG_COLOR:
				ParseTriple(sc, floatTriple);
				defaults->SetArg(LIGHT_RED, clamp<int>((int)(floatTriple[0] * 255), 0, 255));
				defaults->SetArg(LIGHT_GREEN, clamp<int>((int)(floatTriple[1] * 255), 0, 255));
				defaults->SetArg(LIGHT_BLUE, clamp<int>((int)(floatTriple[2] * 255), 0, 255));
				break;
			case LIGHTTAG_OFFSET:
				ParseTriple(sc, floatTriple);
				defaults->SetOffset(floatTriple);
				break;
			case LIGHTTAG_SIZE:
				intVal = clamp<int>(ParseInt(sc), 1, 1024);
				defaults->SetArg(LIGHT_INTENSITY, intVal);
				break;
			case LIGHTTAG_SECSIZE:
				intVal = clamp<int>(ParseInt(sc), 1, 1024);
				defaults->SetArg(LIGHT_SECONDARY_INTENSITY, intVal);
				break;
			case LIGHTTAG_INTERVAL:
				floatVal = ParseFloat(sc);
				defaults->SetParameter(floatVal * 360.);
				break;
			case LIGHTTAG_SUBTRACTIVE:
				defaults->SetSubtractive(ParseInt(sc) != 0);
				break;
			case LIGHTTAG_HALO:	// old development garbage
				ParseInt(sc);
				break;
			case LIGHTTAG_NOSHADOWMAP:
				defaults->SetNoShadowmap(ParseInt(sc) != 0);
				break;
			case LIGHTTAG_DONTLIGHTSELF:
				defaults->SetDontLightSelf(ParseInt(sc) != 0);
				break;
			case LIGHTTAG_ATTENUATE:
				defaults->SetAttenuate(ParseInt(sc) != 0);
				break;
			case LIGHTTAG_DONTLIGHTACTORS:
				defaults->SetDontLightActors(ParseInt(sc) != 0);
				break;
			case LIGHTTAG_SPOT:
				{
					float innerAngle = ParseFloat(sc);
					float outerAngle = ParseFloat(sc);
					defaults->SetSpot(true);
					defaults->SetSpotInnerAngle(innerAngle);
					defaults->SetSpotOuterAngle(outerAngle);
				}
				break;
			default:
				sc.ScriptError("Unknown tag: %s\n", sc.String);
			}
		}
		if (defaults->GetArg(LIGHT_SECONDARY_INTENSITY) < defaults->GetArg(LIGHT_INTENSITY))
		{
			int v = defaults->GetArg(LIGHT_SECONDARY_INTENSITY);
			defaults->SetArg(LIGHT_SECONDARY_INTENSITY, defaults->GetArg(LIGHT_INTENSITY));
			defaults->SetArg(LIGHT_INTENSITY, v);
		}
		AddLightDefaults(defaults);
	}
	else
	{
		sc.ScriptError("Expected '{'.\n");
	}
}


//-----------------------------------------------------------------------------
//
//
//
//-----------------------------------------------------------------------------

static void ParseSectorLight(FScanner &sc)
{
	int type;
	float floatVal;
	float floatTriple[3];
	FLightDefaults *defaults;

	// get name
	sc.GetString();
	FName name = sc.String;

	// check for opening brace
	sc.GetString();
	if (sc.Compare("{"))
	{
		defaults = new FLightDefaults(name, SectorLight);
		ScriptDepth++;
		while (ScriptDepth)
		{
			sc.GetString();
			type = sc.MatchString(LightTags);
			switch (type)
			{
			case LIGHTTAG_OPENBRACE:
				ScriptDepth++;
				break;
			case LIGHTTAG_CLOSEBRACE:
				ScriptDepth--;
				break;
			case LIGHTTAG_COLOR:
				ParseTriple(sc, floatTriple);
				defaults->SetArg(LIGHT_RED, clamp<int>((int)(floatTriple[0] * 255), 0, 255));
				defaults->SetArg(LIGHT_GREEN, clamp<int>((int)(floatTriple[1] * 255), 0, 255));
				defaults->SetArg(LIGHT_BLUE, clamp<int>((int)(floatTriple[2] * 255), 0, 255));
				break;
			case LIGHTTAG_OFFSET:
				ParseTriple(sc, floatTriple);
				defaults->SetOffset(floatTriple);
				break;
			case LIGHTTAG_SCALE:
				floatVal = ParseFloat(sc);
				defaults->SetArg(LIGHT_INTENSITY, clamp((int)(floatVal * 255), 1, 1024));
				break;
			case LIGHTTAG_SUBTRACTIVE:
				defaults->SetSubtractive(ParseInt(sc) != 0);
				break;
			case LIGHTTAG_HALO:	// old development garbage
				ParseInt(sc);
				break;
			case LIGHTTAG_NOSHADOWMAP:
				defaults->SetNoShadowmap(ParseInt(sc) != 0);
				break;
			case LIGHTTAG_DONTLIGHTSELF:
				defaults->SetDontLightSelf(ParseInt(sc) != 0);
				break;
			case LIGHTTAG_ATTENUATE:
				defaults->SetAttenuate(ParseInt(sc) != 0);
				break;
			case LIGHTTAG_DONTLIGHTACTORS:
				defaults->SetDontLightActors(ParseInt(sc) != 0);
				break;
			case LIGHTTAG_SPOT:
				{
					float innerAngle = ParseFloat(sc);
					float outerAngle = ParseFloat(sc);
					defaults->SetSpot(true);
					defaults->SetSpotInnerAngle(innerAngle);
					defaults->SetSpotOuterAngle(outerAngle);
				}
				break;
			default:
				sc.ScriptError("Unknown tag: %s\n", sc.String);
			}
		}
		AddLightDefaults(defaults);
	}
	else
	{
		sc.ScriptError("Expected '{'.\n");
	}
}


//-----------------------------------------------------------------------------
//
//
//
//-----------------------------------------------------------------------------

static void AddLightAssociation(const char *actor, const char *frame, const char *light)
{
	FLightAssociation *temp;
	unsigned int i;
	FLightAssociation assoc(actor, frame, light);

	for (i = 0; i < LightAssociations.Size(); i++)
	{
		temp = &LightAssociations[i];
		if (temp->ActorName() == assoc.ActorName())
		{
			if (strcmp(temp->FrameName(), assoc.FrameName()) == 0)
			{
				temp->ReplaceLightName(assoc.Light());
				return;
			}
		}
	}

	LightAssociations.Push(assoc);
}


//-----------------------------------------------------------------------------
//
//
//
//-----------------------------------------------------------------------------

static void ParseFrame(FScanner &sc, FString name)
{
	int type, startDepth;
	FString frameName;

	// get name
	sc.GetString();
	if (strlen(sc.String) > 8)
	{
		sc.ScriptError("Name longer than 8 characters: %s\n", sc.String);
	}
	frameName = sc.String;
	frameName.ToUpper();

	startDepth = ScriptDepth;

	// check for opening brace
	sc.GetString();
	if (sc.Compare("{"))
	{
		ScriptDepth++;
		while (ScriptDepth > startDepth)
		{
			sc.GetString();
			type = sc.MatchString(LightTags);
			switch (type)
			{
			case LIGHTTAG_OPENBRACE:
				ScriptDepth++;
				break;
			case LIGHTTAG_CLOSEBRACE:
				ScriptDepth--;
				break;
			case LIGHTTAG_LIGHT:
				ParseString(sc);
				AddLightAssociation(name, frameName, sc.String);
				break;
			default:
				sc.ScriptError("Unknown tag: %s\n", sc.String);
			}
		}
	}
	else
	{
		sc.ScriptError("Expected '{'.\n");
	}
}


//-----------------------------------------------------------------------------
//
//
//
//-----------------------------------------------------------------------------

void ParseObject(FScanner &sc)
{
	int type;
	FString name;

	// get name
	sc.GetString();
	name = sc.String;
	if (!PClass::FindActor(name))
		sc.ScriptMessage("Warning: dynamic lights attached to non-existent actor %s\n", name.GetChars());

	// check for opening brace
	sc.GetString();
	if (sc.Compare("{"))
	{
		ScriptDepth++;
		while (ScriptDepth)
		{
			sc.GetString();
			type = sc.MatchString(LightTags);
			switch (type)
			{
			case LIGHTTAG_OPENBRACE:
				ScriptDepth++;
				break;
			case LIGHTTAG_CLOSEBRACE:
				ScriptDepth--;
				break;
			case LIGHTTAG_FRAME:
				ParseFrame(sc, name);
				break;
			default:
				sc.ScriptError("Unknown tag: %s\n", sc.String);
			}
		}
	}
	else
	{
		sc.ScriptError("Expected '{'.\n");
	}
}


//==========================================================================
//
//
//
//==========================================================================

// these are the core types available in the *DEFS lump
static const char *CoreKeywords[]=
{
   "pointlight",
   "pulselight",
   "flickerlight",
   "flickerlight2",
   "sectorlight",
   "object",
   "clearlights",
   "shader",
   "clearshaders",
   "skybox",
   "glow",
   "brightmap",
   "disable_fullbright",
   "hardwareshader",
   "detail",
   "#include",
   "material",
   "lightsizefactor",
   nullptr
};


enum
{
   LIGHT_POINT,
   LIGHT_PULSE,
   LIGHT_FLICKER,
   LIGHT_FLICKER2,
   LIGHT_SECTOR,
   LIGHT_OBJECT,
   LIGHT_CLEAR,
   TAG_SHADER,
   TAG_CLEARSHADERS,
   TAG_SKYBOX,
   TAG_GLOW,
   TAG_BRIGHTMAP,
   TAG_DISABLE_FB,
   TAG_HARDWARESHADER,
   TAG_DETAIL,
   TAG_INCLUDE,
   TAG_MATERIAL,
   TAG_LIGHTSIZEFACTOR
};


//==========================================================================
//
// This is only here so any shader definition for ZDoomGL can be skipped
// There is no functionality for this stuff!
//
//==========================================================================
bool ParseShader(FScanner &sc)
{
	int  ShaderDepth = 0;

	if (sc.GetString())
	{
		char *tmp;

		tmp = strstr(sc.String, "{");
		while (tmp)
		{
			ShaderDepth++;
			tmp++;
			tmp = strstr(tmp, "{");
		}

		tmp = strstr(sc.String, "}");
		while (tmp)
		{
			ShaderDepth--;
			tmp++;
			tmp = strstr(tmp, "}");
		}

		if (ShaderDepth == 0) return true;
	}
	return false;
}


//==========================================================================
//
//
//
//==========================================================================

FInternalLightAssociation::FInternalLightAssociation(FLightAssociation * asso)
{

	m_AssocLight=NULL;
	for(unsigned int i=0;i<LightDefaults.Size();i++)
	{
		if (LightDefaults[i]->GetName() == asso->Light())
		{
			m_AssocLight = LightDefaults[i];
			break;
		}
	}

	m_sprite=-1;
	m_frame = -1;
	for (unsigned i = 0; i < sprites.Size (); ++i)
	{
		if (strncmp (sprites[i].name, asso->FrameName(), 4) == 0)
		{
			m_sprite = (int)i;
			break;
		}
	}

	// Only handle lights for full frames. 
	// I won't bother with special lights for single rotations
	// because there is no decent use for them!
	if (strlen(asso->FrameName())==5 || asso->FrameName()[5]=='0')
	{
		m_frame = toupper(asso->FrameName()[4])-'A';
	}
}

//==========================================================================
//
// State lights
//
//==========================================================================
TArray<FName> ParsedStateLights;
TArray<FLightDefaults *> StateLights;



//==========================================================================
//
//
//
//==========================================================================

void InitializeActorLights()
{
	for(unsigned int i=0;i<LightAssociations.Size();i++)
	{
		PClassActor * ti = PClass::FindActor(LightAssociations[i].ActorName());
		if (ti)
		{
			ti = GetRealType(ti);
			// put this in the class data arena so that we do not have to worry about deleting it ourselves.
			void *mem = ClassDataAllocator.Alloc(sizeof(FInternalLightAssociation));
			FInternalLightAssociation * iasso = new(mem) FInternalLightAssociation(&LightAssociations[i]);
			if (iasso->Light() != nullptr)
				ti->ActorInfo()->LightAssociations.Push(iasso);
		}
	}
	// we don't need the parser data for the light associations anymore
	LightAssociations.Clear();
	LightAssociations.ShrinkToFit();

	StateLights.Resize(ParsedStateLights.Size()+1);
	for(unsigned i=0; i<ParsedStateLights.Size();i++)
	{
		if (ParsedStateLights[i] != NAME_None)
		{
			StateLights[i] = (FLightDefaults*)-1;	// something invalid that's not NULL.
			for(unsigned int j=0;j<LightDefaults.Size();j++)
			{
				if (LightDefaults[j]->GetName() == ParsedStateLights[i])
				{
					StateLights[i] = LightDefaults[j];
					break;
				}
			}
		}
		else StateLights[i] = NULL;
	}
	StateLights[StateLights.Size()-1] = NULL;	// terminator
	ParsedStateLights.Clear();
	ParsedStateLights.ShrinkToFit();
}

//==========================================================================
//
//
//
//==========================================================================

static void DoParseDefs(FScanner &sc, int workingLump)
{
	int recursion=0;
	int lump, type;

	// Get actor class name.
	while (true)
	{
		sc.SavePos();
		if (!sc.GetToken ())
		{
			return;
		}
		type = sc.MatchString(CoreKeywords);
		switch (type)
		{
		case TAG_INCLUDE:
			{
				sc.MustGetString();
				// This is not using sc.Open because it can print a more useful error message when done here
				lump = Wads.CheckNumForFullName(sc.String, true);
				if (lump==-1)
					sc.ScriptError("Lump '%s' not found", sc.String);

				FScanner newscanner(lump);
				DoParseDefs(newscanner, lump);
				break;
			}
		case LIGHT_POINT:
			ParsePointLight(sc);
			break;
		case LIGHT_PULSE:
			ParsePulseLight(sc);
			break;
		case LIGHT_FLICKER:
			ParseFlickerLight(sc);
			break;
		case LIGHT_FLICKER2:
			ParseFlickerLight2(sc);
			break;
		case LIGHT_SECTOR:
			ParseSectorLight(sc);
			break;
		case LIGHT_OBJECT:
			ParseObject(sc);
			break;
		case LIGHT_CLEAR:
			// This has been intentionally removed
			break;
		case TAG_SHADER:
			ParseShader(sc);
			break;
		case TAG_CLEARSHADERS:
			break;
		case TAG_SKYBOX:
			ParseGldefSkybox(sc);
			break;
		case TAG_GLOW:
			// gl_InitGlow(sc);
			break;
		case TAG_BRIGHTMAP:
			// gl_ParseBrightmap(sc, workingLump);
			break;
		case TAG_MATERIAL:
			// gl_ParseMaterial(sc, workingLump);
			break;
		case TAG_HARDWARESHADER:
			// gl_ParseHardwareShader(sc, workingLump);
			break;
		case TAG_DETAIL:
			// gl_ParseDetailTexture(sc);
			break;
		case TAG_LIGHTSIZEFACTOR:
			lightSizeFactor = ParseFloat(sc);
			break;
		case TAG_DISABLE_FB:
			{
				/* not implemented.
				sc.MustGetString();
				const PClass *cls = PClass::FindClass(sc.String);
				if (cls) GetDefaultByType(cls)->renderflags |= RF_NEVERFULLBRIGHT;
				*/
			}
			break;
		default:
			sc.ScriptError("Error parsing defs.  Unknown tag: %s.\n", sc.String);
			break;
		}
	}
}

//==========================================================================
//
//
//
//==========================================================================

static void LoadGLDefs(const char *defsLump)
{
	int workingLump, lastLump;
	static const char *gldefsnames[] = { "GLDEFS", defsLump, nullptr };

	lastLump = 0;
	while ((workingLump = Wads.FindLumpMulti(gldefsnames, &lastLump)) != -1)
	{
		FScanner sc(workingLump);
		DoParseDefs(sc, workingLump);
	}
}


//==========================================================================
//
//
//
//==========================================================================

void ParseGLDefs()
{
	const char *defsLump = NULL;

	LightAssociations.Clear();
	LightDefaults.Clear();
	AttenuationIsSet = -1;
	// gl_DestroyUserShaders();
	switch (gameinfo.gametype)
	{
	case GAME_Heretic:
		defsLump = "HTICDEFS";
		break;
	case GAME_Hexen:
		defsLump = "HEXNDEFS";
		break;
	case GAME_Strife:
		defsLump = "STRFDEFS";
		break;
	case GAME_Doom:
		defsLump = "DOOMDEFS";
		break;
	case GAME_Chex:
		defsLump = "CHEXDEFS";
		break;
	default: // silence GCC
		break;
	}
	ParseVavoomSkybox();
	LoadGLDefs(defsLump);
	InitializeActorLights();
}

//==========================================================================
//
//
//
//==========================================================================

void AddStateLight(FState *State, const char *lname)
{
	if (State->Light == 0)
	{
		ParsedStateLights.Push(NAME_None);
		State->Light = ParsedStateLights.Push(FName(lname));
	}
	else
	{
		ParsedStateLights.Push(FName(lname));
	}
}
