#include "SelfAssessEngineer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>

#include "aicli/AbstractCli.h"

static constexpr int PROMPTS_PER_CYCLE = 4;

// ---------------------------------------------------------------------------
// Construction / public API
// ---------------------------------------------------------------------------

SelfAssessEngineer::SelfAssessEngineer(QObject *parent)
    : QObject(parent)
{}

void SelfAssessEngineer::start(AbstractCli *cli,
                                const QString &promptInput,
                                const QString &assessmentCriteria,
                                int maxAttempts,
                                const QString &filesDir)
{
    cancel();
    m_cancelFlag = std::make_shared<std::atomic<bool>>(false);
    m_running    = true;
    _doRun(QPointer<SelfAssessEngineer>(this), m_cancelFlag,
           cli, promptInput, assessmentCriteria, maxAttempts, filesDir);
}

void SelfAssessEngineer::cancel()
{
    if (m_cancelFlag) {
        m_cancelFlag->store(true);
    }
    m_running = false;
}

bool SelfAssessEngineer::isRunning() const
{
    return m_running;
}

// ---------------------------------------------------------------------------
// Main coroutine
// ---------------------------------------------------------------------------

QCoro::Task<void> SelfAssessEngineer::_doRun(
    QPointer<SelfAssessEngineer> self,
    std::shared_ptr<std::atomic<bool>> cancelled,
    AbstractCli *cli,
    QString promptInput,
    QString assessmentCriteria,
    int maxAttempts,
    QString filesDir)
{
    auto done = [&]() { return cancelled->load() || !self; };
    auto log  = [&](const QString &msg) { if (self) emit self->log(msg); };

    QList<ScoredResult> previousResults;
    int globalIndex = 0;

    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        if (done()) co_return;

        log(QStringLiteral("══════ Attempt %1 / %2 ══════").arg(attempt + 1).arg(maxAttempts));
        if (self) emit self->progressChanged(attempt, maxAttempts);

        // ── Phase 1: craft PROMPTS_PER_CYCLE candidates ──────────────────
        log(QStringLiteral("Phase 1 — Crafting %1 prompt candidates…").arg(PROMPTS_PER_CYCLE));

        const QString fileContext  = _buildFileContext(filesDir);
        const QString crafterMeta =
            _buildCrafterPrompt(promptInput, assessmentCriteria, fileContext, previousResults);

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

        // ── Phase 2 + 3: run then score each candidate ───────────────────
        for (int ci = 0; ci < candidates.size(); ++ci) {
            if (done()) co_return;

            const QString &candidate = candidates[ci];
            log(QStringLiteral("  ┌─ Candidate %1 / %2 (global #%3)")
                .arg(ci + 1).arg(candidates.size()).arg(globalIndex + 1));
            log(QStringLiteral("  │  Prompt: %1%2")
                .arg(candidate.left(150))
                .arg(candidate.size() > 150 ? "…" : ""));

            // Phase 2 — run (in files dir so CLI can read input files)
            log(QStringLiteral("  │  Phase 2 — Running prompt…"));
            const CliRunResult runResult = co_await cli->runPrompt(candidate, filesDir);
            if (done()) co_return;

            if (!runResult.processStarted) {
                log(QStringLiteral("  │  ERROR: runner CLI failed to start."));
                log(QStringLiteral("  └─ skipped"));
                ++globalIndex;
                continue;
            }

            const QString outputExcerpt =
                runResult.output.left(300) + (runResult.output.size() > 300 ? "…" : "");
            log(QStringLiteral("  │  Output (%1 chars): %2")
                .arg(runResult.output.size()).arg(outputExcerpt));

            // Phase 3 — score
            log(QStringLiteral("  │  Phase 3 — Scoring output…"));
            const QString assessorMeta =
                _buildAssessorPrompt(candidate, runResult.output, assessmentCriteria);

            const CliRunResult assessResult = co_await cli->runPrompt(assessorMeta);
            if (done()) co_return;

            const Assessment a = _parseAssessment(assessResult.output);
            if (a.cheated) {
                log(QStringLiteral("  └─ Score: 0.0 (CHEATED — descriptive fingerprinting or direct naming) — %1")
                    .arg(a.explanation));
            } else {
                log(QStringLiteral("  └─ Score: %1 — %2")
                    .arg(a.score, 0, 'f', 1).arg(a.explanation));
            }

            previousResults.append({candidate, a.score, a.explanation});

            if (self) {
                emit self->resultReady(globalIndex, candidate, runResult.output,
                                       a.score, a.explanation);
            }
            ++globalIndex;
        }

        if (self) emit self->progressChanged(attempt + 1, maxAttempts);
    }

    log(QStringLiteral("Finished. %1 candidates evaluated across %2 attempt(s).")
        .arg(globalIndex).arg(maxAttempts));
    if (self) {
        emit self->progressChanged(maxAttempts, maxAttempts);
        emit self->finished();
        self->m_running = false;
    }
}

