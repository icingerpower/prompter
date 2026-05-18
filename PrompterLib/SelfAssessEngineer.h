#ifndef SELFASSESSENGINEER_H
#define SELFASSESSENGINEER_H

#include <atomic>
#include <memory>

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <QCoro/QCoroTask>

class AbstractCli;

// Orchestrates an open-ended prompt exploration loop that scores every result
// instead of stopping at the first pass.
//
// Workflow per attempt cycle:
//   Phase 1 — Crafter:  asks the CLI to craft PROMPTS_PER_CYCLE candidates
//                        aimed at maximising the score on the given criteria.
//   Phase 2 — Runner:   executes each candidate (from filesDir so it can read
//                        any input files).
//   Phase 3 — Assessor: asks the CLI to score the output 1–5 using the
//                        user-supplied assessment criteria.
//
// All results are emitted via resultReady() regardless of score.
// The crafter is fed the best and worst previous scores each cycle so it can
// build on successful patterns and avoid dead ends.
//
// Suggested PROMPTS_PER_CYCLE: 4 gives good variety per round while keeping
// the total number of CLI calls manageable. Raise to 5–6 for broader
// exploration when maxAttempts is small.
class SelfAssessEngineer : public QObject
{
    Q_OBJECT

public:
    explicit SelfAssessEngineer(QObject *parent = nullptr);

    // Starts a run. Cancels any in-progress run first.
    void start(AbstractCli *cli,
               const QString &promptInput,
               const QString &assessmentCriteria,
               int maxAttempts,
               const QString &filesDir = {});

    void cancel();
    bool isRunning() const;

signals:
    void log(const QString &message);
    void progressChanged(int attempt, int maxAttempts);
    // Fired as soon as a candidate has been run AND scored (including failures).
    void resultReady(int index,
                     const QString &prompt,
                     const QString &output,
                     double score,
                     const QString &explanation);
    void finished();

private:
    std::shared_ptr<std::atomic<bool>> m_cancelFlag;
    bool m_running = false;

    struct ScoredResult {
        QString prompt;
        double  score       = 0.0;
        QString explanation;
    };

    static QCoro::Task<void> _doRun(
        QPointer<SelfAssessEngineer> self,
        std::shared_ptr<std::atomic<bool>> cancelled,
        AbstractCli *cli,
        QString promptInput,
        QString assessmentCriteria,
        int maxAttempts,
        QString filesDir);

    static QString    _buildFileContext(const QString &filesDir);
    static QString    _buildCrafterPrompt(const QString &promptInput,
                                           const QString &assessmentCriteria,
                                           const QString &fileContext,
                                           const QList<ScoredResult> &previousResults);
    static QString    _buildAssessorPrompt(const QString &candidatePrompt,
                                            const QString &runnerOutput,
                                            const QString &assessmentCriteria);
    static QStringList _parseCraftedPrompts(const QString &crafterOutput);

    struct Assessment {
        bool    cheated     = false;
        double  score       = 0.0;
        QString explanation;
        QString details;
    };
    static Assessment _parseAssessment(const QString &assessorOutput);
};

#endif // SELFASSESSENGINEER_H
