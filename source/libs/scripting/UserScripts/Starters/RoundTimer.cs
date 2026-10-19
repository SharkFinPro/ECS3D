// A scene script that counts rounds down once a second with every(), publishing tick, over and started events.
using System;
using ScriptBridge;

public class RoundTimer : SceneScript
{
    [ExposeToEditor("Round Seconds")]
    private int m_roundSeconds = 90;

    [ExposeToEditor("Intermission Seconds")]
    private float m_intermissionSeconds = 5.0f;

    [ExposeToEditor("Auto Restart")]
    private bool m_autoRestart = true;

    [ExposeToEditor("Start On Load")]
    private bool m_startOnLoad = true;

    private TimerHandle? m_tick;
    private int m_round;
    private int m_secondsLeft;
    private bool m_warnedInvalid;
    private bool m_waiting;

    // Starts the next round when none is counting down or waiting out an intermission. For Start On Load off.
    public void beginRound()
    {
        if (m_tick == null && !m_waiting)
        {
            startRound(m_round + 1);
        }
    }

    // start() rather than onSceneLoaded, which a script started mid-run never receives. The first round
    // publishes no roundStarted, since subscribers may not have started yet.
    public override void start()
    {
        if (m_startOnLoad)
        {
            startRound(1);
        }
    }

    private void startRound(int round)
    {
        m_waiting = false;
        if (m_roundSeconds <= 0)
        {
            if (!m_warnedInvalid)
            {
                m_warnedInvalid = true;
                Log.warn($"RoundTimer: Round Seconds is {m_roundSeconds}; it must be above zero, so no round runs.");
            }

            return;
        }

        m_round = round;
        m_secondsLeft = m_roundSeconds;
        Log.info($"Round {round} started: {m_roundSeconds}s");
        m_tick = every(1.0f, onTick);
    }

    private void onTick()
    {
        m_secondsLeft--;
        publish(StarterEvents.RoundTick, new RoundTickEvent { SecondsLeft = m_secondsLeft });

        if (m_secondsLeft > 0)
        {
            if (shouldLog(m_secondsLeft))
            {
                Log.info($"Round {m_round}: {m_secondsLeft}s left");
            }

            return;
        }

        m_tick?.cancel();
        m_tick = null;
        Log.info($"Round {m_round} over");
        publish(StarterEvents.RoundOver, new RoundOverEvent { Round = m_round });

        if (!m_autoRestart)
        {
            return;
        }

        var next = m_round + 1;
        m_waiting = true;
        after(Math.Max(0.0f, m_intermissionSeconds), () =>
        {
            publish(StarterEvents.RoundStarted, new RoundStartedEvent { Round = next });
            startRound(next);
        });
    }

    private static bool shouldLog(int secondsLeft)
    {
        return secondsLeft == 60 || secondsLeft == 30 || secondsLeft == 10 || (secondsLeft >= 1 && secondsLeft <= 5);
    }
}
