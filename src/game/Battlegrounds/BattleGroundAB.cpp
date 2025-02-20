/*
 * Copyright (C) 2005-2011 MaNGOS <http://getmangos.com/>
 * Copyright (C) 2009-2011 MaNGOSZero <https://github.com/mangos/zero>
 * Copyright (C) 2011-2016 Nostalrius <https://nostalrius.org>
 * Copyright (C) 2016-2017 Elysium Project <https://github.com/elysium-project>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "Object.h"
#include "Player.h"
#include "BattleGround.h"
#include "BattleGroundAB.h"
#include "GameObject.h"
#include "BattleGroundMgr.h"
#include "Language.h"
#include "Util.h"
#include "WorldPacket.h"

BattleGroundAB::BattleGroundAB()
{
    m_buffChange = true;
    m_bgObjects.resize(BG_AB_OBJECT_MAX);

    m_startMessageIds[BG_STARTING_EVENT_FIRST]  = 0;
    m_startMessageIds[BG_STARTING_EVENT_SECOND] = LANG_BG_AB_START_ONE_MINUTE;
    m_startMessageIds[BG_STARTING_EVENT_THIRD]  = LANG_BG_AB_START_HALF_MINUTE;
    m_startMessageIds[BG_STARTING_EVENT_FOURTH] = LANG_BG_AB_HAS_BEGUN;
}

BattleGroundAB::~BattleGroundAB()
{
}

void BattleGroundAB::Update(uint32 diff)
{
    if (GetStatus() == STATUS_IN_PROGRESS)
    {
        int team_points[BG_TEAMS_COUNT] = { 0, 0 };

        for (int node = 0; node < BG_AB_NODES_MAX; ++node)
        {

            // 1-minute to occupy a node from contested state
            if (m_nodeTimers[node])
            {
                if (m_nodeTimers[node] > diff)
                    m_nodeTimers[node] -= diff;
                else
                {
                    m_nodeTimers[node] = 0;
                    // Change from contested to occupied !
                    uint8 teamIndex = m_nodes[node] - 1;
                    m_prevNodes[node] = m_nodes[node];
                    m_nodes[node] += 2;
                    // create new occupied banner
                    _CreateBanner(node, BG_AB_NODE_TYPE_OCCUPIED, teamIndex);
                    _SendNodeUpdate(node);
                    _NodeOccupied(node, (teamIndex == 0) ? ALLIANCE : HORDE);
                    // Message to chatlog

                    if (teamIndex == 0)
                    {
                        SendMessage2ToAll(LANG_BG_AB_NODE_TAKEN, CHAT_MSG_BG_SYSTEM_ALLIANCE, nullptr, LANG_BG_ALLY, _GetNodeNameId(node));
                        PlaySoundToAll(BG_AB_SOUND_NODE_CAPTURED_ALLIANCE);
                    }
                    else
                    {
                        SendMessage2ToAll(LANG_BG_AB_NODE_TAKEN, CHAT_MSG_BG_SYSTEM_HORDE, nullptr, LANG_BG_HORDE, _GetNodeNameId(node));
                        PlaySoundToAll(BG_AB_SOUND_NODE_CAPTURED_HORDE);
                    }
                }
            }

            for (int team = 0; team < BG_TEAMS_COUNT; ++team)
                if (m_nodes[node] == team + BG_AB_NODE_TYPE_OCCUPIED)
                    ++team_points[team];
        }

        // Accumulate points
        for (int team = 0; team < BG_TEAMS_COUNT; ++team)
        {
            int points = team_points[team];
            if (!points)
                continue;
            m_lastTick[team] += diff;
            if (m_lastTick[team] > BG_AB_TickIntervals[points])
            {
                m_lastTick[team] -= BG_AB_TickIntervals[points];
                m_teamScores[team] += BG_AB_TickPoints[points];
                m_honorScoreTics[team] += BG_AB_TickPoints[points];
                m_reputationScoreTics[team] += BG_AB_TickPoints[points];
                if (m_reputationScoreTics[team] >= m_reputationTics)
                {
                    (team == BG_TEAM_ALLIANCE) ? RewardReputationToTeam(509, 10, ALLIANCE) : RewardReputationToTeam(510, 10, HORDE);
                    m_reputationScoreTics[team] -= m_reputationTics;
                }
                if (m_honorScoreTics[team] >= m_honorTics)
                {
                    RewardHonorToTeam(BG_AB_PerTickHonor[GetBracketId()], (team == BG_TEAM_ALLIANCE) ? ALLIANCE : HORDE);
                    m_honorScoreTics[team] -= m_honorTics;
                }
                if (!m_isInformedNearVictory && m_teamScores[team] > BG_AB_WARNING_NEAR_VICTORY_SCORE)
                {
                    if (team == BG_TEAM_ALLIANCE)
                    {
                        SendMessageToAll(LANG_BG_AB_A_NEAR_VICTORY, CHAT_MSG_BG_SYSTEM_NEUTRAL);
                        PlaySoundToAll(BG_AB_SOUND_NEAR_VICTORY_ALLIANCE);
                    }
                    else
                    {
                        SendMessageToAll(LANG_BG_AB_H_NEAR_VICTORY, CHAT_MSG_BG_SYSTEM_NEUTRAL);
                        PlaySoundToAll(BG_AB_SOUND_NEAR_VICTORY_HORDE);
                    }
                    m_isInformedNearVictory = true;
                }

                if (m_teamScores[team] > BG_AB_MAX_TEAM_SCORE)
                    m_teamScores[team] = BG_AB_MAX_TEAM_SCORE;
                if (team == BG_TEAM_ALLIANCE)
                    UpdateWorldState(BG_AB_OP_RESOURCES_ALLY, m_teamScores[team]);
                if (team == BG_TEAM_HORDE)
                    UpdateWorldState(BG_AB_OP_RESOURCES_HORDE, m_teamScores[team]);
            }
        }

        // Test win condition
        if (m_teamScores[BG_TEAM_ALLIANCE] >= BG_AB_MAX_TEAM_SCORE)
            EndBattleGround(ALLIANCE);
        if (m_teamScores[BG_TEAM_HORDE] >= BG_AB_MAX_TEAM_SCORE)
            EndBattleGround(HORDE);
    }
    // Execute this at the end, since it can delete the BattleGround object!
    BattleGround::Update(diff);
}

void BattleGroundAB::StartingEventCloseDoors()
{
    // despawn buffs
    for (int i = 0; i < BG_AB_NODES_MAX * 3; ++i)
        SpawnBGObject(m_bgObjects[BG_AB_OBJECT_SPEEDBUFF_STABLES + i], RESPAWN_ONE_DAY);
}

void BattleGroundAB::StartingEventOpenDoors()
{
    for (int i = 0; i < BG_AB_NODES_MAX; ++i)
    {
        //randomly select buff to spawn
        uint8 buff = urand(0, 2);
        SpawnBGObject(m_bgObjects[BG_AB_OBJECT_SPEEDBUFF_STABLES + buff + i * 3], RESPAWN_IMMEDIATELY);
    }
    OpenDoorEvent(BG_EVENT_DOOR);
}

void BattleGroundAB::AddPlayer(Player* player)
{
    BattleGround::AddPlayer(player);
    //create score and add it to map, default values are set in the constructor
    BattleGroundABScore* sc = new BattleGroundABScore;

    m_playerScores[player->GetObjectGuid()] = sc;
}

void BattleGroundAB::RemovePlayer(Player* /*player*/, ObjectGuid /*guid*/)
{

}

