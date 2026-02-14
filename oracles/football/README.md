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

### API-Football Provider
- Uses api-football.com free tier API
- Requires API key (optional for free tier)
- Real-time data from actual matches

## Configuration

### Environment Variables

```bash
# Football service configuration
FOOTBALL_SERVICE_HOST=0.0.0.0
FOOTBALL_SERVICE_PORT=31844

# API-Football API key (optional for free tier)
APIFOOTBALL_API_KEY=your_api_key_here
```

### Getting an API Key

1. Visit [api-football.com](https://www.api-football.com/)
2. Sign up for free tier (100 requests/day)
3. Get your API key from dashboard
4. Set `APIFOOTBALL_API_KEY` environment variable

**Note**: Free tier works without API key but has stricter rate limits.

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
query.oracle = OI::Football::getMockOracleId();  // or getApiFootballOracleId()
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
    query.oracle = OI::Football::getApiFootballOracleId();
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

Since Qubic contracts cannot use strings, teams are identified by numeric IDs from the API-Football database.

### Common Team IDs

**Premier League**:
- 33 = Manchester United
- 40 = Liverpool
- 50 = Manchester City
- 49 = Chelsea
- 42 = Arsenal

**La Liga**:
- 529 = Barcelona
- 541 = Real Madrid
- 530 = Atletico Madrid

**Serie A**:
- 489 = AC Milan
- 505 = Inter Milan
- 496 = Juventus

**Bundesliga**:
- 157 = Bayern Munich
- 165 = Borussia Dortmund

**Ligue 1**:
- 85 = Paris Saint-Germain
- 81 = Marseille

For complete team ID list, visit: https://www.api-football.com/documentation-v3#tag/Teams

## Testing

### Test Contract

See `core/src/contracts/TestExampleD.h` for a complete example contract that:
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

# Deploy TestExampleD contract to Qubic core
# The contract will automatically query matches every 13 ticks
```

## API Response Format

API-Football returns JSON responses. The service parses:

```json
{
  "response": [{
    "fixture": {
      "id": 1001,
      "status": {
        "short": "FT",
        "elapsed": 90
      }
    },
    "teams": {
      "home": {"id": 33},
      "away": {"id": 40}
    },
    "goals": {
      "home": 2,
      "away": 1
    }
  }]
}
```

## Limitations

### Free Tier Limits
- 100 requests per day
- Rate limited to 10 requests per minute
- No historical data (current season only)

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
- Verify API key is correct
- Check rate limits (100/day for free tier)
- Ensure internet connectivity
- Check API-Football service status

### No match data returned
- Verify match ID is correct
- Check match exists in specified league/season
- Try mock provider first to test setup

## Contributing

When adding new features:
1. Follow existing code style
2. Add appropriate error handling
3. Update this README
4. Test with both mock and real providers
5. Ensure deterministic behavior for consensus

## License

Same as parent Qubic project.
