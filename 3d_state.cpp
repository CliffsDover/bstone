/* ==============================================================
bstone: A source port of Blake Stone: Planet Strike

Copyright (c) 1992-2013 Apogee Entertainment, LLC
Copyright (c) 2013 Boris Bendovsky (bibendovsky@hotmail.com)

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the
Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
============================================================== */


#include "3d_def.h"


void OpenDoor(
    int16_t door);

void A_DeathScream(
    objtype* ob);

void PlaceItemType(
    int16_t itemtype,
    int16_t tilex,
    int16_t tiley);

void PlaceItemNearTile(
    int16_t itemtype,
    int16_t tilex,
    int16_t tiley);

void ChangeShootMode(
    objtype* ob);


/*
=============================================================================

 GLOBAL VARIABLES

=============================================================================
*/


dirtype opposite[9] =
{ west, southwest, south, southeast, east, northeast, north, northwest, nodir };

dirtype diagonal[9][9] = {
/* east */ { nodir, nodir, northeast, nodir, nodir, nodir, southeast, nodir, nodir },
    { nodir, nodir, nodir, nodir, nodir, nodir, nodir, nodir, nodir },
/* north */ { northeast, nodir, nodir, nodir, northwest, nodir, nodir, nodir, nodir },
    { nodir, nodir, nodir, nodir, nodir, nodir, nodir, nodir, nodir },
/* west */ { nodir, nodir, northwest, nodir, nodir, nodir, southwest, nodir, nodir },
    { nodir, nodir, nodir, nodir, nodir, nodir, nodir, nodir, nodir },
/* south */ { southeast, nodir, nodir, nodir, southwest, nodir, nodir, nodir, nodir },
    { nodir, nodir, nodir, nodir, nodir, nodir, nodir, nodir, nodir },
    { nodir, nodir, nodir, nodir, nodir, nodir, nodir, nodir, nodir }
};


void SpawnNewObj(
    uint16_t tilex,
    uint16_t tiley,
    statetype* state);

void NewState(
    objtype* ob,
    statetype* state);

bool TryWalk(
    objtype* ob,
    bool moveit);

void MoveObj(
    objtype* ob,
    int32_t move);

void KillActor(
    objtype* ob);

bool CheckLine(
    objtype* from_obj,
    objtype* to_obj);

void FirstSighting(
    objtype* ob);

bool CheckSight(
    objtype* from_obj,
    objtype* to_obj);

bool ElevatorFloor(
    int8_t x,
    int8_t y);

/*
===================
=
= SpawnNewObj
=
= Spaws a new actor at the given TILE coordinates, with the given state, and
= the given size in GLOBAL units.
=
= new = a pointer to an initialized new actor
=
===================
*/
void SpawnNewObj(
    uint16_t tilex,
    uint16_t tiley,
    statetype* state)
{
    GetNewActor();
    new_actor->state = state;
    new_actor->ticcount = static_cast<int16_t>(Random(static_cast<uint16_t>(
        state->tictime)) + 1);

    new_actor->tilex = static_cast<uint8_t>(tilex);
    new_actor->tiley = static_cast<uint8_t>(tiley);
    new_actor->x = ((int32_t)tilex << TILESHIFT) + TILEGLOBAL / 2;
    new_actor->y = ((int32_t)tiley << TILESHIFT) + TILEGLOBAL / 2;
    new_actor->dir = new_actor->trydir = nodir;

    if (!nevermark) {
        if (!actorat[tilex][tiley]) {
            actorat[tilex][tiley] = new_actor;
        }
    }

    new_actor->areanumber = GetAreaNumber(new_actor->tilex, new_actor->tiley);

#if IN_DEVELOPMENT
    if (new_actor->areanumber >= NUMAREAS && (!nevermark)) {
        Quit("Actor Spawned on wall at {} {}", new_actor->tilex, new_actor->tiley);
    }
#endif
}

/*
===================
=
= NewState
=
= Changes ob to a new state, setting ticcount to the max for that state
=
===================
*/
void NewState(
    objtype* ob,
    statetype* state)
{
    ob->state = state;
    ob->ticcount = static_cast<int16_t>(state->tictime);
}


/*
=============================================================================

                                ENEMY TILE WORLD MOVEMENT CODE

=============================================================================
*/

static bool CHECKDIAG(
    int x,
    int y)
{
    auto actor = ::actorat[x][y];
    auto temp = reinterpret_cast<size_t>(actor);

    if (temp != 0) {
        if (temp < 256) {
            return false;
        }

        if ((actor->flags & FL_SOLID) != 0) {
            return false;
        }
    }

    if (::ElevatorFloor(static_cast<int8_t>(x), static_cast<int8_t>(y))) {
        return false;
    }

    return true;
}

static bool CHECKSIDE(
    int x,
    int y)
{
    auto actor = ::actorat[x][y];
    auto temp = reinterpret_cast<size_t>(actor);

    if (temp != 0) {
        if (temp < 128) {
            return false;
        }

        if (temp < 256) {
            auto doornum = temp & 63;

            if (::doorobjlist[doornum].lock != kt_none) {
                return false;
            }
        } else if ((actor->flags & FL_SOLID) != 0) {
            return false;
        }
    }

    return true;
}

/*
==================================
=
= TryWalk
=
= Attempts to move ob in its current (ob->dir) direction.
=
= If blocked by either a wall or an actor returns FALSE
=
= If move is either clear or blocked only by a door, returns TRUE and sets
=
= ob->tilex = new destination
= ob->tiley
= ob->areanumber = the floor tile number (0-(NUMAREAS-1)) of destination
= ob->distance = TILEGLOBAl, or -doornumber if a door is blocking the way
=
= If a door is in the way, an OpenDoor call is made to start it opening.
= The actor code should wait until
=       doorobjlist[-ob->distance].action = dr_open, meaning the door has been
=       fully opened
=
==================================
*/
bool TryWalk(
    objtype* ob,
    bool moveit)
{
    int16_t doornum;
    uint8_t old_tilex = ob->tilex, old_tiley = ob->tiley;

    if (ElevatorFloor(ob->tilex, ob->tiley)) {
        return false;
    }

    doornum = -1;

    switch (ob->dir) {
    case north:
        if (!::CHECKSIDE(ob->tilex, ob->tiley - 1)) {
            return false;
        }

        if (ElevatorFloor(ob->tilex, ob->tiley - 1)) {
            return false;
        }

        if (!moveit) {
            return true;
        }

        ob->tiley--;
        break;

    case northeast:
        if (!::CHECKDIAG(ob->tilex + 1, ob->tiley - 1)) {
            return false;
        }
        if (!::CHECKDIAG(ob->tilex + 1, ob->tiley)) {
            return false;
        }
        if (!::CHECKDIAG(ob->tilex, ob->tiley - 1)) {
            return false;
        }

        if (!moveit) {
            return true;
        }

        ob->tilex++;
        ob->tiley--;
        break;

    case east:
        if (!::CHECKSIDE(ob->tilex + 1, ob->tiley)) {
            return false;
        }

        if (ElevatorFloor(ob->tilex + 1, ob->tiley)) {
            if ((doornum != -1) && (ob->obclass != electrosphereobj)) {
                OpenDoor(doornum);
            }

            return false;
        }

        if (!moveit) {
            return true;
        }

        ob->tilex++;
        break;

    case southeast:
        if (!::CHECKDIAG(ob->tilex + 1, ob->tiley + 1)) {
            return false;
        }
        if (!::CHECKDIAG(ob->tilex + 1, ob->tiley)) {
            return false;
        }
        if (!::CHECKDIAG(ob->tilex, ob->tiley + 1)) {
            return false;
        }

        if (!moveit) {
            return true;
        }

        ob->tilex++;
        ob->tiley++;
        break;

    case south:
        if (!::CHECKSIDE(ob->tilex, ob->tiley + 1)) {
            return false;
        }

        if (ElevatorFloor(ob->tilex, ob->tiley + 1)) {
            return false;
        }

        if (!moveit) {
            return true;
        }

        ob->tiley++;
        break;

    case southwest:
        if (!::CHECKDIAG(ob->tilex - 1, ob->tiley + 1)) {
            return false;
        }
        if (!::CHECKDIAG(ob->tilex - 1, ob->tiley)) {
            return false;
        }
        if (!::CHECKDIAG(ob->tilex, ob->tiley + 1)) {
            return false;
        }

        if (!moveit) {
            return true;
        }

        ob->tilex--;
        ob->tiley++;
        break;

    case west:
        if (!::CHECKSIDE(ob->tilex - 1, ob->tiley)) {
            return false;
        }

        if (ElevatorFloor(ob->tilex - 1, ob->tiley)) {
            if ((doornum != -1) && (ob->obclass != electrosphereobj)) {
                OpenDoor(doornum);
            }

            return false;
        }

        if (!moveit) {
            return true;
        }

        ob->tilex--;
        break;

    case northwest:
        if (!::CHECKDIAG(ob->tilex - 1, ob->tiley - 1)) {
            return false;
        }
        if (!::CHECKDIAG(ob->tilex - 1, ob->tiley)) {
            return false;
        }
        if (!::CHECKDIAG(ob->tilex, ob->tiley - 1)) {
            return false;
        }

        if (!moveit) {
            return true;
        }

        ob->tilex--;
        ob->tiley--;
        break;

    case nodir:
        return false;

    default:
        return false; // jam/jdebug
    }

// Should actor open this door?
//
    if (doornum != -1) {
        switch (ob->obclass) {
        // Actors that don't open doors.
        //
        case liquidobj:
        case electrosphereobj:
            ob->tilex = old_tilex;
            ob->tiley = old_tiley;
            return false;
            break;

        // All other actors open doors.
        //
        default:
            OpenDoor(doornum);
            ob->distance = -doornum - 1;
            return true;
            break;
        }
    }

    ob->areanumber = GetAreaNumber(ob->tilex, ob->tiley);

#if IN_DEVELOPMENT
    if (ob->areanumber >= NUMAREAS) {
        Quit("Actor walked on wall at {} {}", ob->tilex, ob->tiley);
    }
#endif

    ob->distance = TILEGLOBAL;
    return true;
}

