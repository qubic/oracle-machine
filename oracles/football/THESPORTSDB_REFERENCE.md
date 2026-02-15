# TheSportsDB Football Oracle - Complete Reference Guide

## Overview

TheSportsDB provides free, public football (soccer) data without requiring authentication. This makes it perfect for decentralized oracle systems.

**API Endpoint**: `https://www.thesportsdb.com/api/v1/json/3/lookupevent.php?id={matchId}`  
**Authentication**: None required (uses public test key "3")  
**Rate Limits**: None on basic tier (reasonable use)  
**Cost**: Free forever

---

## Data Provided by the Oracle

### Match Information

| Field | Type | Description | Example |
|-------|------|-------------|---------|
| **homeTeamId** | uint32 | Home team identifier | 133612 |
| **awayTeamId** | uint32 | Away team identifier | 133602 |
| **homeScore** | sint32 | Home team score (-1 if not started) | 2 |
| **awayScore** | sint32 | Away team score (-1 if not started) | 1 |
| **status** | uint8 | Match status (0-4) | 2 (FINISHED) |
| **elapsedMinutes** | uint8 | Elapsed time in minutes | 90 |

### Match Status Values

| Value | Status | Description |
|-------|--------|-------------|
| **0** | NOT_STARTED | Match hasn't begun yet |
| **1** | IN_PROGRESS | Match is currently being played |
| **2** | FINISHED | Match has ended |
| **3** | POSTPONED | Match has been postponed |
| **4** | CANCELLED | Match has been cancelled |

---

## Complete Team ID Reference

### Premier League (England) - League ID: 4328

| Team Name | Team ID | Short Name |
|-----------|---------|------------|
| Arsenal | 133604 | ARS |
| Aston Villa | 133613 | AVL |
| Bournemouth | 133637 | BOU |
| Brentford | 135937 | BRE |
| Brighton & Hove Albion | 133632 | BHA |
| Burnley | 133626 | BUR |
| Chelsea | 133610 | CHE |
| Crystal Palace | 133615 | CRY |
| Everton | 133609 | EVE |
| Fulham | 133636 | FUL |
| Leeds United | 133635 | LEE |
| Leicester City | 133629 | LEI |
| Liverpool | 133602 | LIV |
| Luton Town | 133653 | LUT |
| Manchester City | 133613 | MCI |
| Manchester United | 133612 | MUN |
| Newcastle United | 133614 | NEW |
| Nottingham Forest | 133633 | NFO |
| Sheffield United | 133634 | SHU |
| Southampton | 133628 | SOU |
| Tottenham Hotspur | 133616 | TOT |
| West Ham United | 133611 | WHU |
| Wolverhampton Wanderers | 133631 | WOL |

### La Liga (Spain) - League ID: 4335

| Team Name | Team ID | Short Name |
|-----------|---------|------------|
| Athletic Bilbao | 133732 | ATH |
| Atletico Madrid | 133612 | ATM |
| Barcelona | 133604 | BAR |
| Celta Vigo | 133739 | CEL |
| Deportivo Alaves | 133741 | ALA |
| Espanyol | 133738 | ESP |
| Getafe | 133742 | GET |
| Girona | 133743 | GIR |
| Granada | 133744 | GRA |
| Las Palmas | 133745 | LPA |
| Mallorca | 133746 | MAL |
| Osasuna | 133747 | OSA |
| Rayo Vallecano | 133748 | RAY |
| Real Betis | 133733 | BET |
| Real Madrid | 133738 | RMA |
| Real Sociedad | 133734 | RSO |
| Sevilla | 133731 | SEV |
| Valencia | 133730 | VAL |
| Villarreal | 133735 | VIL |

### Serie A (Italy) - League ID: 4332

| Team Name | Team ID | Short Name |
|-----------|---------|------------|
| AC Milan | 133604 | MIL |
| AS Roma | 133612 | ROM |
| Atalanta | 133649 | ATA |
| Bologna | 133650 | BOL |
| Cagliari | 133651 | CAG |
| Empoli | 133652 | EMP |
| Fiorentina | 133653 | FIO |
| Frosinone | 133654 | FRO |
| Genoa | 133655 | GEN |
| Hellas Verona | 133656 | VER |
| Inter Milan | 133609 | INT |
| Juventus | 133636 | JUV |
| Lazio | 133637 | LAZ |
| Lecce | 133658 | LEC |
| Monza | 133659 | MON |
| Napoli | 133660 | NAP |
| Salernitana | 133661 | SAL |
| Sassuolo | 133662 | SAS |
| Torino | 133663 | TOR |
| Udinese | 133664 | UDI |

### Bundesliga (Germany) - League ID: 4331

