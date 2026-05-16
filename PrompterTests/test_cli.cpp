#include <QtTest>
#include <QEventLoop>
#include <QTimer>

#include "aicli/AbstractCli.h"
#include "aicli/AvailableCliTable.h"
#include "aicli/CliClaude.h"
#include "aicli/CliGemini.h"
#include "aicli/CliCodex.h"
#include "aicli/CliDeepseek.h"
#include "aicli/CliKimi.h"
#include "aicli/CliMistral.h"

// ---------------------------------------------------------------------------
// Test-only CLI subclasses  (NOT registered via DECLARE_CLI)
// ---------------------------------------------------------------------------

// Executable that does not exist → FailedToStart on every call.
class NonExistentCli : public AbstractCli
{
public:
    QString getName() const override        { return QStringLiteral("NonExistent"); }
    QString getDescription() const override { return QStringLiteral("Test stub — never installed"); }
    bool canGenSvg() const override               { return false; }
    bool canGenImages() const override            { return false; }
    bool canGenVideosFromText() const override    { return false; }
    bool canGenVideoFromImages() const override   { return false; }
    QString getExecutable() const override  { return QStringLiteral("__not_a_real_binary_xyz__"); }
};

// `cat` echoes stdin back to stdout — lets us test the full runPrompt pipeline
// without requiring a real AI CLI.
class CatCli : public AbstractCli
{
public:
    QString getName() const override        { return QStringLiteral("Cat"); }
    QString getDescription() const override { return QStringLiteral("Test stub — cat echo"); }
    bool canGenSvg() const override               { return false; }
    bool canGenImages() const override            { return false; }
    bool canGenVideosFromText() const override    { return false; }
    bool canGenVideoFromImages() const override   { return false; }
    QString getExecutable() const override  { return QStringLiteral("cat"); }
    QStringList promptArgs() const override { return {}; } // cat reads stdin, no extra args
};

// ---------------------------------------------------------------------------
// Helper: run an event loop until `done` is set or the timeout fires.
// Returns true if done was set before the timeout.
// ---------------------------------------------------------------------------
static bool waitFor(bool &done, int msTimeout = 10000)
{
    if (done) return true;
    QEventLoop loop;
    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
    // Poll done flag via a repeating timer so we exit as soon as it becomes true.
    QTimer poller;
    poller.setInterval(20);
    QObject::connect(&poller, &QTimer::timeout, [&]() {
        if (done) loop.quit();
    });
    guard.start(msTimeout);
    poller.start();
    loop.exec();
    return done;
}

// ---------------------------------------------------------------------------

class Test_CLI : public QObject
{
    Q_OBJECT

private slots:
    // ---- Registry -------------------------------------------------------
    void test_registry_notEmpty();
    void test_registry_containsExpectedNames();
    void test_registry_uniqueNames();
    void test_registry_uniqueExecutables();

    // ---- Per-CLI properties (all registered CLIs) -----------------------
    void test_eachCli_nameNotEmpty();
    void test_eachCli_descriptionNotEmpty();
    void test_eachCli_executableNotEmpty();
    void test_eachCli_promptArgsNotEmpty();
    void test_eachCli_availabilityArgsNotEmpty();

    // ---- CliClaude specifics --------------------------------------------
    void test_claude_promptArgsContainSkipPermissions();
    void test_claude_promptArgsReadFromStdin();
    void test_claude_parseMembershipAlwaysEmpty();

    // ---- AbstractCli defaults (via NonExistentCli which overrides nothing extra) --
    void test_defaults_availabilityArgsIsVersion();
    void test_defaults_parseMembershipIsEmpty();