bool ElevatorFloor(
    int8_t x,
    int8_t y)
{
    uint8_t tile = static_cast<uint8_t>(*(mapsegs[0] + farmapylookup[static_cast<int>(y)] + x));

    if (tile >= HIDDENAREATILE) {
        tile -= HIDDENAREATILE;
    } else {
        tile -= AREATILE;
    }

    return tile == 0;
}

/*
==================================
=
= SelectDodgeDir
=
= Attempts to choose and initiate a movement for ob that sends it towards
= the player while dodging
=
= If there is no possible move (ob is totally surrounded)
=
= ob->dir = nodir
=
= Otherwise
=
= ob->dir = new direction to follow
= ob->distance = TILEGLOBAL or -doornumber
= ob->tilex = new destination
= ob->tiley
= ob->areanumber = the floor tile number (0-(NUMAREAS-1)) of destination
=
==================================
*/
void SelectDodgeDir(
    objtype* ob)
{
    int16_t deltax = 0, deltay = 0, i;
    uint16_t absdx, absdy;
    dirtype dirtry[5];
    dirtype turnaround, tdir;

    if (ob->flags & FL_FIRSTATTACK) {
        //
        // turning around is only ok the very first time after noticing the
        // player
        //
        turnaround = nodir;
        ob->flags &= ~FL_FIRSTATTACK;
    } else {
        turnaround = opposite[ob->dir];
    }

    SeekPlayerOrStatic(ob, &deltax, &deltay);

//
// arange 5 direction choices in order of preference
// the four cardinal directions plus the diagonal straight towards
// the player
//

    if (deltax > 0) {
        dirtry[1] = east;
        dirtry[3] = west;
    } else if (deltax <= 0) {
        dirtry[1] = west;
        dirtry[3] = east;
    }

    if (deltay > 0) {
        dirtry[2] = south;
        dirtry[4] = north;
    } else if (deltay <= 0) {
        dirtry[2] = north;
        dirtry[4] = south;
    }

//
// randomize a bit for dodging
//
    absdx = static_cast<uint16_t>(abs(deltax));
    absdy = static_cast<uint16_t>(abs(deltay));

    if (absdx > absdy) {
        tdir = dirtry[1];
        dirtry[1] = dirtry[2];
        dirtry[2] = tdir;
        tdir = dirtry[3];
        dirtry[3] = dirtry[4];
        dirtry[4] = tdir;
    }

    if (US_RndT() < 128) {
        tdir = dirtry[1];
        dirtry[1] = dirtry[2];
        dirtry[2] = tdir;
        tdir = dirtry[3];
        dirtry[3] = dirtry[4];
        dirtry[4] = tdir;
    }

    dirtry[0] = diagonal [ dirtry[1] ] [ dirtry[2] ];

//
// try the directions util one works
//
    for (i = 0; i < 5; i++) {
        if (dirtry[i] == nodir || dirtry[i] == turnaround) {
            continue;
        }

        ob->dir = dirtry[i];
        if (TryWalk(ob, true)) {
            return;
        }
    }

//
// turn around only as a last resort
//
    if (turnaround != nodir) {
        ob->dir = turnaround;

        if (TryWalk(ob, true)) {
            return;
        }
    }

    ob->dir = nodir;

    if (ob->obclass == electrosphereobj) {
        ob->s_tilex = 0;
    }
}

/*
============================
=
= SelectChaseDir
=
= As SelectDodgeDir, but doesn't try to dodge
=
============================
*/
void SelectChaseDir(
    objtype* ob)
{
    int16_t deltax = 0, deltay = 0;
    dirtype d[3];
    dirtype tdir, olddir, turnaround;


    olddir = ob->dir;
    turnaround = opposite[olddir];

    SeekPlayerOrStatic(ob, &deltax, &deltay);

    d[1] = nodir;
    d[2] = nodir;

    if (deltax > 0) {
        d[1] = east;
    } else if (deltax < 0) {
        d[1] = west;
    }
    if (deltay > 0) {
        d[2] = south;
    } else if (deltay < 0) {
        d[2] = north;
    }

    if (abs(deltay) > abs(deltax)) {
        tdir = d[1];
        d[1] = d[2];
        d[2] = tdir;
    }

    if (d[1] == turnaround) {
        d[1] = nodir;
    }
    if (d[2] == turnaround) {
        d[2] = nodir;
    }


    if (d[1] != nodir) {
        ob->dir = d[1];
        if (TryWalk(ob, true)) {
            return; /*either moved forward or attacked*/
        }
    }

    if (d[2] != nodir) {
        ob->dir = d[2];
        if (TryWalk(ob, true)) {
            return;
        }
    }

/* there is no direct path to the player, so pick another direction */

    if (olddir != nodir) {
        ob->dir = olddir;
        if (TryWalk(ob, true)) {
            return;
        }
    }

    if (US_RndT() > 128) { /*randomly determine direction of search*/
        for (tdir = north; tdir <= west; tdir++) {
            if (tdir != turnaround) {
                ob->dir = tdir;
                if (TryWalk(ob, true)) {
                    return;
                }
            }
        }
    } else {
        for (tdir = west; tdir >= north; tdir--) {
            if (tdir != turnaround) {
                ob->dir = tdir;
                if (TryWalk(ob, true)) {
                    return;
                }
            }
        }
    }

    if (turnaround != nodir) {
        ob->dir = turnaround;
        if (ob->dir != nodir) {
            if (TryWalk(ob, true)) {
                return;
            }
        }
    }

    ob->dir = nodir; // can't move
    if (ob->obclass == electrosphereobj) {
        ob->s_tilex = 0;
    }
}

void GetCornerSeek(
    objtype* ob)
{
    uint8_t SeekPointX[] = { 32, 63, 32, 1 }; // s_tilex can't seek to 0!
    uint8_t SeekPointY[] = { 1, 63, 32, 1 };
    uint8_t seek_tile = US_RndT() & 3;

    ob->flags &= ~FL_RUNTOSTATIC;
    ob->s_tilex = SeekPointX[seek_tile];
    ob->s_tiley = SeekPointY[seek_tile];
}