// ---------------------------------------------------------------------------
// File context (identical logic to PromptEngineer)
// ---------------------------------------------------------------------------

QString SelfAssessEngineer::_buildFileContext(const QString &filesDir)
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

    constexpr qint64 kMaxPerFile = 8  * 1024;
    constexpr qint64 kMaxTotal   = 32 * 1024;
    qint64 totalBytes = 0;
    QString ctx;

    for (const QFileInfo &fi : files) {
        if (totalBytes >= kMaxTotal) {
            ctx += QStringLiteral("\n[…additional files omitted — 32 KB limit]\n");
            break;
        }
        QFile f(fi.filePath());
        if (!f.open(QIODevice::ReadOnly)) {
            ctx += QStringLiteral("\n[%1 — unreadable]\n").arg(fi.fileName());
            continue;
        }
        const QByteArray raw = f.read(kMaxPerFile);
        f.close();

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

QString SelfAssessEngineer::_buildCrafterPrompt(
    const QString &promptInput,
    const QString &assessmentCriteria,
    const QString &fileContext,
    const QList<ScoredResult> &previousResults)
{
    QString p;
    p += QStringLiteral(
        "You are an expert AI prompt engineer. Craft %1 prompt candidates whose outputs "
        "will score as HIGH as possible (up to 5.0) when evaluated by these criteria:\n\n"
        "ASSESSMENT CRITERIA:\n%2\n\n"
        "PROMPT REQUIREMENTS (rules every candidate must follow):\n%3\n\n"
        "Each candidate must take a GENUINELY DIFFERENT angle or approach to maximise variety.\n")
        .arg(PROMPTS_PER_CYCLE)
        .arg(assessmentCriteria)
        .arg(promptInput);

    if (!fileContext.isEmpty()) {
        p += QStringLiteral(
            "\nAVAILABLE FILES: The AI running your prompt will have these files in its "
            "working directory. Your prompts may instruct it to read them:\n\n%1\n")
            .arg(fileContext);
    }

    if (!previousResults.isEmpty()) {
        // Sort a copy descending by score to pick best and worst for feedback.
        QList<ScoredResult> sorted = previousResults;
        std::sort(sorted.begin(), sorted.end(),
                  [](const ScoredResult &a, const ScoredResult &b) {
                      return a.score > b.score;
                  });

        p += QStringLiteral("\nPREVIOUS RESULTS — build on high-scoring approaches, "
                            "avoid repeating low-scoring ones:\n\n");

        // Top 3
        const int topN = qMin(3, static_cast<int>(sorted.size()));
        p += QStringLiteral("Top scoring:\n");
        for (int i = 0; i < topN; ++i) {
            const auto &r = sorted[i];
            p += QStringLiteral("• Score %1: \"%2…\" — %3\n")
                     .arg(r.score, 0, 'f', 1)
                     .arg(r.prompt.left(120))
                     .arg(r.explanation);
        }

        // Bottom 2 (only if there are more results than topN)
        const int bottomStart = qMax(topN, static_cast<int>(sorted.size()) - 2);
        if (bottomStart < sorted.size()) {
            p += QStringLiteral("Lowest scoring (avoid similar approaches):\n");
            for (int i = bottomStart; i < sorted.size(); ++i) {
                const auto &r = sorted[i];
                p += QStringLiteral("• Score %1: \"%2…\" — %3\n")
                         .arg(r.score, 0, 'f', 1)
                         .arg(r.prompt.left(120))
                         .arg(r.explanation);
            }
        }
    }

    p += QStringLiteral(
        "\nRespond EXACTLY in the following format (include every marker):\n"
        "---PROMPT_1---\n[first prompt text]\n"
        "---PROMPT_2---\n[second prompt text]\n"
        "---PROMPT_3---\n[third prompt text]\n"
        "---PROMPT_4---\n[fourth prompt text]\n"
        "---END---");
    return p;
}

QString SelfAssessEngineer::_buildAssessorPrompt(const QString &candidatePrompt,
                                                   const QString &runnerOutput,
                                                   const QString &assessmentCriteria)
{
    return QStringLiteral(
        "You are a rigorous evaluator. Score the AI output AND check the prompt for cheating.\n\n"
        "ASSESSMENT CRITERIA:\n%1\n\n"
        "PROMPT THAT WAS EXECUTED:\n%2\n\n"
        "AI RESPONSE TO EVALUATE:\n%3\n\n"
        "STEP 1 — Check whether the prompt cheated.\n"
        "A prompt cheats (mark PROMPT_CHEATS: YES) in EITHER of these ways:\n"
        "  a) DIRECT NAMING: it explicitly quotes or names the specific items the criteria require.\n"
        "  b) DESCRIPTIVE FINGERPRINTING: it describes the required items so specifically\n"
        "     (physical mechanism, geometry, optical technology, number of faces/sides,\n"
        "     material composition, specific regulatory sub-clause, etc.) that only those\n"
        "     exact items could match — even without using their commercial names.\n"
        "A prompt is clean (mark PROMPT_CHEATS: NO) only if it is a neutral, topic-level\n"
        "question a domain expert would naturally answer by mentioning those items.\n\n"
        "STEP 2 — Score the output quality (apply only if PROMPT_CHEATS: NO; otherwise use 0.0).\n"
        "  5 = excellent, fully meets criteria\n"
        "  4 = good, mostly meets criteria\n"
        "  3 = acceptable, partially meets criteria\n"
        "  2 = poor, mostly fails criteria\n"
        "  1 = very poor, fails criteria entirely\n"
        "  0 = prompt cheated (forced score regardless of output quality)\n\n"
        "Reply EXACTLY in this format (no extra text before or after):\n"
        "PROMPT_CHEATS: YES or NO\n"
        "SCORE: [single decimal, e.g. 3.7 — use 0.0 if PROMPT_CHEATS is YES]\n"
        "EXPLANATION: [one sentence covering both the cheat check and output quality]\n"
        "DETAILS: [detailed breakdown]")
        .arg(assessmentCriteria)
        .arg(candidatePrompt)
        .arg(runnerOutput.left(4000));
}

// ---------------------------------------------------------------------------
// Parsers
// ---------------------------------------------------------------------------

QStringList SelfAssessEngineer::_parseCraftedPrompts(const QString &crafterOutput)
{
    const QStringList kMarkers = {
        QStringLiteral("---PROMPT_1---"),
        QStringLiteral("---PROMPT_2---"),
        QStringLiteral("---PROMPT_3---"),
        QStringLiteral("---PROMPT_4---"),
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

SelfAssessEngineer::Assessment SelfAssessEngineer::_parseAssessment(const QString &assessorOutput)
{
    Assessment a;
    for (const QString &raw : assessorOutput.split(u'\n')) {
        const QString line = raw.trimmed();
        if (line.startsWith(QStringLiteral("PROMPT_CHEATS:"))) {
            a.cheated = line.contains(QStringLiteral("YES"), Qt::CaseInsensitive);
        } else if (line.startsWith(QStringLiteral("SCORE:"))) {
            bool ok;
            const double v = line.mid(6).trimmed().toDouble(&ok);
            if (ok) {
                a.score = qBound(0.0, v, 5.0);
            }
        } else if (line.startsWith(QStringLiteral("EXPLANATION:"))) {
            a.explanation = line.mid(12).trimmed();
        } else if (line.startsWith(QStringLiteral("DETAILS:"))) {
            a.details = line.mid(8).trimmed();
        }
    }
    // Enforce zero score for cheating prompts regardless of what the assessor said.
    if (a.cheated) {
        a.score = 0.0;
    }
    return a;
}
