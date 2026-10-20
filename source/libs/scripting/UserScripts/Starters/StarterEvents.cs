// Event names and payload types the starter scripts publish and subscribe to, so they agree on one shape.

public static class StarterEvents
{
    public const string Collected = "collected";
    public const string ScoreChanged = "scoreChanged";
    public const string MatchWon = "matchWon";
    public const string RoundTick = "roundTick";
    public const string RoundOver = "roundOver";
    public const string RoundStarted = "roundStarted";
    public const string PlayerSpawned = "playerSpawned";
}

public sealed class CollectedEvent
{
    public int Slot { get; init; }
    public int Value { get; init; }
    public string CollectorUuid { get; init; } = "";
}

public sealed class ScoreChangedEvent
{
    public int Slot { get; init; }
    public int Score { get; init; }
}

public sealed class MatchWonEvent
{
    public int Slot { get; init; }
    public int Score { get; init; }
}

public sealed class RoundTickEvent
{
    public int SecondsLeft { get; init; }
}

public sealed class RoundOverEvent
{
    public int Round { get; init; }
}

public sealed class RoundStartedEvent
{
    public int Round { get; init; }
}

public sealed class PlayerSpawnedEvent
{
    public int Slot { get; init; }
    public string Uuid { get; init; } = "";
}