extern int32_t last_objy;

/*
=================
=
= MoveObj
=
= Moves ob be move global units in ob->dir direction
= Actors are not allowed to move inside the player
= Does NOT check to see if the move is tile map valid
=
= ob->x = adjusted for new position
= ob->y
=
=================
*/
void MoveObj(
    objtype* ob,
    int32_t move)
{
    int32_t deltax, deltay;

    switch (ob->dir) {
    case north:
        ob->y -= move;
        break;
    case northeast:
        ob->x += move;
        ob->y -= move;
        break;
    case east:
        ob->x += move;
        break;
    case southeast:
        ob->x += move;
        ob->y += move;
        break;
    case south:
        ob->y += move;
        break;
    case southwest:
        ob->x -= move;
        ob->y += move;
        break;
    case west:
        ob->x -= move;
        break;
    case northwest:
        ob->x -= move;
        ob->y -= move;
        break;

    case nodir:
        return;

    default:
        ::Quit("Illegal direction passed.");
        break;
    }

//
// check to make sure it's not on top of player
//
    if (ob->obclass != electrosphereobj) {
        if (areabyplayer[ob->areanumber]) {
            deltax = ob->x - player->x;
            if (deltax < -MINACTORDIST || deltax > MINACTORDIST) {
                goto moveok;
            }
            deltay = ob->y - player->y;
            if (deltay < -MINACTORDIST || deltay > MINACTORDIST) {
                goto moveok;
            }

            //
            // back up
            //
            switch (ob->dir) {
            case north:
                ob->y += move;
                break;
            case northeast:
                ob->x -= move;
                ob->y += move;
                break;
            case east:
                ob->x -= move;
                break;
            case southeast:
                ob->x -= move;
                ob->y -= move;
                break;
            case south:
                ob->y -= move;
                break;
            case southwest:
                ob->x += move;
                ob->y -= move;
                break;
            case west:
                ob->x += move;
                break;
            case northwest:
                ob->x += move;
                ob->y += move;
                break;

            case nodir:
                return;
            }

            PlayerIsBlocking(ob);
            return;
        }
    }
moveok:
    ob->distance -= move;
}


extern statetype s_terrot_die1;

char dki_msg[] =
    "^FC39  YOU JUST SHOT AN\r"
    "	    INFORMANT!\r"
    "^FC79 ONLY SHOOT BIO-TECHS\r"
    "  THAT SHOOT AT YOU!\r"
    "^FC19	    DO NOT SHOOT\r"
    "	    INFORMANTS!!\r";

uint16_t actor_points[] = {
    1025, // rent-a-cop
    1050, // turret
    500, // general scientist
    5075, // pod alien
    5150, // electric alien
    2055, // electro-sphere
    5000, // pro guard
    10000, // genetic guard
    5055, // mutant human1
    6055, // mutant human2
    0, // large canister wait
    6050, // large canister alien
    0, // small canister wait
    3750, // small canister alien
    0, // gurney wait
    3750, // gurney
    12000, // liquid
    7025, // swat
    5000, // goldtern
    5000, // goldstern Morphed
    2025, // volatile transport
    2025, // floating bomb
    0, // rotating cube

    5000, // spider_mutant
    6000, // breather_beast
    7000, // cyborg_warror
    8000, // reptilian_warrior
    9000, // acid_dragon
    9000, // mech_guardian
    30000, // final boss #1
    40000, // final_boss #2
    50000, // final_boss #3
    60000, // final_boss #4

    0, 0, 0, 0, 0, // blake,crate1/2/3, oozes
    0, // pod egg

    5000, // morphing_spider_mutant
    8000, // morphing_reptilian_warrior
    6055, // morphing_mutant human2
};

// ---------------------------------------------------------------------------
//  CheckAndReserve() - Checks for room in the obj_list and returns a ptr
//      to the new object or a nullptr.
//
// ---------------------------------------------------------------------------
objtype* CheckAndReserve()
{
    usedummy = nevermark = true;
    SpawnNewObj(0, 0, &s_hold);
    usedummy = nevermark = false;

    if (new_actor == &dummyobj) {
        return nullptr;
    } else {
        return new_actor;
    }
}

#ifdef TRACK_ENEMY_COUNT
extern int16_t numEnemy[];
#endif

