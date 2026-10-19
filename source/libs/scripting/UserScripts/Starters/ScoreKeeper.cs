// A scene script that totals collected points per player, logs a leaderboard and publishes score and match events.
using System.Collections.Generic;
using System.Linq;
using ScriptBridge;

public class ScoreKeeper : SceneScript
{
    [ExposeToEditor("Event Name")]
    private string m_eventName = StarterEvents.Collected;

    // Zero or less means the match has no target.
    [ExposeToEditor("Target Score")]
    private int m_targetScore = 0;

    [ExposeToEditor("Reset On Round Over")]
    private bool m_resetOnRoundOver = true;

    private readonly SortedDictionary<int, int> m_scores = new();
    private bool m_matchWon;

    public int getScore(int slot) => m_scores.TryGetValue(slot, out var score) ? score : 0;

    public override void start()
    {
        subscribe<CollectedEvent>(m_eventName, onCollected);
        subscribe<RoundOverEvent>(StarterEvents.RoundOver, onRoundOver);
    }

    // A slot's score is dropped without a scoreChanged, so a leaving player's pillar keeps its last height.
    public override void onPlayerLeft(int slot)
    {
        m_scores.Remove(slot);
    }

    // A collected value may be negative; the winner is the highest score, whatever its sign.
    private void onCollected(CollectedEvent collected)
    {
        if (collected == null || collected.Slot < 0)
        {
            return;
        }

        var score = getScore(collected.Slot) + collected.Value;
        m_scores[collected.Slot] = score;
        Log.info(leaderboard());
        publish(StarterEvents.ScoreChanged, new ScoreChangedEvent { Slot = collected.Slot, Score = score });

        if (m_targetScore > 0 && score >= m_targetScore && !m_matchWon)
        {
            m_matchWon = true;
            Log.info($"P{collected.Slot} wins the match with {score}");
            publish(StarterEvents.MatchWon, new MatchWonEvent { Slot = collected.Slot, Score = score });
        }
    }

    private void onRoundOver(RoundOverEvent over)
    {
        Log.info($"Round {over?.Round ?? 0} over: {describeWinner()}");

        if (!m_resetOnRoundOver)
        {
            return;
        }

        m_matchWon = false;
        foreach (var slot in m_scores.Keys.ToList())
        {
            m_scores[slot] = 0;
            publish(StarterEvents.ScoreChanged, new ScoreChangedEvent { Slot = slot, Score = 0 });
        }

        if (m_scores.Count > 0)
        {
            Log.info(leaderboard());
        }
    }

    private string leaderboard()
    {
        return "Scores: " + string.Join(", ", m_scores.Select(entry => $"P{entry.Key} {entry.Value}"));
    }

    private string describeWinner()
    {
        if (m_scores.Values.All(score => score == 0))
        {
            return "no scores";
        }

        var top = m_scores.Values.Max();

        var leaders = m_scores.Where(entry => entry.Value == top).Select(entry => entry.Key).ToList();
        return leaders.Count > 1 ? $"tie at {top}" : $"P{leaders[0]} wins with {top}";
    }
}
