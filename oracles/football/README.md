# Football Oracle Service

Oracle service for querying football (soccer) match data from top 5 European leagues.

## Supported Leagues

- **Premier League** (England) - League ID: 39
- **La Liga** (Spain) - League ID: 140
- **Serie A** (Italy) - League ID: 135
- **Bundesliga** (Germany) - League ID: 78
- **Ligue 1** (France) - League ID: 61

## Features

- Real-time match scores and status
- Team identification using numeric IDs (no string limitations)
- Match status tracking (Not Started, In Progress, Finished, Postponed, Cancelled)
- Elapsed time tracking
- Caching with 60-second TTL
- Rate limiting (2 seconds between API calls)

## Providers

### Mock Provider
- For testing without external API calls
- Pre-configured with sample matches
- Always available
- Perfect for development and testing

### TheSportsDB Provider ⭐ **Recommended**
- Uses thesportsdb.com free public API
- **No authentication required** - perfect for decentralized systems!
- Public test API key "3" works for everyone
- Free forever with no rate limits
- Real match data from all major leagues

## Configuration

### Environment Variables

```bash
# Football service configuration
FOOTBALL_SERVICE_HOST=0.0.0.0
FOOTBALL_SERVICE_PORT=31844

# No API key needed for TheSportsDB!
```

### Why TheSportsDB?

✅ **Truly Decentralized** - No personal API keys needed  
✅ **Permissionless** - Anyone can run the oracle  
✅ **Free Forever** - No rate limits on basic tier  
✅ **No Registration** - Works immediately  
✅ **Public Key** - Everyone uses the same test key "3"

## Building

```bash
cd oracle-machine
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make football_oracle_service
```

## Running

```bash
# Set environment variables
export FOOTBALL_SERVICE_HOST=0.0.0.0
export FOOTBALL_SERVICE_PORT=31844
export APIFOOTBALL_API_KEY=your_key_here  # Optional

# Run service
./build/bin/football_oracle_service --log ./logs/football_service.log
```

## Usage in Smart Contracts

### Query Structure

```cpp
OI::Football::OracleQuery query;
query.oracle = OI::Football::getMockOracleId();  // or getTheSportsDBOracleId()
query.matchId = 1001;                             // Match identifier
query.leagueId = OI::Football::LEAGUE_PREMIER_LEAGUE;
query.season = 2024;
query._reserved = 0;
```

### Reply Structure

```cpp
OI::Football::OracleReply reply;
// reply.homeTeamId - Home team numeric ID
// reply.awayTeamId - Away team numeric ID
// reply.homeScore - Home team score (-1 if not started)
// reply.awayScore - Away team score (-1 if not started)
// reply.status - Match status (0-4)
// reply.elapsedMinutes - Elapsed minutes (0-90+)
```

### Match Status Values

```cpp
0 = NOT_STARTED   // Match hasn't begun
1 = IN_PROGRESS   // Match is currently playing
2 = FINISHED      // Match completed
3 = POSTPONED     // Match postponed
4 = CANCELLED     // Match cancelled
```

### Example Contract Usage

```cpp
// Query a match
PUBLIC_PROCEDURE(QueryMatch)
{
    OI::Football::OracleQuery query;
    using namespace Ch;
    query.oracle = OI::Football::getTheSportsDBOracleId();
    query.matchId = input.matchId;
    query.leagueId = OI::Football::LEAGUE_PREMIER_LEAGUE;
    query.season = 2024;
    
    output.queryId = QUERY_ORACLE(
        OI::Football,
        query,
        NotifyMatchResult,
        60000  // 60 second timeout
    );
}

// Receive result
PRIVATE_PROCEDURE_WITH_LOCALS(NotifyMatchResult)
{
    if (input.status == ORACLE_QUERY_STATUS_SUCCESS)
    {
        // Check if match is finished
        if (input.reply.status == OI::Football::FINISHED)
        {
            // Process final score
            uint32 homeTeam = input.reply.homeTeamId;
            uint32 awayTeam = input.reply.awayTeamId;
            sint32 homeScore = input.reply.homeScore;
            sint32 awayScore = input.reply.awayScore;
            
            // Determine winner and process bets
            if (homeScore > awayScore)
            {
                // Home team won
            }
            else if (awayScore > homeScore)
            {
                // Away team won
            }
            else
            {
                // Draw
            }
        }
    }
}
```

## Team ID Mapping

Since Qubic contracts cannot use strings, teams are identified by numeric IDs from the TheSportsDB database.

### Common Team IDs

**Premier League**:
- 133612 = Manchester United
- 133602 = Liverpool
- 133613 = Manchester City
- 133610 = Chelsea
- 133604 = Arsenal

**La Liga**:
- 133604 = Barcelona
- 133738 = Real Madrid
- 133612 = Atletico Madrid

**Serie A**:
- 133604 = AC Milan
- 133609 = Inter Milan
- 133636 = Juventus

**Bundesliga**:
- 133602 = Bayern Munich
- 133611 = Borussia Dortmund

**Ligue 1**:
- 133612 = Paris Saint-Germain
- 133604 = Marseille

For complete team ID list, see `THESPORTSDB_REFERENCE.md` or visit: https://www.thesportsdb.com/api.php

## Testing

### Test Contract

See `core/src/contracts/TestExampleD.h` for a complete example contract (`FootballTest`) that:
- Queries mock matches automatically
- Handles oracle callbacks
- Logs match results
- Demonstrates proper error handling

### Running Tests

```bash
# Start football service
./build/bin/football_oracle_service

# Start oracle machine node (in another terminal)
./build/bin/oracle_machine_node

# Deploy FootballTest contract to Qubic core
# The contract will automatically query matches every 13 ticks
```

## API Response Format

TheSportsDB returns JSON responses. The service parses:

```json
{
  "events": [{
    "idEvent": "2279631",
    "idHomeTeam": "133738",
    "idAwayTeam": "133604",
    "intHomeScore": "4",
    "intAwayScore": "1",
    "strStatus": "Match Finished"
  }]
}
```

## Testing the API

### Quick Test in Terminal

```powershell
# Get Real Madrid match
Invoke-RestMethod -Uri "https://www.thesportsdb.com/api/v1/json/3/lookupevent.php?id=2279631"

# Search for a team
Invoke-RestMethod -Uri "https://www.thesportsdb.com/api/v1/json/3/searchteams.php?t=Manchester_United"

# Get recent matches for a team
Invoke-RestMethod -Uri "https://www.thesportsdb.com/api/v1/json/3/eventslast.php?id=133612"
```

## Limitations

### TheSportsDB Limitations
- Elapsed minutes are estimated (90 for finished, 45 for in-progress)
- No real-time minute-by-minute updates
- Match IDs differ from other APIs

### Qubic Constraints
- No string support - teams identified by numeric IDs
- Fixed-size data structures
- All data must be deterministic

## Troubleshooting

### Service won't start
- Check port 31844 is not in use
- Verify libcurl is installed
- Check log file for errors

### API requests failing
- Verify internet connectivity
- Check TheSportsDB service status
- Try with a known match ID (e.g., 2279631)
- Test API in terminal first

### No match data returned
- Verify match ID is correct (TheSportsDB IDs, not API-Football IDs)
- Check match exists in database
- Try mock provider first to test setup
- Use recent/historical matches (not future matches)

## Contributing

When adding new features:
1. Follow existing code style
2. Add appropriate error handling
3. Update this README
4. Test with both mock and real providers
5. Ensure deterministic behavior for consensus

## License

Same as parent Qubic project.