void BattleGroundAB::HandleAreaTrigger(Player* source, uint32 trigger)
{
    switch (trigger)
    {
        case 3948:                                          // Arathi Basin Alliance Exit.
            if (source->GetTeam() != ALLIANCE)
                source->GetSession()->SendNotification(LANG_BATTLEGROUND_ONLY_ALLIANCE_USE);
            else
                source->LeaveBattleground();
            break;
        case 3949:                                          // Arathi Basin Horde Exit.
            if (source->GetTeam() != HORDE)
                source->GetSession()->SendNotification(LANG_BATTLEGROUND_ONLY_HORDE_USE);
            else
                source->LeaveBattleground();
            break;
        case 3866:                                          // Stables
        case 3869:                                          // Gold Mine
        case 3867:                                          // Farm
        case 3868:                                          // Lumber Mill
        case 3870:                                          // Black Smith
        case 4020:                                          // Unk1
        case 4021:                                          // Unk2
        //break;
        default:
            //sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "WARNING: Unhandled AreaTrigger in Battleground: %u", trigger);
            //source->GetSession()->SendAreaTriggerMessage("Warning: Unhandled AreaTrigger in Battleground: %u", trigger);
            break;
    }
}

/*  type: 0-neutral, 1-contested, 3-occupied
    teamIndex: 0-ally, 1-horde                        */