void KillActor(
    objtype* ob)
{
    int16_t tilex, tiley;
    bool KeepSolid = false;
    bool givepoints = true;
    bool deadguy = true;
    classtype clas;

    tilex = ob->x >> TILESHIFT; // drop item on center
    tiley = ob->y >> TILESHIFT;

    ob->flags &= ~(FL_FRIENDLY | FL_SHOOTABLE);
    clas = ob->obclass;

    switch (clas) {
    case podeggobj:
        ::sd_play_actor_sound(PODHATCHSND, ob, bstone::AC_VOICE);

        ::InitSmartSpeedAnim(ob, SPR_POD_HATCH1, 0, 2, at_ONCE, ad_FWD, 7);
        KeepSolid = true;
        deadguy = givepoints = false;
        break;

    case morphing_spider_mutantobj:
    case morphing_reptilian_warriorobj:
    case morphing_mutanthuman2obj:
        ob->flags &= ~FL_SHOOTABLE;
        ::InitSmartSpeedAnim(ob, ob->temp1, 0, 8, at_ONCE, ad_FWD, 2);
        KeepSolid = true;
        deadguy = givepoints = false;
        break;

    case crate1obj:
    case crate2obj:
    case crate3obj:
#if IN_DEVELOPMENT
        if (!ob->temp3) {
            Quit("exp crate->temp3 is null!");
        }
#endif

        ui16_to_static_object(ob->temp3)->shapenum = -1;

        SpawnStatic(tilex, tiley, ob->temp2);
        ob->obclass = deadobj;
        ob->lighting = NO_SHADING; // No Shading
        ::InitSmartSpeedAnim(ob, SPR_GRENADE_EXPLODE2, 0, 3, at_ONCE, ad_FWD, 3 + (US_RndT() & 7));
        A_DeathScream(ob);
        MakeAlertNoise(ob);
        break;

    case floatingbombobj:
        ob->lighting = EXPLOSION_SHADING;
        A_DeathScream(ob);
        ::InitSmartSpeedAnim(ob, SPR_FSCOUT_DIE1, 0, 7, at_ONCE, ad_FWD, ::is_ps() ? 5 : 17);
        break;

    case volatiletransportobj:
        ob->lighting = EXPLOSION_SHADING;
        A_DeathScream(ob);
        ::InitSmartSpeedAnim(ob, SPR_GSCOUT_DIE1, 0, 8, at_ONCE, ad_FWD, ::is_ps() ? 5 : 17);
        break;

    case goldsternobj:
        NewState(ob, &s_goldwarp_it);
        GoldsternInfo.flags = GS_NEEDCOORD;
        GoldsternInfo.GoldSpawned = false;

        // Init timer.  Search for a location out of all possible locations.

        GoldsternInfo.WaitTime = MIN_GOLDIE_WAIT + Random(MAX_GOLDIE_WAIT - MIN_GOLDIE_WAIT); // Reinit Delay Timer before spawning on new position
        clas = goldsternobj;

        if (::gamestate.mapon == 9) {
            static_cast<void>(::ReserveStatic());
            ::PlaceReservedItemNearTile(bo_gold_key, ob->tilex, ob->tiley);
        }
        break;

    case gold_morphobj:
        GoldsternInfo.flags = GS_NO_MORE;

        ::sd_play_actor_sound(PODDEATHSND, ob, bstone::AC_VOICE);

        ob->flags |= FL_OFFSET_STATES;
        InitAnim(ob, SPR_GOLD_DEATH1, 0, 4, at_ONCE, ad_FWD, 25, 9);
        break;

    case gen_scientistobj:
        if (ob->flags & FL_INFORMANT) {
            givepoints = false;
            clas = nothing;
            gamestuff.level[gamestate.mapon].stats.accum_inf--;
            if (!(gamestate.flags & GS_KILL_INF_WARN) || (US_RndT() < 25)) {
                DisplayInfoMsg(dki_msg, static_cast<msg_priorities>(MP_INTERROGATE - 1), DISPLAY_MSG_STD_TIME * 3, MT_GENERAL);
                gamestate.flags |= GS_KILL_INF_WARN;
            }
        }
        NewState(ob, &s_ofcdie1);
        if ((ob->ammo) && !(ob->flags & FL_INFORMANT)) {
            if (US_RndT() < 65) {
                PlaceItemType(bo_coin, tilex, tiley);
            } else {
                PlaceItemType(bo_clip2, tilex, tiley);
            }
        }
        break;

    case rentacopobj:
        NewState(ob, &s_rent_die1);
        if (!(gamestate.weapons & (1 << wp_pistol))) {
            PlaceItemType(bo_pistol, tilex, tiley);
        } else if (US_RndT() < 65 || (!ob->ammo)) {
            PlaceItemType(bo_coin, tilex, tiley);
        } else if (ob->ammo) {
            PlaceItemType(bo_clip2, tilex, tiley);
        }
        break;

    case swatobj:
        NewState(ob, &s_swatdie1);
        if (!(gamestate.weapons & (1 << wp_burst_rifle))) {
            PlaceItemType(bo_burst_rifle, tilex, tiley);
        } else if (US_RndT() < 65 || (!ob->ammo)) {
            PlaceItemType(bo_coin, tilex, tiley);
        } else if (ob->ammo) {
            PlaceItemType(bo_clip2, tilex, tiley);
        }
        break;


    case proguardobj:
        NewState(ob, &s_prodie1);
        if (!(gamestate.weapons & (1 << wp_burst_rifle))) {
            PlaceItemType(bo_burst_rifle, tilex, tiley);
        } else if (US_RndT() < 65 || (!ob->ammo)) {
            PlaceItemType(bo_coin, tilex, tiley);
        } else if (ob->ammo) {
            PlaceItemType(bo_clip2, tilex, tiley);
        }
        break;

    case electroobj:
        NewState(ob, &s_electro_die1);
        eaList[ob->temp2].aliens_out--;
        ob->obclass = nothing;
        actorat[ob->tilex][ob->tiley] = nullptr;
        break;

    case liquidobj:
        NewState(ob, &s_liquid_die1);
        ob->obclass = nothing;
        actorat[ob->tilex][ob->tiley] = nullptr;
        break;

    case podobj:
        ob->temp1 = SPR_POD_DIE1;
        NewState(ob, &s_ofs_pod_death1);
        A_DeathScream(ob);
        break;

    case electrosphereobj:
        ob->obclass = nothing;
        ob->temp1 = SPR_ELECTRO_SPHERE_DIE1;
        NewState(ob, &s_ofs_esphere_death1);
        actorat[ob->tilex][ob->tiley] = nullptr;
        break;

    case mutant_human1obj:
        PlaceItemNearTile(bo_clip2, tilex, tiley);
    case final_boss3obj:
    case final_boss4obj:
    case mutant_human2obj:
    case scan_alienobj:
    case lcan_alienobj:
        NewState(ob, &s_ofs_die1);
        break;

    case cyborg_warriorobj:
    case mech_guardianobj:
    case reptilian_warriorobj:
        if (::is_ps()) {
            ::PlaceItemNearTile(bo_clip2, tilex, tiley);
        }
    case spider_mutantobj:
    case breather_beastobj:
    case acid_dragonobj:
        ::NewState(ob, &s_ofs_die1);

        if (!::is_ps()) {
            static_cast<void>(::ReserveStatic());
            ::PlaceReservedItemNearTile(bo_gold_key, ob->tilex, ob->tiley);
            ActivatePinballBonus(B_GALIEN_DESTROYED);
        }
        break;

    case final_boss2obj:
        ::sd_play_actor_sound(PODDEATHSND, ob, bstone::AC_VOICE);

        InitAnim(ob, SPR_BOSS8_DIE1, 0, 4, at_ONCE, ad_FWD, 25, 9);
        break;

    case genetic_guardobj:
    case final_boss1obj:
    case gurneyobj:
        if (!(gamestate.weapons & (1 << wp_pistol))) {
            PlaceItemNearTile(bo_pistol, tilex, tiley);
        } else {
            PlaceItemNearTile(bo_clip2, tilex, tiley);
        }
        NewState(ob, &s_ofs_die1);
        break;

    case gurney_waitobj: // mutant asleep on gurney
        ::InitSmartAnim(ob, SPR_GURNEY_MUT_B1, 0, 3, at_ONCE, ad_FWD);
        KeepSolid = true;
        givepoints = false;
        break;

    case scan_wait_alienobj: // Actual Canister - Destroyed
        ::InitSmartAnim(ob, SPR_SCAN_ALIEN_B1, 0, 3, at_ONCE, ad_FWD);
        KeepSolid = true;
        givepoints = false;
        break;

    case lcan_wait_alienobj: // Actual Canister - Destroyed
        ::InitSmartAnim(ob, SPR_LCAN_ALIEN_B1, 0, 3, at_ONCE, ad_FWD);
        KeepSolid = true;
        givepoints = false;
        break;

    case hang_terrotobj:
        NewState(ob, &s_terrot_die1);
        ob->lighting = EXPLOSION_SHADING;
        break;

    case rotating_cubeobj:
        if (::is_ps()) {
            break;
        }

        ::A_DeathScream(ob);
        ob->ammo = 0;
        ob->lighting = EXPLOSION_SHADING;
        ::InitSmartSpeedAnim(ob, SPR_VITAL_DIE_1, 0, 7, at_ONCE, ad_FWD, 7);
        break;

    default:
        break;
    }

#if LOOK_FOR_DEAD_GUYS
    switch (clas) {
    case SMART_ACTORS:
        DeadGuys[NumDeadGuys++] = ob;
        break;
    }
#endif

    if (KeepSolid) {
        ob->flags &= ~(FL_SHOOTABLE);

        if (::is_ps()) {
            ob->flags2 &= ~FL2_BFG_SHOOTABLE;
        }

        if (deadguy) {
            ob->flags |= FL_DEADGUY;
        }
    } else {
        if (deadguy) {
            ob->flags |= (FL_NONMARK | FL_DEADGUY);
        }

        if ((clas >= rentacopobj) && (clas < crate1obj) && (clas != electroobj) && (clas != goldsternobj)) {
            gamestuff.level[gamestate.mapon].stats.accum_enemy++;
#ifdef TRACK_ENEMY_COUNT
            numEnemy[clas]--;
#endif
        }

        if (givepoints) {
            if ((clas == electroobj) || (clas == goldsternobj)) {
                GivePoints(actor_points[clas - rentacopobj], false);
            } else {
                GivePoints(actor_points[clas - rentacopobj], true);
            }
        }

        ob->flags &= ~(FL_SHOOTABLE | FL_SOLID | FL_FAKE_STATIC);

        if (::is_ps()) {
            ob->flags2 &= ~FL2_BFGSHOT_SOLID;
        }

        if ((actorat[ob->tilex][ob->tiley]) == ob) {
            // Clear actor from WHERE IT WAS GOING in actorat[].
            //
            if (!(tilemap[ob->tilex][ob->tiley] & 0x80)) {
                actorat[ob->tilex][ob->tiley] = nullptr;
            }

            // Set actor WHERE IT DIED in actorat[], IF there's a door!
            // Otherwise, just leave it removed!
            //
            if (tilemap[tilex][tiley] & 0x80) {
                actorat[tilex][tiley] = ob;
            } else {
                ob->flags |= FL_NEVERMARK;
            }
        }
    }

    DropCargo(ob);

    ob->tilex = static_cast<uint8_t>(tilex);
    ob->tiley = static_cast<uint8_t>(tiley);

    if ((LastMsgPri == MP_TAKE_DAMAGE) && (LastInfoAttacker == clas)) {
        MsgTicsRemain = 1;
    }

    switch (clas) {
    case electroobj:
    case liquidobj:
    case electrosphereobj:
        ob->obclass = clas;
        ob->flags |= FL_NEVERMARK;
        break;

    default:
        break;
    }
}