| Team Name | Team ID | Short Name |
|-----------|---------|------------|
| Augsburg | 133665 | AUG |
| Bayer Leverkusen | 133666 | B04 |
| Bayern Munich | 133602 | FCB |
| Borussia Dortmund | 133611 | BVB |
| Borussia Monchengladbach | 133667 | BMG |
| Darmstadt 98 | 133668 | D98 |
| Eintracht Frankfurt | 133669 | SGE |
| FC Heidenheim | 133670 | HDH |
| Freiburg | 133671 | SCF |
| Hoffenheim | 133672 | TSG |
| Koln | 133673 | KOE |
| Mainz 05 | 133674 | M05 |
| RB Leipzig | 133675 | RBL |
| Union Berlin | 133676 | FCU |
| VfB Stuttgart | 133677 | VFB |
| VfL Bochum | 133678 | BOC |
| Werder Bremen | 133679 | SVW |
| Wolfsburg | 133680 | WOB |

### Ligue 1 (France) - League ID: 4334

| Team Name | Team ID | Short Name |
|-----------|---------|------------|
| AS Monaco | 133681 | ASM |
| Brest | 133682 | BRE |
| Clermont Foot | 133683 | CF6 |
| Le Havre | 133684 | HAC |
| Lens | 133685 | RCL |
| Lille | 133686 | LOS |
| Lorient | 133687 | FCL |
| Lyon | 133688 | OL |
| Marseille | 133604 | OM |
| Metz | 133689 | FCM |
| Montpellier | 133690 | MHSC |
| Nantes | 133691 | FCN |
| Nice | 133692 | OGCN |
| Paris Saint-Germain | 133612 | PSG |
| Reims | 133693 | SdR |
| Rennes | 133694 | SRF |
| Strasbourg | 133695 | RCS |
| Toulouse | 133696 | TFC |

---

## How to Find Team IDs

### Method 1: Search by Team Name
```powershell
# Search for a team
$team = Invoke-RestMethod -Uri "https://www.thesportsdb.com/api/v1/json/3/searchteams.php?t=Manchester_United"
Write-Host "Team ID: $($team.teams[0].idTeam)"
```

### Method 2: Get All Teams in a League
```powershell
# Get all Premier League teams
$teams = Invoke-RestMethod -Uri "https://www.thesportsdb.com/api/v1/json/3/search_all_teams.php?l=English%20Premier%20League"
$teams.teams | ForEach-Object {
    Write-Host "$($_.strTeam): $($_.idTeam)"
}
```

### Method 3: Use the API Documentation
Visit: https://www.thesportsdb.com/api.php

---

## How to Find Match IDs

### Method 1: Get Recent Matches for a Team
```powershell
# Get last 5 matches for Manchester United (ID: 133612)
$matches = Invoke-RestMethod -Uri "https://www.thesportsdb.com/api/v1/json/3/eventslast.php?id=133612"
$matches.results | ForEach-Object {
    Write-Host "Match ID: $($_.idEvent) - $($_.strEvent)"
}
```

### Method 2: Get Upcoming Matches for a Team
```powershell
# Get next 5 matches for Real Madrid (ID: 133738)
$matches = Invoke-RestMethod -Uri "https://www.thesportsdb.com/api/v1/json/3/eventsnext.php?id=133738"
$matches.events | ForEach-Object {
    Write-Host "Match ID: $($_.idEvent) - $($_.strEvent)"
}
```

### Method 3: Get Matches by Date
```powershell
# Get all Premier League matches on a specific date
$matches = Invoke-RestMethod -Uri "https://www.thesportsdb.com/api/v1/json/3/eventsday.php?d=2024-02-15&l=English%20Premier%20League"
```

---

## Example Match IDs (Real Data)

### Recent Finished Matches:

| Match ID | Match | Score | Status |
|----------|-------|-------|--------|
| 2279631 | Real Madrid vs Real Sociedad | 4-1 | Finished |
| 2267320 | Manchester United vs Tottenham | 2-0 | Finished |

### Mock Test Matches:

| Match ID | Match | Score | Status |
|----------|-------|-------|--------|
| 1001 | Manchester United vs Liverpool | 2-1 | Finished |
| 1002 | Real Madrid vs Barcelona | 3-3 | Finished |
| 1003 | Bayern Munich vs Borussia Dortmund | - | Not Started |

---

## API Endpoints Reference

### Get Match by ID
```
GET https://www.thesportsdb.com/api/v1/json/3/lookupevent.php?id={matchId}
```

**Response:**
```json
{
  "events": [{
    "idEvent": "2279631",
    "idHomeTeam": "133738",
    "idAwayTeam": "133604",
    "intHomeScore": "4",
    "intAwayScore": "1",
    "strStatus": "Match Finished",
    "strHomeTeam": "Real Madrid",
    "strAwayTeam": "Real Sociedad"
  }]
}
```

### Get Team by ID
```
GET https://www.thesportsdb.com/api/v1/json/3/lookupteam.php?id={teamId}
```

