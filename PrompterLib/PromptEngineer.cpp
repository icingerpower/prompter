#include "PromptEngineer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>

#include "aicli/AbstractCli.h"

static constexpr int PROMPTS_PER_CYCLE = 3;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

PromptEngineer::PromptEngineer(QObject *parent)
    : QObject(parent)
{}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void PromptEngineer::start(AbstractCli *cli,
                            const QString &promptInput,
                            const QString &neededOutput,
                            int maxAttempts,
                            const QString &filesDir)
{
    cancel();
    m_cancelFlag = std::make_shared<std::atomic<bool>>(false);
    m_running = true;
    _doRun(QPointer<PromptEngineer>(this), m_cancelFlag,
           cli, promptInput, neededOutput, maxAttempts, filesDir);
}

void PromptEngineer::cancel()
{
    if (m_cancelFlag) {
        m_cancelFlag->store(true);
    }
    m_running = false;
}

bool PromptEngineer::isRunning() const
{
    return m_running;
}

// ---------------------------------------------------------------------------
// Main coroutine loop
// ---------------------------------------------------------------------------

QCoro::Task<void> PromptEngineer::_doRun(
    QPointer<PromptEngineer> self,
    std::shared_ptr<std::atomic<bool>> cancelled,
    AbstractCli *cli,
    QString promptInput,
    QString neededOutput,
    int maxAttempts,
    QString filesDir)
{
    auto done = [&]() { return cancelled->load() || !self; };
    auto log  = [&](const QString &msg) { if (self) emit self->log(msg); };

    QStringList previousFailures;

    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        if (done()) co_return;

        log(QStringLiteral("══════ Attempt %1 / %2 ══════").arg(attempt + 1).arg(maxAttempts));
        if (self) emit self->progressChanged(attempt, maxAttempts);

        // ── Phase 1: craft PROMPTS_PER_CYCLE candidates ──────────────────
        log(QStringLiteral("Phase 1 — Crafting %1 prompt candidates…").arg(PROMPTS_PER_CYCLE));

        const QString fileContext  = _buildFileContext(filesDir);
        const QString crafterMeta = _buildCrafterPrompt(
            promptInput, neededOutput, fileContext, previousFailures);

        const CliRunResult crafterResult = co_await cli->runPrompt(crafterMeta);
        if (done()) co_return;

        if (!crafterResult.processStarted) {
            log(QStringLiteral("ERROR: CLI failed to start. Aborting."));
            break;
        }
        if (crafterResult.exitCode != 0) {
            log(QStringLiteral("WARNING: Crafter exited with code %1.").arg(crafterResult.exitCode));
        }

        const QStringList candidates = _parseCraftedPrompts(crafterResult.output);
        if (candidates.isEmpty()) {
            log(QStringLiteral("ERROR: Could not parse any prompt candidates.\nRaw output:\n")
                + crafterResult.output.left(600));
            continue;
        }
        log(QStringLiteral("  Parsed %1 candidate(s).").arg(candidates.size()));

        // ── Phase 2 + 3: run then assess each candidate ───────────────────
        for (int ci = 0; ci < candidates.size(); ++ci) {
            if (done()) co_return;

            const QString &candidate = candidates[ci];
            log(QStringLiteral("  ┌─ Candidate %1 / %2").arg(ci + 1).arg(candidates.size()));
            log(QStringLiteral("  │  Prompt: %1%2")
                .arg(candidate.left(150))
                .arg(candidate.size() > 150 ? "…" : ""));

            if (self) emit self->candidatePromptChanged(candidate);

            // Phase 2 — run the candidate (in files dir so it can read input files)
            log(QStringLiteral("  │  Phase 2 — Running prompt…"));
            const CliRunResult runResult = co_await cli->runPrompt(candidate, filesDir);
            if (done()) co_return;

            if (!runResult.processStarted) {
                log(QStringLiteral("  │  ERROR: runner CLI failed to start."));
                previousFailures << QStringLiteral("Candidate: %1\nError: CLI failed to start.").arg(candidate);
                log(QStringLiteral("  └─ skipped (runner error)"));
                continue;
            }

            if (self) emit self->candidateReplyChanged(runResult.output);

            const QString outputExcerpt =
                runResult.output.left(300) + (runResult.output.size() > 300 ? "…" : "");
            log(QStringLiteral("  │  Output (%1 chars): %2").arg(runResult.output.size()).arg(outputExcerpt));

            // Phase 3 — assess
            log(QStringLiteral("  │  Phase 3 — Assessing output…"));
            const QString assessorMeta =
                _buildAssessorPrompt(candidate, runResult.output, neededOutput);

            const CliRunResult assessResult = co_await cli->runPrompt(assessorMeta);
            if (done()) co_return;

            const Assessment a = _parseAssessment(assessResult.output);
            log(QStringLiteral("  │  Satisfies: %1  |  General: %2  |  %3")
                .arg(a.satisfies ? "YES" : "NO")
                .arg(a.isGeneral ? "YES" : "NO")
                .arg(a.explanation));

            if (a.satisfies && a.isGeneral) {
                log(QStringLiteral("  └─ ✓ SUCCESS after %1 attempt(s)!").arg(attempt + 1));
                if (self) {
                    emit self->progressChanged(maxAttempts, maxAttempts);
                    emit self->finished(true, candidate, runResult.output);
                    self->m_running = false;
                }
                co_return;
            }

            previousFailures << QStringLiteral(
                "Candidate prompt:\n%1\n\nOutput excerpt:\n%2\n\nWhy it failed: %3")
                .arg(candidate)
                .arg(runResult.output.left(400))
                .arg(a.explanation);

            log(QStringLiteral("  └─ not valid, added to failure history"));
        }

        if (self) emit self->progressChanged(attempt + 1, maxAttempts);
    }

    log(QStringLiteral("✗ FAILED: %1 attempt cycle(s) exhausted without a valid prompt.").arg(maxAttempts));
    if (self) {
        emit self->progressChanged(maxAttempts, maxAttempts);
        emit self->finished(false, {}, {});
        self->m_running = false;
    }
}