void BattleGroundAB::_CreateBanner(uint8 node, uint8 type, uint8 teamIndex)
{
    uint32 delay = 0;
    switch (type){
    case BG_AB_NODE_TYPE_CONTESTED:
        delay = 1;
        break;
    case BG_AB_NODE_TYPE_OCCUPIED:
        delay = 5;
        break;
    }

    // cause the node-type is in the generic form
    // please see in the headerfile for the ids
    if (type != BG_AB_NODE_TYPE_NEUTRAL)
        type += teamIndex;


    SpawnEvent(node, type, true, true, delay);                           // will automaticly despawn other events
}

int32 BattleGroundAB::_GetNodeNameId(uint8 node)
{
    switch (node)
    {
        case BG_AB_NODE_STABLES:
            return LANG_BG_AB_NODE_STABLES;
        case BG_AB_NODE_BLACKSMITH:
            return LANG_BG_AB_NODE_BLACKSMITH;
        case BG_AB_NODE_FARM:
            return LANG_BG_AB_NODE_FARM;
        case BG_AB_NODE_LUMBER_MILL:
            return LANG_BG_AB_NODE_LUMBER_MILL;
        case BG_AB_NODE_GOLD_MINE:
            return LANG_BG_AB_NODE_GOLD_MINE;
        default:
            MANGOS_ASSERT(0);
    }
    return 0;
}

void BattleGroundAB::FillInitialWorldStates(WorldPacket& data, uint32& count)
{
    uint8 const plusArray[] = {0, 2, 3, 0, 1};

    // Node icons
    for (uint8 node = 0; node < BG_AB_NODES_MAX; ++node)
        FillInitialWorldState(data, count, BG_AB_OP_NODEICONS[node], m_nodes[node] == 0);

    // Node occupied states
    for (uint8 node = 0; node < BG_AB_NODES_MAX; ++node)
        for (uint8 i = 1; i < BG_AB_NODES_MAX; ++i)
            FillInitialWorldState(data, count, BG_AB_OP_NODESTATES[node] + plusArray[i], m_nodes[node] == i);

    // How many bases each team owns
    uint8 ally = 0, horde = 0;
    for (uint8 node = 0; node < BG_AB_NODES_MAX; ++node)
    {
        if (m_nodes[node] == BG_AB_NODE_STATUS_ALLY_OCCUPIED)
            ++ally;
        else if (m_nodes[node] == BG_AB_NODE_STATUS_HORDE_OCCUPIED)
            ++horde;
    }

    FillInitialWorldState(data, count, BG_AB_OP_OCCUPIED_BASES_ALLY, ally);
    FillInitialWorldState(data, count, BG_AB_OP_OCCUPIED_BASES_HORDE, horde);

    // Team scores
    FillInitialWorldState(data, count, BG_AB_OP_RESOURCES_MAX,      BG_AB_MAX_TEAM_SCORE);
    FillInitialWorldState(data, count, BG_AB_OP_RESOURCES_WARNING,  BG_AB_WARNING_NEAR_VICTORY_SCORE);
    FillInitialWorldState(data, count, BG_AB_OP_RESOURCES_ALLY,     m_teamScores[BG_TEAM_ALLIANCE]);
    FillInitialWorldState(data, count, BG_AB_OP_RESOURCES_HORDE,    m_teamScores[BG_TEAM_HORDE]);

    // other unknown
    FillInitialWorldState(data, count, 0x745, 0x2);         // 37 1861 unk
}