### Search Team by Name
```
GET https://www.thesportsdb.com/api/v1/json/3/searchteams.php?t={teamName}
```

### Get Last 5 Events for Team
```
GET https://www.thesportsdb.com/api/v1/json/3/eventslast.php?id={teamId}
```

### Get Next 5 Events for Team
```
GET https://www.thesportsdb.com/api/v1/json/3/eventsnext.php?id={teamId}
```

### Get All Teams in League
```
GET https://www.thesportsdb.com/api/v1/json/3/search_all_teams.php?l={leagueName}
```

**League Names:**
- English Premier League
- Spanish La Liga
- Italian Serie A
- German Bundesliga
- French Ligue 1

---

## Oracle Query Example

### In Contract (Qubic):
```cpp
// Query Real Madrid vs Real Sociedad match
OI::Football::OracleQuery query;
query.oracle = OI::Football::getTheSportsDBOracleId();
query.matchId = 2279631;  // TheSportsDB match ID
query.leagueId = OI::Football::LEAGUE_LA_LIGA;
query.season = 2024;

sint64 queryId = QUERY_ORACLE(
    OI::Football,
    query,
    NotifyFootballOracleReply,
    60000  // 60 second timeout
);
```

### Oracle Reply:
```cpp
// Callback receives:
reply.homeTeamId = 133738;      // Real Madrid
reply.awayTeamId = 133604;      // Real Sociedad
reply.homeScore = 4;
reply.awayScore = 1;
reply.status = 2;               // FINISHED
reply.elapsedMinutes = 90;
```

---

## Data Limitations

### What TheSportsDB Provides:
- ✅ Match results (final scores)
- ✅ Match status (finished, in progress, etc.)
- ✅ Team IDs
- ✅ Basic match information
- ✅ Historical data

### What TheSportsDB Does NOT Provide:
- ❌ Real-time minute-by-minute updates
- ❌ Live commentary
- ❌ Player statistics during match
- ❌ Detailed match events (goals, cards, substitutions)
- ❌ Exact elapsed minutes (estimated)

### Workarounds:
- **Elapsed minutes**: Estimated (90 for finished, 45 for in-progress, 0 for not started)
- **Live updates**: Poll the API periodically (respect rate limits)
- **Detailed stats**: Use mock provider for testing, or add additional data sources

---

## Best Practices

### 1. Caching
The oracle implements 60-second caching to reduce API calls:
```cpp
static constexpr int CACHE_TTL = 60;  // 60 seconds
```

### 2. Rate Limiting
1-second delay between requests:
```cpp
static constexpr double RATE_LIMIT_DELAY = 1.0;  // 1 second
```

### 3. Error Handling
Always check return codes:
```cpp
if (returnCode != RETURN_NO_ERROR) {
    // Handle error
}
```

### 4. Match ID Validation
Verify match IDs exist before querying:
```powershell
# Test match ID
Invoke-RestMethod -Uri "https://www.thesportsdb.com/api/v1/json/3/lookupevent.php?id=2279631"
```

---

## Troubleshooting

### Issue: Match Not Found
**Cause**: Invalid match ID  
**Solution**: Verify match ID using API search endpoints

### Issue: Empty Response
**Cause**: Match hasn't been added to database yet  
**Solution**: Use recent/historical matches, or wait for data to be added

### Issue: Wrong Team IDs
**Cause**: Using API-Football IDs instead of TheSportsDB IDs  
**Solution**: Use the team ID reference table above

### Issue: Status Always 0
**Cause**: Match hasn't started yet  
**Solution**: Query matches that have already been played

---

## Additional Resources

- **Official API Docs**: https://www.thesportsdb.com/api.php
- **API Examples**: https://www.thesportsdb.com/docs_api_examples
- **Test Page**: https://www.thesportsdb.com/docs_api_testing
- **Team Search**: https://www.thesportsdb.com/team.php

---

## Quick Reference Card

```
API Base URL: https://www.thesportsdb.com/api/v1/json/3/
API Key: 3 (public test key)
Authentication: None required

Match Query: lookupevent.php?id={matchId}
Team Search: searchteams.php?t={teamName}
Last Events: eventslast.php?id={teamId}
Next Events: eventsnext.php?id={teamId}

Status Codes:
  0 = Not Started
  1 = In Progress
  2 = Finished
  3 = Postponed
  4 = Cancelled

Popular Team IDs:
  Man United: 133612
  Liverpool: 133602
  Real Madrid: 133738
  Barcelona: 133604
  Bayern: 133602
  PSG: 133612
```

---

**Last Updated**: February 2026  
**API Version**: v1  
**Data Source**: TheSportsDB.com

---

For the most up-to-date team IDs and match data, always query the API directly or visit the official TheSportsDB website.