    // ---- AvailableCliTable — synchronous API ----------------------------
    void test_table_rowCountEqualsRegistry();
    void test_table_columnCountIsThree();
    void test_table_headerData_names();
    void test_table_headerData_verticalReturnsDefault();
    void test_table_headerData_nonDisplayRoleReturnsInvalid();
    void test_table_flags_availableHasCheckable();
    void test_table_flags_availableNotEditable();
    void test_table_flags_nameNotCheckable();
    void test_table_initialState_allPartiallyChecked();
    void test_table_initialState_membershipShowsChecking();
    void test_table_dataName_matchesRegistry();
    void test_table_dataName_tooltipIsDescription();
    void test_table_dataAvailable_tooltipWhileChecking();
    void test_table_data_invalidIndex();
    void test_table_data_outOfRangeRow();
    void test_table_cliAt_validRows();
    void test_table_cliAt_negativeRow();
    void test_table_cliAt_rowBeyondEnd();
    void test_table_refresh_immediatelyResetsToPartial();

    // ---- Async: checkAvailability ---------------------------------------
    void test_checkAvailability_nonExistentIsUnavailable();
    void test_checkAvailability_nonExistentVersionOutputEmpty();
    void test_checkAvailability_nonExistentMembershipEmpty();
    void test_checkAvailability_contextGuardPreventsCallback();

    // ---- Async: runPrompt -----------------------------------------------
    void test_runPrompt_nonExistentProcessNotStarted();
    void test_runPrompt_catEchosPrompt();
    void test_runPrompt_catExitCodeZero();
    void test_runPrompt_catNoStderr();
    void test_runPrompt_catDurationRecorded();
    void test_runPrompt_contextGuardPreventsCallback();

    // ---- AvailableCliTable async ----------------------------------------
    void test_table_allChecksCompleteEventually();
};

// ===========================================================================
// Registry
// ===========================================================================

void Test_CLI::test_registry_notEmpty()
{
    QVERIFY(!AbstractCli::ALL_CLIS().isEmpty());
}

void Test_CLI::test_registry_containsExpectedNames()
{
    QStringList names;
    for (const AbstractCli *cli : AbstractCli::ALL_CLIS()) {
        names << cli->getName();
    }
    const QStringList expected = {
        QStringLiteral("Claude"),
        QStringLiteral("Gemini"),
        QStringLiteral("Codex"),
        QStringLiteral("DeepSeek"),
        QStringLiteral("Kimi"),
        QStringLiteral("Mistral"),
    };
    for (const QString &name : expected) {
        QVERIFY2(names.contains(name), qPrintable("Missing: " + name));
    }
}

void Test_CLI::test_registry_uniqueNames()
{
    QStringList seen;
    for (const AbstractCli *cli : AbstractCli::ALL_CLIS()) {
        const QString n = cli->getName();
        QVERIFY2(!seen.contains(n), qPrintable("Duplicate name: " + n));
        seen << n;
    }
}

void Test_CLI::test_registry_uniqueExecutables()
{
    QStringList seen;
    for (const AbstractCli *cli : AbstractCli::ALL_CLIS()) {
        const QString e = cli->getExecutable();
        QVERIFY2(!seen.contains(e), qPrintable("Duplicate executable: " + e));
        seen << e;
    }
}

// ===========================================================================
// Per-CLI properties
// ===========================================================================

void Test_CLI::test_eachCli_nameNotEmpty()
{
    for (const AbstractCli *cli : AbstractCli::ALL_CLIS()) {
        QVERIFY2(!cli->getName().isEmpty(),
            qPrintable(cli->getExecutable() + ": name is empty"));
    }
}

void Test_CLI::test_eachCli_descriptionNotEmpty()
{
    for (const AbstractCli *cli : AbstractCli::ALL_CLIS()) {
        QVERIFY2(!cli->getDescription().isEmpty(),
            qPrintable(cli->getName() + ": description is empty"));
    }
}

void Test_CLI::test_eachCli_executableNotEmpty()
{
    for (const AbstractCli *cli : AbstractCli::ALL_CLIS()) {
        QVERIFY2(!cli->getExecutable().isEmpty(),
            qPrintable(cli->getName() + ": executable is empty"));
    }
}