void BattleGroundAB::_SendNodeUpdate(uint8 node)
{
    // Send node owner state update to refresh map icons on client
    uint8 const plusArray[] = { 0, 2, 3, 0, 1 };

    if (m_prevNodes[node])
        UpdateWorldState(BG_AB_OP_NODESTATES[node] + plusArray[m_prevNodes[node]], 0);
    else
        UpdateWorldState(BG_AB_OP_NODEICONS[node], 0);

    UpdateWorldState(BG_AB_OP_NODESTATES[node] + plusArray[m_nodes[node]], 1);

    // How many bases each team owns
    uint8 ally = 0, horde = 0;
    for (uint8 i = 0; i < BG_AB_NODES_MAX; ++i)
    {
        if (m_nodes[i] == BG_AB_NODE_STATUS_ALLY_OCCUPIED)
            ++ally;
        else if (m_nodes[i] == BG_AB_NODE_STATUS_HORDE_OCCUPIED)
            ++horde;
    }

    UpdateWorldState(BG_AB_OP_OCCUPIED_BASES_ALLY, ally);
    UpdateWorldState(BG_AB_OP_OCCUPIED_BASES_HORDE, horde);
}

void BattleGroundAB::_NodeOccupied(uint8 node, Team team)
{
    uint8 capturedNodes = 0;
    for (uint8 i = 0; i < BG_AB_NODES_MAX; ++i)
    {
        if (m_nodes[i] == GetTeamIndexByTeamId(team) + BG_AB_NODE_TYPE_OCCUPIED && !m_nodeTimers[i])
            ++capturedNodes;
    }
    if (capturedNodes >= 5)
        CastSpellOnTeam(SPELL_AB_QUEST_REWARD_5_BASES, team);
    if (capturedNodes >= 4)
        CastSpellOnTeam(SPELL_AB_QUEST_REWARD_4_BASES, team);
}