void DoAttack(
    objtype* ob);

extern statetype s_proshoot2;
extern statetype s_goldmorphwait1;
extern bool barrier_damage;

/*
===================
=
= DamageActor
=
= Called when the player succesfully hits an enemy.
=
= Does damage points to enemy ob, either putting it into a stun frame or
= killing it.
=
===================
*/
void DamageActor(
    objtype* ob,
    uint16_t damage,
    objtype* attacker)
{
    int16_t old_hp = ob->hitpoints, wound_mod, mod_before = 0, mod_after = 1;

    if (!(ob->flags & FL_SHOOTABLE)) {
        return;
    }

    if (gamestate.weapon != wp_autocharge) {
        MakeAlertNoise(player);
    }

    if (ob->flags & FL_FREEZE) {
        return;
    }

    switch (ob->obclass) {
    case hang_terrotobj:
        if (gamestate.weapon < wp_burst_rifle) {
            return;
        }
        break;

    case gurney_waitobj:
        if (ob->temp3) {
            return;
        }
        break;

    case arc_barrierobj:
        if (attacker->obclass == bfg_shotobj) {
            if (BARRIER_STATE(ob) != bt_DISABLING) {
                BARRIER_STATE(ob) = bt_DISABLING;
                ob->hitpoints = 15;
                ob->temp3 = 0;
                ob->temp2 = US_RndT() & 0xf;
                NewState(ob, &s_barrier_shutdown);
            }
        }
        return;

    case rotating_cubeobj:
        if (::is_ps()) {
            return;
        }

    case post_barrierobj:
        return;

    case plasma_detonatorobj:
        //
        // Detonate 'Em!
        //
        if (attacker == player) {
            ob->temp3 = 1;
        } else {
            ob->temp3 = damage;
        }
        return;

    default:
        break;
    }

//
// do double damage if shooting a non attack mode actor
//
    if (!(ob->flags & FL_ATTACKMODE)) {
        damage <<= 1;
    }

    ob->hitpoints -= damage;
    ob->flags2 |= (::is_ps() ? FL2_DAMAGE_CLOAK : 0);

    if (ob->hitpoints <= 0) {
        switch (ob->obclass) {
#ifdef OBJ_RESERV
        case scan_wait_alienobj: // These actors do not have an ouch!
        case lcan_wait_alienobj: // So... RETURN!
        case gurney_waitobj:
            ob->temp2 = actor_to_ui16(CheckAndReserve());

            if (ob->temp2 == 0) {
                ob->hitpoints += damage;
                return;
            }
            break;
#endif

        case goldsternobj:
            if (gamestate.mapon == GOLD_MORPH_LEVEL) {
                extern int16_t morphWaitTime;
                extern bool noShots;

                morphWaitTime = 60;
                noShots = true;
                NewState(ob, &s_goldmorphwait1);
                ob->obclass = gold_morphingobj;
                ob->flags &= ~FL_SHOOTABLE;
                return;
            }
            break;

        default:
            break;

        }

        ob->hitpoints = actor_to_ui16(attacker);

        KillActor(ob);
        return;
    } else {
        switch (ob->obclass) {
        case swatobj:
            // Don't get wounded if it's an arc!
            //
            if ((attacker->obclass == arc_barrierobj) ||
                (attacker->obclass == post_barrierobj))
            {
                break;
            }

            // Calculate 'wound boundary' (based on NUM_WOUND_STAGES).
            //
            wound_mod = starthitpoints[gamestate.difficulty][en_swat] / (ob->temp1 + 1) + 1;
            mod_before = old_hp / wound_mod;
            mod_after = ob->hitpoints / wound_mod;

            // If modulo 'before' and 'after' are different, we've crossed
            // a 'wound boundary'!
            //
            if (mod_before != mod_after) {
                ::sd_play_actor_sound(
                    SWATDEATH2SND, ob, bstone::AC_VOICE);

                NewState(ob, &s_swatwounded1);
                ob->flags &= ~(FL_SHOOTABLE | FL_SOLID);
                ob->temp2 = (5 * 60) + ((US_RndT() % 20) * 60);
                return;
            }
            break;

        default:
            break;
        }

        if (ob->flags & FL_LOCKED_STATE) {
            return;
        }

        if (!(ob->flags & FL_ATTACKMODE)) {
            if ((ob->obclass == gen_scientistobj) && (ob->flags & FL_INFORMANT)) {
                return;
            }
            FirstSighting(ob); // put into combat mode
        }

        switch (ob->obclass) {
        case volatiletransportobj:
        case floatingbombobj:
            ::T_PainThink(ob);
            break;

        case goldsternobj:
            NewState(ob, &s_goldpain);
            break;

        case gold_morphobj:
            NewState(ob, &s_mgold_pain);
            break;

        case liquidobj:
            NewState(ob, &s_liquid_ouch);
            break;

        case rentacopobj:
            NewState(ob, &s_rent_pain);
            break;

        case podobj:
            NewState(ob, &s_ofs_pod_ouch);
            break;

        case spider_mutantobj:
        case breather_beastobj:
        case cyborg_warriorobj:
        case reptilian_warriorobj:
        case acid_dragonobj:
        case mech_guardianobj:
        case final_boss1obj:
        case final_boss2obj:
        case final_boss3obj:
        case final_boss4obj:

        case genetic_guardobj:
        case mutant_human1obj:
        case mutant_human2obj:
        case scan_alienobj:
        case lcan_alienobj:
        case gurneyobj:
            NewState(ob, &s_ofs_pain);
            break;

        case electrosphereobj:
            NewState(ob, &s_ofs_ouch);
            ob->temp1 = SPR_ELECTRO_SPHERE_OUCH;
            break;

        case electroobj:
            NewState(ob, &s_electro_ouch);
            break;

        case gen_scientistobj:
            NewState(ob, &s_ofcpain);
            break;

        case swatobj:
            NewState(ob, &s_swatpain);
            break;

        case proguardobj:
            NewState(ob, &s_propain);
            break;

        case rotating_cubeobj:
            if (::is_ps()) {
                break;
            }

            // Show 'pain' animation only once
            if ((ob->hitpoints + damage) ==
                starthitpoints[gamestate.difficulty][en_rotating_cube])
            {
                ::InitSmartSpeedAnim(ob, SPR_VITAL_OUCH, 0, 0, at_ONCE, ad_FWD, 23);
            }
            break;

        default:
            break;
        }
    }

// Make sure actors aren't sitting ducks!
//

    if ((US_RndT() < 192) &&
        (!(ob->flags & (FL_LOCKED_STATE | FL_BARRIER_DAMAGE))))
    {
        ChangeShootMode(ob);
        DoAttack(ob);
    }

    ob->flags |= FL_LOCKED_STATE;
}


/*
=============================================================================

 CHECKSIGHT

=============================================================================
*/