void Test_CLI::test_eachCli_promptArgsNotEmpty()
{
    for (const AbstractCli *cli : AbstractCli::ALL_CLIS()) {
        QVERIFY2(!cli->promptArgs().isEmpty(),
            qPrintable(cli->getName() + ": promptArgs is empty"));
    }
}

void Test_CLI::test_eachCli_availabilityArgsNotEmpty()
{
    for (const AbstractCli *cli : AbstractCli::ALL_CLIS()) {
        QVERIFY2(!cli->availabilityArgs().isEmpty(),
            qPrintable(cli->getName() + ": availabilityArgs is empty"));
    }
}

// ===========================================================================
// CliClaude specifics
// ===========================================================================

void Test_CLI::test_claude_promptArgsContainSkipPermissions()
{
    CliClaude claude;
    QVERIFY(claude.promptArgs().contains(
        QStringLiteral("--dangerously-skip-permissions")));
}

void Test_CLI::test_claude_promptArgsReadFromStdin()
{
    CliClaude claude;
    // "-" means "read from stdin" in Claude's CLI convention.
    QVERIFY(claude.promptArgs().contains(QStringLiteral("-")));
    QVERIFY(claude.promptArgs().contains(QStringLiteral("-p")));
}

void Test_CLI::test_claude_parseMembershipAlwaysEmpty()
{
    CliClaude claude;
    QVERIFY(claude.parseMembership(QStringLiteral("claude/1.2.3 linux-x64")).isEmpty());
    QVERIFY(claude.parseMembership(QStringLiteral("Claude Pro member")).isEmpty());
    QVERIFY(claude.parseMembership(QString{}).isEmpty());
}

// ===========================================================================
// AbstractCli defaults
// ===========================================================================

void Test_CLI::test_defaults_availabilityArgsIsVersion()
{
    NonExistentCli cli;
    const QStringList args = cli.availabilityArgs();
    QCOMPARE(args.size(), 1);
    QCOMPARE(args.first(), QStringLiteral("--version"));
}

void Test_CLI::test_defaults_parseMembershipIsEmpty()
{
    NonExistentCli cli;
    QVERIFY(cli.parseMembership(QStringLiteral("anything")).isEmpty());
    QVERIFY(cli.parseMembership(QString{}).isEmpty());
}

// ===========================================================================
// AvailableCliTable — synchronous API
// ===========================================================================

void Test_CLI::test_table_rowCountEqualsRegistry()
{
    AvailableCliTable table;
    QCOMPARE(table.rowCount(), AbstractCli::ALL_CLIS().size());
}

void Test_CLI::test_table_columnCountIsThree()
{
    AvailableCliTable table;
    QCOMPARE(table.columnCount(), 3);
    QCOMPARE(table.columnCount(), static_cast<int>(AvailableCliTable::ColCount));
}

void Test_CLI::test_table_headerData_names()
{
    AvailableCliTable table;
    const QVariant colName = table.headerData(
        AvailableCliTable::ColName, Qt::Horizontal, Qt::DisplayRole);
    const QVariant colAvail = table.headerData(
        AvailableCliTable::ColAvailable, Qt::Horizontal, Qt::DisplayRole);
    const QVariant colMember = table.headerData(
        AvailableCliTable::ColMembership, Qt::Horizontal, Qt::DisplayRole);

    QVERIFY(colName.isValid());
    QVERIFY(!colName.toString().isEmpty());
    QVERIFY(colAvail.isValid());
    QVERIFY(!colAvail.toString().isEmpty());
    QVERIFY(colMember.isValid());
    QVERIFY(!colMember.toString().isEmpty());
    // All three headers must be distinct.
    QVERIFY(colName != colAvail);
    QVERIFY(colName != colMember);
    QVERIFY(colAvail != colMember);
}

void Test_CLI::test_table_headerData_verticalReturnsDefault()
{
    AvailableCliTable table;
    // Vertical headers are handled by the base class (row numbers).
    const QVariant v = table.headerData(0, Qt::Vertical, Qt::DisplayRole);
    QVERIFY(v.isValid()); // base class returns "1", "2", …
}