/* Invoked if a player used a banner as a gameobject */
void BattleGroundAB::EventPlayerClickedOnFlag(Player* source, GameObject* targetGo)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    uint8 event = (sBattleGroundMgr.GetGameObjectEventIndex(targetGo->GetGUIDLow())).event1;
    if (event >= BG_AB_NODES_MAX)                           // not a node
        return;

    BG_AB_Nodes node = BG_AB_Nodes(event);

    BattleGroundTeamIndex teamIndex = GetTeamIndexByTeamId(source->GetTeam());

    // Check if player really could use this banner, not cheated
    if (!(m_nodes[node] == 0 || teamIndex == m_nodes[node] % 2))
        return;

    uint32 killCredits[BG_AB_NODES_MAX] = {
        BG_AB_NODE_STABLES_CREDIT,
        BG_AB_NODE_BLACKSMITH_CREDIT,
        BG_AB_NODE_FARM_CREDIT,
        BG_AB_NODE_LUMBER_MILL_CREDIT,
        BG_AB_NODE_GOLD_MINE_CREDIT};
    source->KilledMonsterCredit(killCredits[node]);

    uint32 sound = 0;

    // TODO in the following code we should restructure a bit to avoid
    // duplication (or maybe write functions?)
    // If node is neutral, change to contested
    if (m_nodes[node] == BG_AB_NODE_TYPE_NEUTRAL)
    {
        UpdatePlayerScore(source, SCORE_BASES_ASSAULTED, 1);
        m_prevNodes[node] = m_nodes[node];
        m_nodes[node] = teamIndex + 1;
        // create new contested banner
        _CreateBanner(node, BG_AB_NODE_TYPE_CONTESTED, teamIndex);
        _SendNodeUpdate(node);
        m_nodeTimers[node] = BG_AB_FLAG_CAPTURING_TIME;

        if (teamIndex == 0)
            SendMessage2ToAll(LANG_BG_AB_NODE_CLAIMED, CHAT_MSG_BG_SYSTEM_ALLIANCE, source, _GetNodeNameId(node), LANG_BG_ALLY);
        else
            SendMessage2ToAll(LANG_BG_AB_NODE_CLAIMED, CHAT_MSG_BG_SYSTEM_HORDE, source, _GetNodeNameId(node), LANG_BG_HORDE);

        sound = BG_AB_SOUND_NODE_CLAIMED;
    }
    // If node is contested
    else if ((m_nodes[node] == BG_AB_NODE_STATUS_ALLY_CONTESTED) || (m_nodes[node] == BG_AB_NODE_STATUS_HORDE_CONTESTED))
    {
        // If last state is NOT occupied, change node to enemy-contested
        if (m_prevNodes[node] < BG_AB_NODE_TYPE_OCCUPIED)
        {
            UpdatePlayerScore(source, SCORE_BASES_ASSAULTED, 1);
            m_prevNodes[node] = m_nodes[node];
            m_nodes[node] = teamIndex + BG_AB_NODE_TYPE_CONTESTED;
            // create new contested banner
            _CreateBanner(node, BG_AB_NODE_TYPE_CONTESTED, teamIndex);
            _SendNodeUpdate(node);
            m_nodeTimers[node] = BG_AB_FLAG_CAPTURING_TIME;

            if (teamIndex == BG_TEAM_ALLIANCE)
                SendMessage2ToAll(LANG_BG_AB_NODE_ASSAULTED, CHAT_MSG_BG_SYSTEM_ALLIANCE, source, _GetNodeNameId(node));
            else
                SendMessage2ToAll(LANG_BG_AB_NODE_ASSAULTED, CHAT_MSG_BG_SYSTEM_HORDE, source, _GetNodeNameId(node));
        }
        // If contested, change back to occupied
        else
        {
            UpdatePlayerScore(source, SCORE_BASES_DEFENDED, 1);
            m_prevNodes[node] = m_nodes[node];
            m_nodes[node] = teamIndex + BG_AB_NODE_TYPE_OCCUPIED;
            // create new occupied banner
            _CreateBanner(node, BG_AB_NODE_TYPE_OCCUPIED, teamIndex);
            _SendNodeUpdate(node);
            m_nodeTimers[node] = 0;
            _NodeOccupied(node, (teamIndex == BG_TEAM_ALLIANCE) ? ALLIANCE : HORDE);

            if (teamIndex == BG_TEAM_ALLIANCE)
                SendMessage2ToAll(LANG_BG_AB_NODE_DEFENDED, CHAT_MSG_BG_SYSTEM_ALLIANCE, source, _GetNodeNameId(node));
            else
                SendMessage2ToAll(LANG_BG_AB_NODE_DEFENDED, CHAT_MSG_BG_SYSTEM_HORDE, source, _GetNodeNameId(node));
        }
        sound = (teamIndex == BG_TEAM_ALLIANCE) ? BG_AB_SOUND_NODE_ASSAULTED_ALLIANCE : BG_AB_SOUND_NODE_ASSAULTED_HORDE;
    }
    // If node is occupied, change to enemy-contested
    else
    {
        UpdatePlayerScore(source, SCORE_BASES_ASSAULTED, 1);
        m_prevNodes[node] = m_nodes[node];
        m_nodes[node] = teamIndex + BG_AB_NODE_TYPE_CONTESTED;
        // create new contested banner
        _CreateBanner(node, BG_AB_NODE_TYPE_CONTESTED, teamIndex);
        _SendNodeUpdate(node);
        m_nodeTimers[node] = BG_AB_FLAG_CAPTURING_TIME;

        if (teamIndex == BG_TEAM_ALLIANCE)
            SendMessage2ToAll(LANG_BG_AB_NODE_ASSAULTED, CHAT_MSG_BG_SYSTEM_ALLIANCE, source, _GetNodeNameId(node));
        else
            SendMessage2ToAll(LANG_BG_AB_NODE_ASSAULTED, CHAT_MSG_BG_SYSTEM_HORDE, source, _GetNodeNameId(node));

        sound = (teamIndex == BG_TEAM_ALLIANCE) ? BG_AB_SOUND_NODE_ASSAULTED_ALLIANCE : BG_AB_SOUND_NODE_ASSAULTED_HORDE;
    }

    // If node is occupied again, send "X has taken the Y" msg.
    if (m_nodes[node] >= BG_AB_NODE_TYPE_OCCUPIED)
    {
        if (teamIndex == BG_TEAM_ALLIANCE)
            SendMessage2ToAll(LANG_BG_AB_NODE_TAKEN, CHAT_MSG_BG_SYSTEM_ALLIANCE, nullptr, LANG_BG_ALLY, _GetNodeNameId(node));
        else
            SendMessage2ToAll(LANG_BG_AB_NODE_TAKEN, CHAT_MSG_BG_SYSTEM_HORDE, nullptr, LANG_BG_HORDE, _GetNodeNameId(node));
    }
    PlaySoundToAll(sound);
}