/*
=====================
=
= CheckLine
=
= Returns true if a straight line between the player and ob is unobstructed
=
=====================
*/
bool CheckLine(
    objtype* from_obj,
    objtype* to_obj)
{
    int16_t x1, y1, xt1, yt1, x2, y2, xt2, yt2;
    int16_t x, y;
    int16_t xdist, ydist, xstep, ystep;
    int16_t partial, delta;
    int32_t ltemp;
    int16_t xfrac, yfrac, deltafrac;
    uint16_t value, intercept;



    x1 = static_cast<int16_t>(from_obj->x >> UNSIGNEDSHIFT); // 1/256 tile precision
    y1 = static_cast<int16_t>(from_obj->y >> UNSIGNEDSHIFT);
    xt1 = x1 >> 8;
    yt1 = y1 >> 8;

//      x2 = plux;
//      y2 = pluy;

    x2 = static_cast<int16_t>(to_obj->x >> UNSIGNEDSHIFT);
    y2 = static_cast<int16_t>(to_obj->y >> UNSIGNEDSHIFT);
    xt2 = to_obj->tilex;
    yt2 = to_obj->tiley;


    xdist = static_cast<int16_t>(abs(xt2 - xt1));

    if (xdist > 0) {
        if (xt2 > xt1) {
            partial = 256 - (x1 & 0xff);
            xstep = 1;
        } else {
            partial = x1 & 0xff;
            xstep = -1;
        }

        deltafrac = static_cast<int16_t>(abs(x2 - x1));
        if (!deltafrac) {
            deltafrac = 1;
        }
        delta = y2 - y1;
        ltemp = ((int32_t)delta << 8) / deltafrac;
        if (ltemp > 0x7fffl) {
            ystep = 0x7fff;
        } else if (ltemp < -0x7fffl) {
            ystep = -0x7fff;
        } else {
            ystep = static_cast<int16_t>(ltemp);
        }
        yfrac = y1 + ((static_cast<int32_t>(ystep) * partial) >> 8);

        x = xt1 + xstep;
        xt2 += xstep;
        do {
            y = yfrac >> 8;
            yfrac += ystep;

            value = (uint16_t)tilemap[x][y];
            x += xstep;

            if (!value) {
                continue;
            }

            if (value < 128 || value > 256) {
                return false;
            }

            //
            // see if the door is open enough
            //
            value &= ~0x80;
            intercept = yfrac - ystep / 2;

            if (intercept > doorposition[value]) {
                return false;
            }

        } while (x != xt2);
    }

    ydist = static_cast<int16_t>(abs(yt2 - yt1));

    if (ydist > 0) {
        if (yt2 > yt1) {
            partial = 256 - (y1 & 0xff);
            ystep = 1;
        } else {
            partial = y1 & 0xff;
            ystep = -1;
        }

        deltafrac = static_cast<int16_t>(abs(y2 - y1));
        if (!deltafrac) {
            deltafrac = 1;
        }
        delta = x2 - x1;
        ltemp = ((int32_t)delta << 8) / deltafrac;
        if (ltemp > 0x7fffl) {
            xstep = 0x7fff;
        } else if (ltemp < -0x7fffl) {
            xstep = -0x7fff;
        } else {
            xstep = static_cast<int16_t>(ltemp);
        }
        xfrac = x1 + (((int32_t)xstep * partial) >> 8);

        y = yt1 + ystep;
        yt2 += ystep;
        do {
            x = xfrac >> 8;
            xfrac += xstep;

            value = (uint16_t)tilemap[x][y];
            y += ystep;

            if (!value) {
                continue;
            }

            if (value < 128 || value > 256) {
                return false;
            }

            //
            // see if the door is open enough
            //
            value &= ~0x80;
            intercept = xfrac - xstep / 2;

            if (intercept > doorposition[value]) {
                return false;
            }
        } while (y != yt2);
    }

    return true;
}

/*
================
=
= CheckSight
=
= Checks a straight line between player and current object
=
= If the sight is ok, check alertness and angle to see if they notice
=
= returns true if the player has been spoted
=
================
*/
bool CheckSight(
    objtype* from_obj,
    objtype* to_obj)
{
    const int MINSIGHT = 0x18000L;

    int32_t deltax, deltay;

//
// don't bother tracing a line if the area isn't connected to the player's
//
    if (!areabyplayer[from_obj->areanumber]) {
        return false;
    }

//
// if the player is real close, sight is automatic
//
    deltax = to_obj->x - from_obj->x;
    deltay = to_obj->y - from_obj->y;


    if (deltax > -MINSIGHT && deltax < MINSIGHT && deltay > -MINSIGHT && deltay < MINSIGHT) {
        return true;
    }

//
// see if they are looking in the right direction
//
    switch (from_obj->dir) {
    case north:
        if (deltay > 0) {
            return false;
        }
        break;

    case east:
        if (deltax < 0) {
            return false;
        }
        break;

    case south:
        if (deltay < 0) {
            return false;
        }
        break;

    case west:
        if (deltax > 0) {
            return false;
        }
        break;

    default:
        break;
    }

//
// trace a line to check for blocking tiles (corners)
//
    return CheckLine(from_obj, to_obj);
}



/*
===============
=
= FirstSighting
=
= Puts an actor into attack mode and possibly reverses the direction
= if the player is behind it
=
===============
*/
void FirstSighting(
    objtype* ob)
{
    if (PlayerInvisable) {
        return;
    }

//
// react to the player
//
    switch (ob->obclass) {
    case floatingbombobj:
        if (ob->flags & FL_STATIONARY) {
            return;
        }

        ::sd_play_actor_sound(SCOUT_ALERTSND, ob, bstone::AC_VOICE);

        NewState(ob, &s_scout_run);
        ob->speed *= 3; // Haul Ass
        break;


    case goldsternobj:
        ::sd_play_actor_sound(GOLDSTERNHALTSND, ob, bstone::AC_VOICE);

        NewState(ob, &s_goldchase1);
        ob->speed *= 3; // go faster when chasing player
        break;

    case rentacopobj:
        ::sd_play_actor_sound(HALTSND, ob, bstone::AC_VOICE);

        NewState(ob, &s_rent_chase1);
        ob->speed *= 3; // go faster when chasing player
        break;

    case gen_scientistobj:
        ::sd_play_actor_sound(SCIENTISTHALTSND, ob, bstone::AC_VOICE);

        NewState(ob, &s_ofcchase1);
        ob->speed *= 3; // go faster when chasing player
        break;

    case swatobj:
        ::sd_play_actor_sound(SWATHALTSND, ob, bstone::AC_VOICE);

        NewState(ob, &s_swatchase1);
        ob->speed *= 3; // go faster when chasing player
        break;

    case breather_beastobj:
    case reptilian_warriorobj:
    case genetic_guardobj:
    case final_boss4obj:
    case final_boss2obj:
        ::sd_play_actor_sound(GGUARDHALTSND, ob, bstone::AC_VOICE);

        NewState(ob, &s_ofs_chase1);
        ob->speed *= 3; // go faster when chasing player
        break;


    case cyborg_warriorobj:
    case mech_guardianobj:
    case mutant_human1obj:
    case final_boss3obj:
    case final_boss1obj:
        ::sd_play_actor_sound(BLUEBOYHALTSND, ob, bstone::AC_VOICE);

        NewState(ob, &s_ofs_chase1);
        ob->speed *= 2; // go faster when chasing player
        break;

    case mutant_human2obj:
        ::sd_play_actor_sound(DOGBOYHALTSND, ob, bstone::AC_VOICE);

        NewState(ob, &s_ofs_chase1);
        ob->speed *= 2; // go faster when chasing player
        break;

    case liquidobj:
        NewState(ob, &s_liquid_move);
        break;

    case spider_mutantobj:
    case scan_alienobj:
        ::sd_play_actor_sound(SCANHALTSND, ob, bstone::AC_VOICE);

        NewState(ob, &s_ofs_chase1);
        ob->speed *= 3; // go faster when chasing player
        break;

    case lcan_alienobj:
        ::sd_play_actor_sound(LCANHALTSND, ob, bstone::AC_VOICE);

        NewState(ob, &s_ofs_chase1);
        ob->speed *= 3; // go faster when chasing player
        break;

    case gurneyobj:
        ::sd_play_actor_sound(GURNEYSND, ob, bstone::AC_VOICE);

        NewState(ob, &s_ofs_chase1);
        ob->speed *= 3; // go faster when chasing player
        break;

    case acid_dragonobj:
    case podobj:
        ::sd_play_actor_sound(PODHALTSND, ob, bstone::AC_VOICE);

        NewState(ob, &s_ofs_chase1);
        ob->speed *= 2;
        break;

    case gurney_waitobj:
        if (ob->temp3) {
            ob->temp2 = actor_to_ui16(CheckAndReserve());

            if (ob->temp2 != 0) {
                ob->flags &= ~(FL_SHOOTABLE);
                ::InitSmartAnim(ob, SPR_GURNEY_MUT_B1, 0, 3, at_ONCE, ad_FWD);
            } else {
                return;
            }
        }
        break;

    case proguardobj:
        ::sd_play_actor_sound(PROHALTSND, ob, bstone::AC_VOICE);

        NewState(ob, &s_prochase1);
        ob->speed *= 4; // go faster when chasing player
        break;

    case hang_terrotobj:
        ::sd_play_actor_sound(TURRETSND, ob, bstone::AC_VOICE);

        NewState(ob, &s_terrot_seek1);
        break;

    default:
        break;

    }

    ob->flags |= FL_ATTACKMODE | FL_FIRSTATTACK;
}