void Test_CLI::test_table_headerData_nonDisplayRoleReturnsInvalid()
{
    AvailableCliTable table;
    // DecorationRole is not handled — should return invalid.
    const QVariant v = table.headerData(
        AvailableCliTable::ColName, Qt::Horizontal, Qt::DecorationRole);
    QVERIFY(!v.isValid());
}

void Test_CLI::test_table_flags_availableHasCheckable()
{
    AvailableCliTable table;
    if (table.rowCount() == 0) QSKIP("No CLIs registered");
    const QModelIndex idx = table.index(0, AvailableCliTable::ColAvailable);
    QVERIFY(table.flags(idx) & Qt::ItemIsUserCheckable);
}

void Test_CLI::test_table_flags_availableNotEditable()
{
    AvailableCliTable table;
    if (table.rowCount() == 0) QSKIP("No CLIs registered");
    const QModelIndex idx = table.index(0, AvailableCliTable::ColAvailable);
    QVERIFY(!(table.flags(idx) & Qt::ItemIsEditable));
}

void Test_CLI::test_table_flags_nameNotCheckable()
{
    AvailableCliTable table;
    if (table.rowCount() == 0) QSKIP("No CLIs registered");
    const QModelIndex idx = table.index(0, AvailableCliTable::ColName);
    QVERIFY(!(table.flags(idx) & Qt::ItemIsUserCheckable));
}

void Test_CLI::test_table_initialState_allPartiallyChecked()
{
    // Before the event loop runs, no async callback can have fired yet.
    AvailableCliTable table;
    for (int row = 0; row < table.rowCount(); ++row) {
        const QModelIndex idx = table.index(row, AvailableCliTable::ColAvailable);
        const auto state = table.data(idx, Qt::CheckStateRole).value<Qt::CheckState>();
        QVERIFY2(state == Qt::PartiallyChecked,
            qPrintable(QString("Row %1 (%2) should be PartiallyChecked")
                .arg(row)
                .arg(table.data(table.index(row, AvailableCliTable::ColName)).toString())));
    }
}

void Test_CLI::test_table_initialState_membershipShowsChecking()
{
    AvailableCliTable table;
    for (int row = 0; row < table.rowCount(); ++row) {
        const QModelIndex idx = table.index(row, AvailableCliTable::ColMembership);
        const QString text = table.data(idx, Qt::DisplayRole).toString();
        QVERIFY2(!text.isEmpty(),
            qPrintable(QString("Row %1 membership should show placeholder text").arg(row)));
    }
}

void Test_CLI::test_table_dataName_matchesRegistry()
{
    AvailableCliTable table;
    const auto &all = AbstractCli::ALL_CLIS();
    for (int row = 0; row < table.rowCount(); ++row) {
        const QString name = table.data(table.index(row, AvailableCliTable::ColName)).toString();
        QCOMPARE(name, all.at(row)->getName());
    }
}

void Test_CLI::test_table_dataName_tooltipIsDescription()
{
    AvailableCliTable table;
    if (table.rowCount() == 0) QSKIP("No CLIs registered");
    const QString tooltip = table.data(
        table.index(0, AvailableCliTable::ColName), Qt::ToolTipRole).toString();
    QCOMPARE(tooltip, AbstractCli::ALL_CLIS().first()->getDescription());
}

void Test_CLI::test_table_dataAvailable_tooltipWhileChecking()
{
    AvailableCliTable table;
    if (table.rowCount() == 0) QSKIP("No CLIs registered");
    // Before event loop: still checking → tooltip should say so.
    const QString tip = table.data(
        table.index(0, AvailableCliTable::ColAvailable), Qt::ToolTipRole).toString();
    QVERIFY(!tip.isEmpty());
}

void Test_CLI::test_table_data_invalidIndex()
{
    AvailableCliTable table;
    QVERIFY(!table.data(QModelIndex{}).isValid());
}