bool BattleGroundAB::SetupBattleGround()
{
    //buffs
    for (int i = 0; i < BG_AB_NODES_MAX; ++i)
    {
        if (!AddObject(BG_AB_OBJECT_SPEEDBUFF_STABLES + 3 * i, g_buffEntries[0], BG_AB_BuffPositions[i][0], BG_AB_BuffPositions[i][1], BG_AB_BuffPositions[i][2], BG_AB_BuffPositions[i][3], 0, 0, sin(BG_AB_BuffPositions[i][3] / 2), cos(BG_AB_BuffPositions[i][3] / 2), RESPAWN_ONE_DAY)
                || !AddObject(BG_AB_OBJECT_SPEEDBUFF_STABLES + 3 * i + 1, g_buffEntries[1], BG_AB_BuffPositions[i][0], BG_AB_BuffPositions[i][1], BG_AB_BuffPositions[i][2], BG_AB_BuffPositions[i][3], 0, 0, sin(BG_AB_BuffPositions[i][3] / 2), cos(BG_AB_BuffPositions[i][3] / 2), RESPAWN_ONE_DAY)
                || !AddObject(BG_AB_OBJECT_SPEEDBUFF_STABLES + 3 * i + 2, g_buffEntries[2], BG_AB_BuffPositions[i][0], BG_AB_BuffPositions[i][1], BG_AB_BuffPositions[i][2], BG_AB_BuffPositions[i][3], 0, 0, sin(BG_AB_BuffPositions[i][3] / 2), cos(BG_AB_BuffPositions[i][3] / 2), RESPAWN_ONE_DAY)
           )
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "BatteGroundAB: Failed to spawn buff object!");
    }

    return true;
}