// ---------------------------------------------------------------------------
// File context builder
// ---------------------------------------------------------------------------

QString PromptEngineer::_buildFileContext(const QString &filesDir)
{
    if (filesDir.isEmpty()) {
        return {};
    }
    const QDir dir(filesDir);
    if (!dir.exists()) {
        return {};
    }
    const QFileInfoList files =
        dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    if (files.isEmpty()) {
        return {};
    }

    constexpr qint64 kMaxPerFile  = 8  * 1024;
    constexpr qint64 kMaxTotal    = 32 * 1024;
    qint64 totalBytes = 0;
    QString ctx;

    for (const QFileInfo &fi : files) {
        if (totalBytes >= kMaxTotal) {
            ctx += QStringLiteral("\n[…additional files omitted — 32 KB context limit reached]\n");
            break;
        }
        QFile f(fi.filePath());
        if (!f.open(QIODevice::ReadOnly)) {
            ctx += QStringLiteral("\n[%1 — unreadable]\n").arg(fi.fileName());
            continue;
        }
        const QByteArray raw = f.read(kMaxPerFile);
        f.close();

        // Heuristic: no null bytes in first 1 KB → treat as text.
        if (raw.left(1024).contains('\0')) {
            ctx += QStringLiteral("\n[%1 — binary, %2 bytes]\n")
                       .arg(fi.fileName()).arg(fi.size());
        } else {
            ctx += QStringLiteral("\n--- %1 ---\n").arg(fi.fileName());
            ctx += QString::fromUtf8(raw);
            if (fi.size() > kMaxPerFile) {
                ctx += QStringLiteral("\n[…truncated at 8 KB]\n");
            }
        }
        totalBytes += raw.size();
    }
    return ctx.trimmed();
}

// ---------------------------------------------------------------------------
// Prompt builders
// ---------------------------------------------------------------------------