void Test_CLI::test_table_data_outOfRangeRow()
{
    AvailableCliTable table;
    const int oob = table.rowCount() + 99;
    QVERIFY(!table.data(table.index(oob, 0)).isValid());
}

void Test_CLI::test_table_cliAt_validRows()
{
    AvailableCliTable table;
    const auto &all = AbstractCli::ALL_CLIS();
    for (int row = 0; row < table.rowCount(); ++row) {
        QCOMPARE(table.cliAt(row), all.at(row));
    }
}

void Test_CLI::test_table_cliAt_negativeRow()
{
    AvailableCliTable table;
    QVERIFY(table.cliAt(-1) == nullptr);
}

void Test_CLI::test_table_cliAt_rowBeyondEnd()
{
    AvailableCliTable table;
    QVERIFY(table.cliAt(table.rowCount()) == nullptr);
    QVERIFY(table.cliAt(table.rowCount() + 999) == nullptr);
}

void Test_CLI::test_table_refresh_immediatelyResetsToPartial()
{
    AvailableCliTable table;
    // Let the event loop run briefly so some checks might complete.
    QTest::qWait(200);

    table.refresh();

    // Immediately after refresh() — before any event-loop iteration — every
    // row must be back to PartiallyChecked (checked=false).
    for (int row = 0; row < table.rowCount(); ++row) {
        const QModelIndex idx = table.index(row, AvailableCliTable::ColAvailable);
        const auto state = table.data(idx, Qt::CheckStateRole).value<Qt::CheckState>();
        QVERIFY2(state == Qt::PartiallyChecked,
            qPrintable(QString("Row %1 not reset after refresh").arg(row)));
    }
}

// ===========================================================================
// Async: checkAvailability
// ===========================================================================

void Test_CLI::test_checkAvailability_nonExistentIsUnavailable()
{
    NonExistentCli cli;
    bool done = false;
    CliAvailability result;
    QObject ctx;

    cli.checkAvailabilityAsync(&ctx, [&](CliAvailability avail) {
        result = avail;
        done   = true;
    });

    QVERIFY2(waitFor(done), "checkAvailability timed out for non-existent CLI");
    QVERIFY(!result.available);
}

void Test_CLI::test_checkAvailability_nonExistentVersionOutputEmpty()
{
    NonExistentCli cli;
    bool done = false;
    CliAvailability result;
    QObject ctx;

    cli.checkAvailabilityAsync(&ctx, [&](CliAvailability avail) {
        result = avail;
        done   = true;
    });

    QVERIFY2(waitFor(done), "checkAvailability timed out");
    QVERIFY(result.versionOutput.isEmpty());
}

void Test_CLI::test_checkAvailability_nonExistentMembershipEmpty()
{
    NonExistentCli cli;
    bool done = false;
    CliAvailability result;
    QObject ctx;

    cli.checkAvailabilityAsync(&ctx, [&](CliAvailability avail) {
        result = avail;
        done   = true;
    });

    QVERIFY2(waitFor(done), "checkAvailability timed out");
    QVERIFY(result.membership.isEmpty());
}

void Test_CLI::test_checkAvailability_contextGuardPreventsCallback()
{
    NonExistentCli cli;
    bool callbackFired = false;

    {
        QObject ctx;
        cli.checkAvailabilityAsync(&ctx, [&callbackFired](CliAvailability) {
            callbackFired = true;
        });
        // ctx destroyed here — guard becomes null
    }

    // Wait long enough for the coroutine to finish, then verify callback was suppressed.
    QTest::qWait(3000);
    QVERIFY(!callbackFired);
}

// ===========================================================================
// Async: runPrompt
// ===========================================================================

void Test_CLI::test_runPrompt_nonExistentProcessNotStarted()
{
    NonExistentCli cli;
    bool done = false;
    CliRunResult result;
    QObject ctx;

    cli.runPromptAsync(QStringLiteral("hello"), &ctx, [&](CliRunResult r) {
        result = r;
        done   = true;
    });

    QVERIFY2(waitFor(done), "runPrompt timed out for non-existent CLI");
    QVERIFY(!result.processStarted);
    QCOMPARE(result.exitCode, -1);
}

