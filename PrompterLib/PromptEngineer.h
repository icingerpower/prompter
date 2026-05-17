#ifndef PROMPTENGINEER_H
#define PROMPTENGINEER_H

#include <atomic>
#include <memory>

#include <QObject>
#include <QPointer>
#include <QStringList>

#include <QCoro/QCoroTask>

class AbstractCli;

// Orchestrates the three-phase prompt reverse-engineering loop:
//
//  Phase 1 — Crafter:   asks the CLI to produce 3 diverse prompt candidates
//                        that should satisfy the user's rules and output criteria.
//  Phase 2 — Runner:    executes each candidate prompt (CLI runs from filesDir
//                        so it can read any input files by name).
//  Phase 3 — Assessor:  asks the CLI to judge whether the output satisfies
//                        the criteria AND whether the prompt is general enough
//                        (not "cheating" by quoting the required output).
//
//  The loop repeats up to maxAttempts times, feeding previous failures back to
//  the crafter so each round tries genuinely different approaches.
class PromptEngineer : public QObject
{
    Q_OBJECT

public:
    explicit PromptEngineer(QObject *parent = nullptr);

    // Start a run. Cancels any in-progress run first.
    void start(AbstractCli *cli,
               const QString &promptInput,
               const QString &neededOutput,
               int maxAttempts,
               const QString &filesDir = {});

    void cancel();
    bool isRunning() const;

signals:
    void log(const QString &message);
    void progressChanged(int attempt, int maxAttempts);
    void candidatePromptChanged(const QString &prompt);
    void candidateReplyChanged(const QString &reply);
    void finished(bool success, const QString &bestPrompt, const QString &bestOutput);

private:
    std::shared_ptr<std::atomic<bool>> m_cancelFlag;
    bool m_running = false;

    static QCoro::Task<void> _doRun(
        QPointer<PromptEngineer> self,
        std::shared_ptr<std::atomic<bool>> cancelled,
        AbstractCli *cli,
        QString promptInput,
        QString neededOutput,
        int maxAttempts,
        QString filesDir);

    // Scans filesDir and returns a context block (file list + text content
    // for files ≤ 8 KB, binary files listed by name only). Total capped at 32 KB.
    static QString _buildFileContext(const QString &filesDir);

    static QString _buildCrafterPrompt(const QString &promptInput,
                                        const QString &neededOutput,
                                        const QString &fileContext,
                                        const QStringList &previousFailures);

    static QString _buildAssessorPrompt(const QString &candidatePrompt,
                                         const QString &runnerOutput,
                                         const QString &neededOutput);

    // Parse the structured crafter output into individual prompt strings.
    static QStringList _parseCraftedPrompts(const QString &crafterOutput);

    struct Assessment {
        bool    satisfies = false;
        bool    isGeneral = false;
        QString explanation;
    };
    static Assessment _parseAssessment(const QString &assessorOutput);
};

#endif // PROMPTENGINEER_H