QString PromptEngineer::_buildCrafterPrompt(const QString &promptInput,
                                              const QString &neededOutput,
                                              const QString &fileContext,
                                              const QStringList &previousFailures)
{
    QString p;
    p += QStringLiteral(
        "You are an expert AI prompt engineer. Your task is to craft %1 prompt candidates.\n"
        "Each candidate, when sent to an AI assistant, must:\n"
        "  1. Follow these rules / requirements:\n\n%2\n\n"
        "  2. Produce output that satisfies:\n\n%3\n\n"
        "CRITICAL: The crafted prompts must NOT directly quote or mention the specific items "
        "listed in the output requirement. They must achieve the result naturally through "
        "good prompt design, not by explicitly asking for those items.\n"
        "Each candidate must take a genuinely different angle or approach.\n")
        .arg(PROMPTS_PER_CYCLE)
        .arg(promptInput)
        .arg(neededOutput);

    if (!fileContext.isEmpty()) {
        p += QStringLiteral(
            "\nAVAILABLE FILES: The AI that runs your prompt will have the following files "
            "in its working directory. Your prompts may instruct it to read and use them:\n\n%1\n")
            .arg(fileContext);
    }

    if (!previousFailures.isEmpty()) {
        // Keep the last 5 failures to stay within reasonable context size.
        const QStringList recent = previousFailures.mid(
            qMax(0, previousFailures.size() - 5));
        p += QStringLiteral("\nPREVIOUS FAILED ATTEMPTS — learn from these and try "
                            "completely different approaches:\n\n");
        for (int i = 0; i < recent.size(); ++i) {
            p += QStringLiteral("--- Failure %1 ---\n%2\n\n").arg(i + 1).arg(recent[i]);
        }
    }

    p += QStringLiteral(
        "\nRespond EXACTLY in the following format (include every marker):\n"
        "---PROMPT_1---\n[first prompt text]\n"
        "---PROMPT_2---\n[second prompt text]\n"
        "---PROMPT_3---\n[third prompt text]\n"
        "---END---");
    return p;
}

QString PromptEngineer::_buildAssessorPrompt(const QString &candidatePrompt,
                                               const QString &runnerOutput,
                                               const QString &neededOutput)
{
    return QStringLiteral(
        "You are a strict evaluator. Assess whether an AI output satisfies specific criteria.\n\n"
        "OUTPUT CRITERIA (what the response must contain or satisfy):\n%1\n\n"
        "PROMPT THAT WAS EXECUTED:\n%2\n\n"
        "AI RESPONSE RECEIVED:\n%3\n\n"
        "Evaluate:\n"
        "1. Does the response satisfy the output criteria?\n"
        "2. Is the prompt GENERAL? (A prompt 'cheats' if it directly quotes or names "
        "the specific items from the criteria — it must get the result naturally.)\n\n"
        "Reply EXACTLY in this format (one line each, no extra text before or after):\n"
        "SATISFIES_REQUIREMENTS: YES or NO\n"
        "PROMPT_IS_GENERAL: YES or NO\n"
        "EXPLANATION: [one concise sentence]")
        .arg(neededOutput)
        .arg(candidatePrompt)
        .arg(runnerOutput.left(4000)); // cap to avoid hitting context limits
}

// ---------------------------------------------------------------------------
// Parsers
// ---------------------------------------------------------------------------

QStringList PromptEngineer::_parseCraftedPrompts(const QString &crafterOutput)
{
    static const QStringList kMarkers = {
        QStringLiteral("---PROMPT_1---"),
        QStringLiteral("---PROMPT_2---"),
        QStringLiteral("---PROMPT_3---"),
        QStringLiteral("---END---"),
    };

    QStringList result;
    for (int i = 0; i < PROMPTS_PER_CYCLE; ++i) {
        const int start = crafterOutput.indexOf(kMarkers[i]);
        const int end   = crafterOutput.indexOf(kMarkers[i + 1]);
        if (start < 0 || end < 0) {
            break;
        }
        const QString prompt =
            crafterOutput.mid(start + kMarkers[i].length(), end - start - kMarkers[i].length())
                         .trimmed();
        if (!prompt.isEmpty()) {
            result << prompt;
        }
    }
    return result;
}

PromptEngineer::Assessment PromptEngineer::_parseAssessment(const QString &assessorOutput)
{
    Assessment a;
    for (const QString &raw : assessorOutput.split(u'\n')) {
        const QString line = raw.trimmed();
        if (line.startsWith(QStringLiteral("SATISFIES_REQUIREMENTS:"))) {
            a.satisfies = line.contains(QStringLiteral("YES"), Qt::CaseInsensitive);
        } else if (line.startsWith(QStringLiteral("PROMPT_IS_GENERAL:"))) {
            a.isGeneral = line.contains(QStringLiteral("YES"), Qt::CaseInsensitive);
        } else if (line.startsWith(QStringLiteral("EXPLANATION:"))) {
            a.explanation = line.mid(line.indexOf(u':') + 1).trimmed();
        }
    }
    return a;
}