/*
===============
=
= SightPlayer
=
= Called by actors that ARE NOT chasing the player.  If the player
= is detected (by sight, noise, or proximity), the actor is put into
= it's combat frame and true is returned.
=
= Incorporates a random reaction delay
=
===============
*/
bool SightPlayer(
    objtype* ob)
{
    if (ob->obclass == gen_scientistobj) {
        if (ob->flags & FL_INFORMANT) {
            return false;
        }
    }

    if (PlayerInvisable) {
        return false;
    }

    if (ob->flags & FL_ATTACKMODE) {
        return true;
    }

    if (ob->temp2) {
        //
        // count down reaction time
        //
        ob->temp2 -= tics;
        if (ob->temp2 > 0) {
            return false;
        }
        ob->temp2 = 0; // time to react
    } else {
        if (!areabyplayer[ob->areanumber]) {
            return false;
        }

        if (ob->flags & FL_AMBUSH) {
            if (!CheckSight(ob, player)) {
                return false;
            }
            ob->flags &= ~FL_AMBUSH;
        } else {
            bool sighted = false;

            if (madenoise || CheckSight(ob, player)) {
                sighted = true;
            }

            switch (ob->obclass) {
            // Actors that look fine while JUST STANDING AROUND should go here.
            //
            case rentacopobj:
            case proguardobj:
            case swatobj:
            case goldsternobj:
            case gen_scientistobj:
            case floatingbombobj:
            case volatiletransportobj:
                break;

            // Actors that look funny when just standing around go here...
            //
            default:
                if (ob->flags & FL_VISABLE) {
                    sighted = true;
                }
                break;
            }

            if (!sighted) {
                return false;
            }
        }

        switch (ob->obclass) {
        case goldsternobj:
            ob->temp2 = 1 + US_RndT() / 4;
            break;


        case rentacopobj:
            ob->temp2 = 1 + US_RndT() / 4;
            break;

        case gen_scientistobj:
            ob->temp2 = 2;
            break;

        case swatobj:
            ob->temp2 = 1 + US_RndT() / 6;
            break;

        case proguardobj:
            ob->temp2 = 1 + US_RndT() / 6;
            break;

        case hang_terrotobj:
            ob->temp2 = 1 + US_RndT() / 4;
            break;

        case gurney_waitobj:
            ob->temp2 = ob->temp3;
            break;

        case liquidobj:
            ob->temp2 = 1 + US_RndT() / 6;
            break;

        case floatingbombobj:
            ob->temp2 = 1 + US_RndT() / 4;
            break;

        case genetic_guardobj:
        case mutant_human1obj:
        case mutant_human2obj:
        case scan_alienobj:
        case lcan_alienobj:
        case gurneyobj:
        case spider_mutantobj:
        case breather_beastobj:
        case cyborg_warriorobj:
        case reptilian_warriorobj:
        case acid_dragonobj:
        case mech_guardianobj:
        case final_boss1obj:
        case final_boss2obj:
        case final_boss3obj:
        case final_boss4obj:
            ob->temp2 = 1;
            break;

        default:
            break;
        }
        ob->flags &= ~FL_FRIENDLY;
        return false;
    }

    FirstSighting(ob);

    return true;
}


int16_t AdjAngleTable[2][8] = {
    { 225, 270, 315, 360, 45, 90, 135, 180 }, // Upper Bound
    { 180, 225, 270, 315, 0, 45, 90, 135 }, // Lower Bound
};

// --------------------------------------------------------------------------
// CheckView()  Checks a straight line between player and current object
//      If the sight is ok, check angle to see if they notice
//      returns true if the player has been spoted
// --------------------------------------------------------------------------
bool CheckView(
    objtype* from_obj,
    objtype* to_obj)
{
    int32_t deltax, deltay;
    int16_t angle;
    float fangle;

    //
    // don't bother tracing a line if the area isn't connected to the player's
    //

    if (!areabyplayer[from_obj->areanumber]) {
        return false;
    }

    deltax = from_obj->x - to_obj->x;
    deltay = to_obj->y - from_obj->y;


    fangle = static_cast<float>(atan2(static_cast<double>(deltay), static_cast<double>(deltax))); // returns -pi to pi
    if (fangle < 0) {
        fangle = static_cast<float>(::m_pi() * 2 + fangle);
    }

    angle = static_cast<int16_t>(fangle / (::m_pi() * 2) * ANGLES + 23);

    if (angle > 360) {
        angle = 360;
    }

    if ((angle <= AdjAngleTable[1][from_obj->dir]) || (angle >= AdjAngleTable[0][from_obj->dir])) {
        return false;
    }

    //
    // trace a line to check for blocking tiles (corners)
    //

    return CheckLine(from_obj, to_obj);
}

#if LOOK_FOR_DEAD_GUYS
// --------------------------------------------------------------------------
// LookForDeadGuys()
// --------------------------------------------------------------------------
bool LookForDeadGuys(
    objtype* obj)
{
    uint8_t loop;
    bool DeadGuyFound = false;

    if ((obj->obclass == gen_scientistobj) && (obj->flags & FL_INFORMANT)) {
        return false;
    }

    for (loop = 0; loop < NumDeadGuys; loop++) {
        if (CheckSight(obj, DeadGuys[loop])) {
            DeadGuyFound = true;
            FirstSighting(obj);
            break;
        }
    }

    return DeadGuyFound;
}
#endif