void Test_CLI::test_runPrompt_catEchosPrompt()
{
    CatCli cli;
    bool done = false;
    CliRunResult result;
    QObject ctx;

    const QString prompt = QStringLiteral("hello from prompter test");
    cli.runPromptAsync(prompt, &ctx, [&](CliRunResult r) {
        result = r;
        done   = true;
    });

    QVERIFY2(waitFor(done, 10000), "runPrompt/cat timed out");
    QVERIFY(result.processStarted);
    QVERIFY2(result.output.contains(prompt),
        qPrintable("Expected '" + prompt + "' in output: " + result.output));
}

void Test_CLI::test_runPrompt_catExitCodeZero()
{
    CatCli cli;
    bool done = false;
    CliRunResult result;
    QObject ctx;

    cli.runPromptAsync(QStringLiteral("exit test"), &ctx, [&](CliRunResult r) {
        result = r;
        done   = true;
    });

    QVERIFY2(waitFor(done, 10000), "runPrompt/cat timed out");
    QCOMPARE(result.exitCode, 0);
}

void Test_CLI::test_runPrompt_catNoStderr()
{
    CatCli cli;
    bool done = false;
    CliRunResult result;
    QObject ctx;

    cli.runPromptAsync(QStringLiteral("stderr test"), &ctx, [&](CliRunResult r) {
        result = r;
        done   = true;
    });

    QVERIFY2(waitFor(done, 10000), "runPrompt/cat timed out");
    QVERIFY(result.errorOutput.isEmpty());
}

void Test_CLI::test_runPrompt_catDurationRecorded()
{
    CatCli cli;
    bool done = false;
    CliRunResult result;
    QObject ctx;

    cli.runPromptAsync(QStringLiteral("duration test"), &ctx, [&](CliRunResult r) {
        result = r;
        done   = true;
    });

    QVERIFY2(waitFor(done, 10000), "runPrompt/cat timed out");
    QVERIFY(result.durationMs >= 0);
}

void Test_CLI::test_runPrompt_contextGuardPreventsCallback()
{
    CatCli cli;
    bool callbackFired = false;

    {
        QObject ctx;
        cli.runPromptAsync(QStringLiteral("guard test"), &ctx, [&callbackFired](CliRunResult) {
            callbackFired = true;
        });
        // ctx destroyed here
    }

    QTest::qWait(5000);
    QVERIFY(!callbackFired);
}

// ===========================================================================
// AvailableCliTable async
// ===========================================================================

void Test_CLI::test_table_allChecksCompleteEventually()
{
    AvailableCliTable table;

    // Wait until every row leaves PartiallyChecked, or 15 s (covers the 5 s
    // CLI timeout even if all 6 checks run sequentially, which they don't).
    bool allDone = false;
    QObject conn;
    QObject::connect(&table, &QAbstractTableModel::dataChanged, &conn, [&]() {
        for (int row = 0; row < table.rowCount(); ++row) {
            const auto s = table.data(
                table.index(row, AvailableCliTable::ColAvailable),
                Qt::CheckStateRole).value<Qt::CheckState>();
            if (s == Qt::PartiallyChecked) return;
        }
        allDone = true;
    });

    QVERIFY2(waitFor(allDone, 15000), "Some CLI checks never completed");

    for (int row = 0; row < table.rowCount(); ++row) {
        const auto s = table.data(
            table.index(row, AvailableCliTable::ColAvailable),
            Qt::CheckStateRole).value<Qt::CheckState>();
        QVERIFY2(s != Qt::PartiallyChecked,
            qPrintable(QString("Row %1 (%2) still PartiallyChecked after 15 s")
                .arg(row)
                .arg(table.data(table.index(row, AvailableCliTable::ColName)).toString())));
    }
}

QTEST_GUILESS_MAIN(Test_CLI)
#include "test_cli.moc"