void BattleGroundAB::Reset()
{
    //call parent's class reset
    BattleGround::Reset();

    for (uint8 i = 0; i < BG_TEAMS_COUNT; ++i)
    {
        m_teamScores[i]          = 0;
        m_lastTick[i]            = 0;
        m_honorScoreTics[i]      = 0;
        m_reputationScoreTics[i] = 0;
    }

    m_isInformedNearVictory                 = false;
    bool isBGWeekend = BattleGroundMgr::IsBgWeekend(GetTypeID());
    m_honorTics = (isBGWeekend) ? AB_WEEKEND_HONOR_INTERVAL : AB_NORMAL_HONOR_INTERVAL;
    m_reputationTics = (isBGWeekend) ? GET_AB_WEEKEND_REP_INTERVAL : GET_AB_NORMAL_REP_INTERVAL;

    for (uint8 i = 0; i < BG_AB_NODES_MAX; ++i)
    {
        m_nodes[i] = 0;
        m_prevNodes[i] = 0;
        m_nodeTimers[i] = 0;

        // all nodes owned by neutral team at beginning
        m_activeEvents[i] = BG_AB_NODE_TYPE_NEUTRAL;
    }

}

void BattleGroundAB::EndBattleGround(Team winner)
{
    bool isBGWeekend = BattleGroundMgr::IsBgWeekend(GetTypeID());
    //win reward
    if (winner == ALLIANCE)
    {
        if (isBGWeekend)
            RewardHonorToTeam(BG_AB_WinMatchHonor[GetBracketId()], ALLIANCE);
        RewardHonorToTeam(BG_AB_WinMatchHonor[GetBracketId()], ALLIANCE);
    }
    if (winner == HORDE)
    {
        if (isBGWeekend)
            RewardHonorToTeam(BG_AB_WinMatchHonor[GetBracketId()], HORDE);
        RewardHonorToTeam(BG_AB_WinMatchHonor[GetBracketId()], HORDE);
    }

    BattleGround::EndBattleGround(winner);
}

WorldSafeLocsEntry const* BattleGroundAB::GetClosestGraveYard(Player* player)
{
    // repop players at the entrance GY if BG is not started yet
    if (GetStatus() != STATUS_IN_PROGRESS && !player->IsGameMaster())
    {
        if (WorldSafeLocsEntry const* gEntry = sWorldSafeLocsStore.LookupEntry(player->GetTeam() == ALLIANCE ? 890 : 889))
            return gEntry;
    }

    BattleGroundTeamIndex teamIndex = GetTeamIndexByTeamId(player->GetTeam());

    // Is there any occupied node for this team?
    std::vector<uint8> nodes;
    for (uint8 i = 0; i < BG_AB_NODES_MAX; ++i)
        if (m_nodes[i] == teamIndex + 3)
            nodes.push_back(i);

    WorldSafeLocsEntry const* graveyard = nullptr;
    // If so, select the closest node to place ghost on
    if (!nodes.empty())
    {
        float playerX = player->GetPositionX();
        float playerY = player->GetPositionY();

        float minDist = 999999.0f;
        for (uint8 node : nodes)
        {
            WorldSafeLocsEntry const*entry = sWorldSafeLocsStore.LookupEntry(BG_AB_GraveyardIds[node]);
            if (!entry)
                continue;
            float dist = (entry->x - playerX) * (entry->x - playerX) + (entry->y - playerY) * (entry->y - playerY);
            if (minDist > dist)
            {
                minDist = dist;
                graveyard = entry;
            }
        }
        nodes.clear();
    }
    // If not, place ghost on starting location
    if (!graveyard)
        graveyard = sWorldSafeLocsStore.LookupEntry(BG_AB_GraveyardIds[teamIndex + 5]);

    return graveyard;
}

void BattleGroundAB::UpdatePlayerScore(Player* source, uint32 type, uint32 value)
{
    BattleGroundScoreMap::iterator itr = m_playerScores.find(source->GetObjectGuid());
    if (itr == m_playerScores.end())                          // player not found...
        return;

    switch (type)
    {
        case SCORE_BASES_ASSAULTED:
            ((BattleGroundABScore*)itr->second)->basesAssaulted += value;
            break;
        case SCORE_BASES_DEFENDED:
            ((BattleGroundABScore*)itr->second)->basesDefended += value;
            break;
        default:
            BattleGround::UpdatePlayerScore(source, type, value);
            break;
    }
}