bool LookForGoodies(
    objtype* ob,
    uint16_t RunReason)
{
    statobj_t* statptr;
    bool just_find_door = false;

// Don't look for goodies if this actor is simply a non-informant that
// was interrogated. (These actors back away, then attack!)
//
    if (RunReason == RR_INTERROGATED) {
        just_find_door = true;
        if (US_RndT() < 128) {
            ob->flags &= ~FL_INTERROGATED; // No longer runs, now attacks!
        }
    }

// We'll let the computer-controlled actors cheat in some circumstances...
//
    if ((player->areanumber != ob->areanumber) && (!(ob->flags & FL_VISABLE))) {
        if (!ob->ammo) {
            ob->ammo += 8;
        }
        if (ob->hitpoints <= (starthitpoints[gamestate.difficulty][ob->obclass - rentacopobj] >> 1)) {
            ob->hitpoints += 10;
        }
        return true;
    }

// Let's REALLY look for some goodies!
//
    if (!just_find_door) {
        for (statptr = &statobjlist[0]; statptr != laststatobj; statptr++) {
            if ((ob->areanumber == statptr->areanumber) && (statptr->shapenum != -1)) {
                switch (statptr->itemnumber) {
                case bo_chicken:
                case bo_ham:
                case bo_water:
                case bo_clip:
                case bo_clip2:
                case bo_candybar:
                case bo_sandwich:

                    // If actor is 'on top' of this object, get it!
                    //
                    if ((statptr->tilex == ob->tilex) && (statptr->tiley == ob->tiley)) {
                        int16_t shapenum = -1;

                        switch (statptr->itemnumber) {
                        case bo_clip:
                        case bo_clip2:
                            if (ob->ammo) { // this actor has plenty
                                continue; // of ammo!
                            }
                            ob->ammo += 8;
                            break;

                        case bo_candybar:
                        case bo_sandwich:
                        case bo_chicken:
                            if (ob->hitpoints > (starthitpoints[gamestate.difficulty][ob->obclass - rentacopobj] >> 1)) {
                                continue; // actor has plenty of health!
                            }
                            ob->hitpoints += 8;
                            shapenum = statptr->shapenum + 1;
                            break;

                        case bo_ham:
                            if (ob->hitpoints > (starthitpoints[gamestate.difficulty][ob->obclass - rentacopobj] >> 1)) {
                                continue; // actor has plenty of health!
                            }
                            ob->hitpoints += 12;
                            shapenum = statptr->shapenum + 1;
                            break;

                        case bo_water:
                            if (ob->hitpoints > (starthitpoints[gamestate.difficulty][ob->obclass - rentacopobj] >> 1)) {
                                continue; // actor has plenty of health!
                            }
                            ob->hitpoints += 2;
                            shapenum = statptr->shapenum + 1;
                            break;
                        }

                        ob->s_tilex = 0; // reset for next search!
                        statptr->shapenum = shapenum; // remove from list if necessary
                        statptr->itemnumber = bo_nothing;
                        statptr->flags &= ~FL_BONUS;
                        return true;
                    }

                    // Give actor a chance to run towards this object.
                    //
                    if ((!(ob->flags & FL_RUNTOSTATIC))) { // &&(US_RndT()<25))
                        if (
                            ((RunReason & RR_AMMO) &&
                             ((statptr->itemnumber == bo_clip) ||
                              (statptr->itemnumber == bo_clip2)))
                            ||
                            ((RunReason & RR_HEALTH) &&
                             ((statptr->itemnumber == bo_firstaid) ||
                              (statptr->itemnumber == bo_water) ||
                              (statptr->itemnumber == bo_chicken) ||
                              (statptr->itemnumber == bo_candybar) ||
                              (statptr->itemnumber == bo_sandwich) ||
                              (statptr->itemnumber == bo_ham)))
                            )
                        {
                            ob->flags |= FL_RUNTOSTATIC;
                            ob->s_tilex = statptr->tilex;
                            ob->s_tiley = statptr->tiley;
                            return false;
                        }
                    }
                }
            }
        }
    }

// Should actor run for a door? (quick escape!)
//
    if (areabyplayer[ob->areanumber]) {
        const int DOOR_CHOICES = 8;

        doorobj_t* door;
        doorobj_t* doorlist[DOOR_CHOICES];
        int8_t doorsfound = 0;

        // If actor is running for a 'goody' or a door -- leave it alone!
        //
        if (ob->flags & FL_RUNTOSTATIC) {
            return false;
        }

        // Search for all doors in actor's current area.
        //
        for (door = &doorobjlist[0]; door != lastdoorobj; door++) {
            // Is this an elevator door   OR   a locked door?
            //
            if ((!(*(mapsegs[0] + farmapylookup[door->tiley] + (door->tilex - 1)) - AREATILE)) ||
                (!(*(mapsegs[0] + farmapylookup[door->tiley] + (door->tilex + 1)) - AREATILE)) ||
                (door->lock != kt_none))
            {
                continue;
            }

            // Does this door connect the area the actor is in with another area?
            //
            if ((door->areanumber[0] == ob->areanumber) ||
                (door->areanumber[1] == ob->areanumber))
            {
                doorlist[static_cast<int>(doorsfound)] = door; // add to list
                if (++doorsfound == DOOR_CHOICES) { // check for max
                    break;
                }
            }
        }

        // Randomly choose a door if any were found.
        //
        if (doorsfound) {
            int8_t doornum;

            // Randomly choose a door from the list.
            // (Only choose the last door used if it's the only door in this area!)
            //
            doornum = static_cast<int8_t>(Random(doorsfound));
            door = doorlist[static_cast<int>(doornum)];

            if (door == ui16_to_door_object(ob->temp3) && doorsfound > 1) {
                doornum++;
                if (doornum >= doorsfound) {
                    doornum = 0;
                }
                door = doorlist[static_cast<int>(doornum)];
            }

            ob->temp3 = door_object_to_ui16(door);

            ob->s_tilex = door->tilex;
            ob->s_tiley = door->tiley;

            ob->flags |= FL_RUNTOSTATIC;
        }
    } else {
        // Either: actor is running to corner (leave it alone)     OR
        //         actor is chasing an object already removed by another actor
        //         (make this actor run to corner)
        //
        if (ob->flags & FL_RUNTOSTATIC) {
            ob->s_tilex = 0;
        }
    }

    return false;
}

uint16_t CheckRunChase(
    objtype* ob)
{
    const int RUNAWAY_SPEED = 1000;

    uint16_t RunReason = 0;

// Mark the reason for running.
//
    if (!ob->ammo) { // Out of ammo!
        RunReason |= RR_AMMO;
    }

    if (ob->hitpoints <= (starthitpoints[gamestate.difficulty][ob->obclass - rentacopobj] >> 1)) {
        RunReason |= RR_HEALTH; // Feeling sickly!

    }
    if ((ob->flags & (FL_FRIENDLY | FL_INTERROGATED)) == FL_INTERROGATED) {
        RunReason |= RR_INTERROGATED; // Non-informant was interrogated!

    }

// Time to RUN or CHASE?
//
    if (RunReason) { // run, Run, RUN!
        if (!(ob->flags & FL_RUNAWAY)) {
            ob->temp3 = 0;
            ob->flags |= FL_RUNAWAY;
            ob->speed += RUNAWAY_SPEED;
        }
    } else { // chase, Chase, CHASE!
        if (ob->flags & FL_RUNAWAY) {
            ob->flags &= ~FL_RUNAWAY;
            ob->speed -= RUNAWAY_SPEED;
        }
    }

    return RunReason;
}

void SeekPlayerOrStatic(
    objtype* ob,
    int16_t* deltax,
    int16_t* deltay)
{
    uint16_t whyrun = 0;
    bool smart = false;

// Is this a "smart" actor?
//
    switch (ob->obclass) {
    case SMART_ACTORS:
        smart = true;
        break;

    case electrosphereobj:
        if (!ob->s_tilex) {
            GetCornerSeek(ob);
        }
        *deltax = ob->s_tilex - ob->tilex;
        *deltay = ob->s_tiley - ob->tiley;
        return;
        break;

    default:
        break;
    }

// Should actor run away (chase static) or chase player?
//
    if ((smart) && ((whyrun = CheckRunChase(ob))) != 0) {
        // Initilize seek tile?
        //
        if (!ob->s_tilex) {
            GetCornerSeek(ob);
        }

        // Are there any goodies available?
        //
        if (!LookForGoodies(ob, whyrun)) {
            // Change seek tile when actor reaches it.
            //
            if ((ob->tilex == ob->s_tilex) && (ob->tiley == ob->s_tiley)) { // don't forget me!
                GetCornerSeek(ob);
                ob->flags &= ~FL_INTERROGATED;
            } // add me, too

            // Calculate horizontal / vertical distance to seek point.
            //
            *deltax = ob->s_tilex - ob->tilex;
            *deltay = ob->s_tiley - ob->tiley;
        } else {
            whyrun = CheckRunChase(ob);
        }
    }

// Make actor chase player if it's not running.
//
    if (!whyrun) {
        *deltax = player->tilex - ob->tilex;
        *deltay = player->tiley - ob->tiley;
    }
}

bool PlayerIsBlocking(
    objtype* ob)
{
    int8_t opp_off[9][2] = {
        { -1, 0 }, { -1, 1 }, { 0, 1 }, { 1, 1 },
        { 1, 0 }, { 1, -1 }, { 0, -1 }, { -1, -1 }, { 0, 0 }
    };

    ob->tilex += opp_off[ob->dir][0];
    ob->tiley += opp_off[ob->dir][1];
    ob->dir = opposite[ob->dir];
    ob->distance = TILEGLOBAL - ob->distance;
    return true;
}

void MakeAlertNoise(
    objtype* obj)
{
    madenoise = true;
    alerted = 2;
    alerted_areanum = obj->areanumber;
}
