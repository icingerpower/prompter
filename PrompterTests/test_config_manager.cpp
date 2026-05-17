#include <QtTest>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "ConfigManager.h"

class Test_ConfigManager : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // ---- Construction / initial state ------------------------------------
    void test_constructor_createsDefaultEntry();
    void test_constructor_defaultNameIsDefault();
    void test_constructor_currentIdIsValid();
    void test_constructor_currentIdMatchesDefaultRow();
    void test_constructor_rowCountIsOne();
    void test_constructor_columnCountIsOne();
    void test_constructor_createsDirOnDisk();

    // ---- Persistence across instances ------------------------------------
    void test_persistence_reloadsEntries();
    void test_persistence_reloadsCurrentId();
    void test_persistence_emptyDirCreatesDefault();

    // ---- addConfig -------------------------------------------------------
    void test_addConfig_incrementsRowCount();
    void test_addConfig_returnsNonEmptyId();
    void test_addConfig_idIsAccessibleViaRole();
    void test_addConfig_defaultNameIsNewConfig();
    void test_addConfig_namesAreUnique();
    void test_addConfig_doesNotChangeCurrentId();

    // ---- duplicateConfig -------------------------------------------------
    void test_duplicateConfig_incrementsRowCount();
    void test_duplicateConfig_returnsNewId();
    void test_duplicateConfig_newNameContainsCopyOf();
    void test_duplicateConfig_copiesDataFiles();
    void test_duplicateConfig_unknownSourceReturnsEmpty();
    void test_duplicateConfig_duplicateNamesAreUnique();

    // ---- removeConfig ----------------------------------------------------
    void test_removeConfig_decrementsRowCount();
    void test_removeConfig_returnsTrueOnSuccess();
    void test_removeConfig_returnsFalseWhenLastEntry();
    void test_removeConfig_removedIdNoLongerFound();
    void test_removeConfig_deletesDataDirOnDisk();
    void test_removeConfig_currentMovesToNeighbour();
    void test_removeConfig_emitsCurrentConfigChanged();
    void test_removeConfig_unknownIdReturnsFalse();

    // ---- renameConfig ----------------------------------------------------
    void test_renameConfig_changesDisplayName();
    void test_renameConfig_doesNotChangeInternalId();
    void test_renameConfig_emitsDataChanged();
    void test_renameConfig_unknownIdIsNoOp();
    void test_renameConfig_emptyNameIsIgnored();
    void test_renameConfig_trims();

    // ---- selectConfig ----------------------------------------------------
    void test_selectConfig_updatesCurrent();
    void test_selectConfig_emitsCurrentConfigChanged();
    void test_selectConfig_sameIdIsNoOp();
    void test_selectConfig_unknownIdIsNoOp();

    // ---- setData (inline rename) ----------------------------------------
    void test_setData_renamesViaEditRole();
    void test_setData_rejectsEmptyName();
    void test_setData_rejectsNonEditRole();
    void test_setData_rejectsInvalidIndex();

    // ---- configDir -------------------------------------------------------
    void test_configDir_createsDirOnDisk();
    void test_configDir_stableAcrossRename();
    void test_configDir_differentIdsHaveDifferentDirs();

    // ---- QAbstractTableModel interface -----------------------------------
    void test_model_internalIdRole();
    void test_model_invalidIndexReturnsInvalid();
    void test_model_outOfRangeRowReturnsInvalid();
    void test_model_flagsAreSelectableEditable();
    void test_model_flagsInvalidIndexIsNoItemFlags();
    void test_model_headerDataHorizontalDisplayRole();
    void test_model_headerDataVerticalIsDefault();
    void test_model_rowCountZeroForValidParent();
    void test_model_columnCountZeroForValidParent();

    // ---- Key scenario: rename then reload --------------------------------
    void test_scenario_renamePreservesDataAcrossInstances();
    void test_scenario_multipleInstancesDifferentIds();
    void test_scenario_addRenameSwitchAndVerifyDataIsolation();

private:
    QTemporaryDir *m_tempDir = nullptr;
    QDir           m_workDir;

    // Helper: create a manager for the default test sub-id.
    ConfigManager *makeCm(const QString &id = QStringLiteral("TestPane")) const;

    // Helper: write a marker file inside a config dir.
    static void writeMarker(const QDir &dir, const QString &content);
    static QString readMarker(const QDir &dir);
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

ConfigManager *Test_ConfigManager::makeCm(const QString &id) const
{
    return new ConfigManager(m_workDir, id);
}

void Test_ConfigManager::writeMarker(const QDir &dir, const QString &content)
{
    QFile f(dir.filePath(QStringLiteral("marker.txt")));
    f.open(QIODevice::WriteOnly | QIODevice::Truncate);
    f.write(content.toUtf8());
}

QString Test_ConfigManager::readMarker(const QDir &dir)
{
    QFile f(dir.filePath(QStringLiteral("marker.txt")));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(f.readAll());
}

// ---------------------------------------------------------------------------
// Setup / teardown
// ---------------------------------------------------------------------------

void Test_ConfigManager::init()
{
    m_tempDir = new QTemporaryDir();
    m_workDir = QDir(m_tempDir->path());
}

void Test_ConfigManager::cleanup()
{
    delete m_tempDir;
    m_tempDir = nullptr;
}

// ===========================================================================
// Construction / initial state
// ===========================================================================

void Test_ConfigManager::test_constructor_createsDefaultEntry()
{
    auto *cm = makeCm();
    QCOMPARE(cm->rowCount(), 1);
    delete cm;
}

void Test_ConfigManager::test_constructor_defaultNameIsDefault()
{
    auto *cm = makeCm();
    const QString name = cm->data(cm->index(0, 0), Qt::DisplayRole).toString();
    QCOMPARE(name, QStringLiteral("Default"));
    delete cm;
}

void Test_ConfigManager::test_constructor_currentIdIsValid()
{
    auto *cm = makeCm();
    QVERIFY(!cm->currentId().isEmpty());
    delete cm;
}

void Test_ConfigManager::test_constructor_currentIdMatchesDefaultRow()
{
    auto *cm = makeCm();
    QCOMPARE(cm->rowForId(cm->currentId()), 0);
    delete cm;
}

void Test_ConfigManager::test_constructor_rowCountIsOne()
{
    auto *cm = makeCm();
    QCOMPARE(cm->rowCount(), 1);
    delete cm;
}

void Test_ConfigManager::test_constructor_columnCountIsOne()
{
    auto *cm = makeCm();
    QCOMPARE(cm->columnCount(), 1);
    delete cm;
}

void Test_ConfigManager::test_constructor_createsDirOnDisk()
{
    auto *cm = makeCm();
    const QDir base(m_workDir.filePath(QStringLiteral("TestPane")));
    QVERIFY(base.exists());
    delete cm;
}

// ===========================================================================
// Persistence across instances
// ===========================================================================

void Test_ConfigManager::test_persistence_reloadsEntries()
{
    QString id1, id2;
    {
        auto *cm = makeCm();
        id1 = cm->currentId();
        id2 = cm->addConfig();
        cm->renameConfig(id2, QStringLiteral("Second"));
        delete cm;
    }
    auto *cm2 = makeCm();
    QCOMPARE(cm2->rowCount(), 2);
    QVERIFY(cm2->rowForId(id1) >= 0);
    QVERIFY(cm2->rowForId(id2) >= 0);
    QCOMPARE(cm2->nameForId(id2), QStringLiteral("Second"));
    delete cm2;
}

void Test_ConfigManager::test_persistence_reloadsCurrentId()
{
    QString savedCurrent;
    {
        auto *cm = makeCm();
        cm->addConfig();
        const QString secondId = cm->data(cm->index(1, 0), ConfigManager::InternalIdRole).toString();
        cm->selectConfig(secondId);
        savedCurrent = cm->currentId();
        delete cm;
    }
    auto *cm2 = makeCm();
    QCOMPARE(cm2->currentId(), savedCurrent);
    delete cm2;
}

void Test_ConfigManager::test_persistence_emptyDirCreatesDefault()
{
    // A fresh directory with no index.ini should get a Default config.
    const QDir freshDir(m_workDir.filePath(QStringLiteral("fresh")));
    ConfigManager cm(m_workDir, QStringLiteral("fresh"));
    QCOMPARE(cm.rowCount(), 1);
    QCOMPARE(cm.data(cm.index(0, 0)).toString(), QStringLiteral("Default"));
}

// ===========================================================================
// addConfig
// ===========================================================================

void Test_ConfigManager::test_addConfig_incrementsRowCount()
{
    auto *cm = makeCm();
    cm->addConfig();
    QCOMPARE(cm->rowCount(), 2);
    delete cm;
}

void Test_ConfigManager::test_addConfig_returnsNonEmptyId()
{
    auto *cm = makeCm();
    QVERIFY(!cm->addConfig().isEmpty());
    delete cm;
}

void Test_ConfigManager::test_addConfig_idIsAccessibleViaRole()
{
    auto *cm = makeCm();
    const QString newId = cm->addConfig();
    const int row = cm->rowForId(newId);
    QVERIFY(row >= 0);
    QCOMPARE(cm->data(cm->index(row, 0), ConfigManager::InternalIdRole).toString(), newId);
    delete cm;
}

void Test_ConfigManager::test_addConfig_defaultNameIsNewConfig()
{
    auto *cm = makeCm();
    const QString newId = cm->addConfig();
    const QString name  = cm->nameForId(newId);
    QVERIFY(!name.isEmpty());
    QVERIFY(name.contains(QStringLiteral("New Config"), Qt::CaseInsensitive));
    delete cm;
}

void Test_ConfigManager::test_addConfig_namesAreUnique()
{
    auto *cm = makeCm();
    const QString id1 = cm->addConfig();
    const QString id2 = cm->addConfig();
    QVERIFY(cm->nameForId(id1) != cm->nameForId(id2));
    delete cm;
}

void Test_ConfigManager::test_addConfig_doesNotChangeCurrentId()
{
    auto *cm = makeCm();
    const QString before = cm->currentId();
    cm->addConfig();
    QCOMPARE(cm->currentId(), before);
    delete cm;
}

// ===========================================================================
// duplicateConfig
// ===========================================================================

void Test_ConfigManager::test_duplicateConfig_incrementsRowCount()
{
    auto *cm = makeCm();
    cm->duplicateConfig(cm->currentId());
    QCOMPARE(cm->rowCount(), 2);
    delete cm;
}

void Test_ConfigManager::test_duplicateConfig_returnsNewId()
{
    auto *cm = makeCm();
    const QString original = cm->currentId();
    const QString dup      = cm->duplicateConfig(original);
    QVERIFY(!dup.isEmpty());
    QVERIFY(dup != original);
    delete cm;
}

void Test_ConfigManager::test_duplicateConfig_newNameContainsCopyOf()
{
    auto *cm = makeCm();
    const QString dup = cm->duplicateConfig(cm->currentId());
    QVERIFY(cm->nameForId(dup).contains(QStringLiteral("Copy"), Qt::CaseInsensitive));
    delete cm;
}

void Test_ConfigManager::test_duplicateConfig_copiesDataFiles()
{
    auto *cm = makeCm();
    const QString srcId = cm->currentId();
    writeMarker(cm->configDir(srcId), QStringLiteral("original data"));

    const QString dupId = cm->duplicateConfig(srcId);
    QCOMPARE(readMarker(cm->configDir(dupId)), QStringLiteral("original data"));
    delete cm;
}

void Test_ConfigManager::test_duplicateConfig_unknownSourceReturnsEmpty()
{
    auto *cm = makeCm();
    QVERIFY(cm->duplicateConfig(QStringLiteral("no-such-id")).isEmpty());
    delete cm;
}

void Test_ConfigManager::test_duplicateConfig_duplicateNamesAreUnique()
{
    auto *cm = makeCm();
    const QString d1 = cm->duplicateConfig(cm->currentId());
    const QString d2 = cm->duplicateConfig(cm->currentId());
    QVERIFY(cm->nameForId(d1) != cm->nameForId(d2));
    delete cm;
}

// ===========================================================================
// removeConfig
// ===========================================================================

void Test_ConfigManager::test_removeConfig_decrementsRowCount()
{
    auto *cm = makeCm();
    const QString extra = cm->addConfig();
    cm->removeConfig(extra);
    QCOMPARE(cm->rowCount(), 1);
    delete cm;
}

void Test_ConfigManager::test_removeConfig_returnsTrueOnSuccess()
{
    auto *cm = makeCm();
    const QString extra = cm->addConfig();
    QVERIFY(cm->removeConfig(extra));
    delete cm;
}

void Test_ConfigManager::test_removeConfig_returnsFalseWhenLastEntry()
{
    auto *cm = makeCm();
    QCOMPARE(cm->rowCount(), 1);
    QVERIFY(!cm->removeConfig(cm->currentId()));
    QCOMPARE(cm->rowCount(), 1);
    delete cm;
}

void Test_ConfigManager::test_removeConfig_removedIdNoLongerFound()
{
    auto *cm = makeCm();
    const QString extra = cm->addConfig();
    cm->removeConfig(extra);
    QCOMPARE(cm->rowForId(extra), -1);
    delete cm;
}

void Test_ConfigManager::test_removeConfig_deletesDataDirOnDisk()
{
    auto *cm = makeCm();
    const QString extra = cm->addConfig();
    const QString dirPath = cm->configDir(extra).absolutePath();
    QVERIFY(QDir(dirPath).exists());
    cm->removeConfig(extra);
    QVERIFY(!QDir(dirPath).exists());
    delete cm;
}

void Test_ConfigManager::test_removeConfig_currentMovesToNeighbour()
{
    auto *cm = makeCm();
    const QString orig  = cm->currentId();
    const QString extra = cm->addConfig();
    cm->selectConfig(extra);       // extra is now current
    cm->removeConfig(extra);
    QCOMPARE(cm->currentId(), orig); // should fall back to orig
    delete cm;
}

void Test_ConfigManager::test_removeConfig_emitsCurrentConfigChanged()
{
    auto *cm = makeCm();
    cm->addConfig();
    cm->selectConfig(cm->data(cm->index(1, 0), ConfigManager::InternalIdRole).toString());

    QSignalSpy spy(cm, &ConfigManager::currentConfigChanged);
    cm->removeConfig(cm->currentId());
    QCOMPARE(spy.count(), 1);
    delete cm;
}

void Test_ConfigManager::test_removeConfig_unknownIdReturnsFalse()
{
    auto *cm = makeCm();
    cm->addConfig(); // make sure >1 entries so size guard isn't the reason
    QVERIFY(!cm->removeConfig(QStringLiteral("no-such-id")));
    delete cm;
}

// ===========================================================================
// renameConfig
// ===========================================================================

void Test_ConfigManager::test_renameConfig_changesDisplayName()
{
    auto *cm = makeCm();
    cm->renameConfig(cm->currentId(), QStringLiteral("My Prompt"));
    QCOMPARE(cm->nameForId(cm->currentId()), QStringLiteral("My Prompt"));
    delete cm;
}

void Test_ConfigManager::test_renameConfig_doesNotChangeInternalId()
{
    auto *cm = makeCm();
    const QString idBefore = cm->currentId();
    cm->renameConfig(idBefore, QStringLiteral("Renamed"));
    QCOMPARE(cm->currentId(), idBefore);
    QCOMPARE(cm->data(cm->index(0, 0), ConfigManager::InternalIdRole).toString(), idBefore);
    delete cm;
}

void Test_ConfigManager::test_renameConfig_emitsDataChanged()
{
    auto *cm = makeCm();
    QSignalSpy spy(cm, &ConfigManager::dataChanged);
    cm->renameConfig(cm->currentId(), QStringLiteral("New Name"));
    QCOMPARE(spy.count(), 1);
    delete cm;
}

void Test_ConfigManager::test_renameConfig_unknownIdIsNoOp()
{
    auto *cm = makeCm();
    QSignalSpy spy(cm, &ConfigManager::dataChanged);
    cm->renameConfig(QStringLiteral("no-such-id"), QStringLiteral("X"));
    QCOMPARE(spy.count(), 0);
    delete cm;
}

void Test_ConfigManager::test_renameConfig_emptyNameIsIgnored()
{
    auto *cm = makeCm();
    const QString nameBefore = cm->nameForId(cm->currentId());
    cm->renameConfig(cm->currentId(), QStringLiteral(""));
    QCOMPARE(cm->nameForId(cm->currentId()), nameBefore);
    delete cm;
}

void Test_ConfigManager::test_renameConfig_trims()
{
    auto *cm = makeCm();
    cm->renameConfig(cm->currentId(), QStringLiteral("  Trimmed  "));
    QCOMPARE(cm->nameForId(cm->currentId()), QStringLiteral("Trimmed"));
    delete cm;
}

// ===========================================================================
// selectConfig
// ===========================================================================

void Test_ConfigManager::test_selectConfig_updatesCurrent()
{
    auto *cm = makeCm();
    const QString newId = cm->addConfig();
    cm->selectConfig(newId);
    QCOMPARE(cm->currentId(), newId);
    delete cm;
}

void Test_ConfigManager::test_selectConfig_emitsCurrentConfigChanged()
{
    auto *cm = makeCm();
    const QString newId = cm->addConfig();
    QSignalSpy spy(cm, &ConfigManager::currentConfigChanged);
    cm->selectConfig(newId);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toString(), newId);
    delete cm;
}

void Test_ConfigManager::test_selectConfig_sameIdIsNoOp()
{
    auto *cm = makeCm();
    QSignalSpy spy(cm, &ConfigManager::currentConfigChanged);
    cm->selectConfig(cm->currentId());
    QCOMPARE(spy.count(), 0);
    delete cm;
}

void Test_ConfigManager::test_selectConfig_unknownIdIsNoOp()
{
    auto *cm = makeCm();
    const QString before = cm->currentId();
    QSignalSpy spy(cm, &ConfigManager::currentConfigChanged);
    cm->selectConfig(QStringLiteral("ghost-id"));
    QCOMPARE(spy.count(), 0);
    QCOMPARE(cm->currentId(), before);
    delete cm;
}

// ===========================================================================
// setData (inline rename)
// ===========================================================================

void Test_ConfigManager::test_setData_renamesViaEditRole()
{
    auto *cm = makeCm();
    cm->setData(cm->index(0, 0), QStringLiteral("Inline"), Qt::EditRole);
    QCOMPARE(cm->data(cm->index(0, 0), Qt::DisplayRole).toString(), QStringLiteral("Inline"));
    delete cm;
}

void Test_ConfigManager::test_setData_rejectsEmptyName()
{
    auto *cm = makeCm();
    const QString before = cm->data(cm->index(0, 0)).toString();
    const bool ok = cm->setData(cm->index(0, 0), QStringLiteral("   "), Qt::EditRole);
    QVERIFY(!ok);
    QCOMPARE(cm->data(cm->index(0, 0)).toString(), before);
    delete cm;
}

void Test_ConfigManager::test_setData_rejectsNonEditRole()
{
    auto *cm = makeCm();
    const bool ok = cm->setData(cm->index(0, 0), QStringLiteral("X"), Qt::DisplayRole);
    QVERIFY(!ok);
    delete cm;
}

void Test_ConfigManager::test_setData_rejectsInvalidIndex()
{
    auto *cm = makeCm();
    QVERIFY(!cm->setData(QModelIndex{}, QStringLiteral("X"), Qt::EditRole));
    delete cm;
}

// ===========================================================================
// configDir
// ===========================================================================

void Test_ConfigManager::test_configDir_createsDirOnDisk()
{
    auto *cm = makeCm();
    const QDir d = cm->configDir(cm->currentId());
    QVERIFY(d.exists());
    delete cm;
}

void Test_ConfigManager::test_configDir_stableAcrossRename()
{
    auto *cm = makeCm();
    const QString id     = cm->currentId();
    const QString path1  = cm->configDir(id).absolutePath();
    cm->renameConfig(id, QStringLiteral("Renamed"));
    const QString path2  = cm->configDir(id).absolutePath();
    QCOMPARE(path1, path2);
    delete cm;
}

void Test_ConfigManager::test_configDir_differentIdsHaveDifferentDirs()
{
    auto *cm = makeCm();
    const QString id2 = cm->addConfig();
    QVERIFY(cm->configDir(cm->currentId()).absolutePath()
         != cm->configDir(id2).absolutePath());
    delete cm;
}

// ===========================================================================
// QAbstractTableModel interface
// ===========================================================================

void Test_ConfigManager::test_model_internalIdRole()
{
    auto *cm = makeCm();
    const QString id = cm->currentId();
    const QVariant v = cm->data(cm->index(0, 0), ConfigManager::InternalIdRole);
    QCOMPARE(v.toString(), id);
    delete cm;
}

void Test_ConfigManager::test_model_invalidIndexReturnsInvalid()
{
    auto *cm = makeCm();
    QVERIFY(!cm->data(QModelIndex{}).isValid());
    delete cm;
}

void Test_ConfigManager::test_model_outOfRangeRowReturnsInvalid()
{
    auto *cm = makeCm();
    QVERIFY(!cm->data(cm->index(99, 0)).isValid());
    delete cm;
}

void Test_ConfigManager::test_model_flagsAreSelectableEditable()
{
    auto *cm = makeCm();
    const Qt::ItemFlags f = cm->flags(cm->index(0, 0));
    QVERIFY(f & Qt::ItemIsSelectable);
    QVERIFY(f & Qt::ItemIsEditable);
    QVERIFY(f & Qt::ItemIsEnabled);
    delete cm;
}

void Test_ConfigManager::test_model_flagsInvalidIndexIsNoItemFlags()
{
    auto *cm = makeCm();
    QCOMPARE(cm->flags(QModelIndex{}), Qt::NoItemFlags);
    delete cm;
}

void Test_ConfigManager::test_model_headerDataHorizontalDisplayRole()
{
    auto *cm = makeCm();
    const QVariant h = cm->headerData(0, Qt::Horizontal, Qt::DisplayRole);
    QVERIFY(h.isValid());
    QVERIFY(!h.toString().isEmpty());
    delete cm;
}

void Test_ConfigManager::test_model_headerDataVerticalIsDefault()
{
    auto *cm = makeCm();
    // Vertical header for row 0 returns row number from base class.
    const QVariant v = cm->headerData(0, Qt::Vertical, Qt::DisplayRole);
    QVERIFY(v.isValid());
    delete cm;
}

void Test_ConfigManager::test_model_rowCountZeroForValidParent()
{
    auto *cm = makeCm();
    QCOMPARE(cm->rowCount(cm->index(0, 0)), 0);
    delete cm;
}

void Test_ConfigManager::test_model_columnCountZeroForValidParent()
{
    auto *cm = makeCm();
    QCOMPARE(cm->columnCount(cm->index(0, 0)), 0);
    delete cm;
}

// ===========================================================================
// Key scenarios
// ===========================================================================

void Test_ConfigManager::test_scenario_renamePreservesDataAcrossInstances()
{
    // This is the core correctness test:
    // write data under an ID, rename the config, destroy and reload the
    // manager, then verify the data is still readable under the same ID.

    QString configId;
    {
        auto *cm = makeCm();
        configId = cm->currentId();

        // Write pane data using the internalId-based directory.
        writeMarker(cm->configDir(configId), QStringLiteral("saved payload"));

        // Rename the config — internalId must not change.
        cm->renameConfig(configId, QStringLiteral("Renamed Config"));
        QCOMPARE(cm->nameForId(configId), QStringLiteral("Renamed Config"));
        delete cm;
    }

    // New instance — simulates app restart.
    {
        auto *cm = makeCm();
        QCOMPARE(cm->rowCount(), 1);

        // The display name should survive the reload.
        QCOMPARE(cm->data(cm->index(0, 0), Qt::DisplayRole).toString(),
                 QStringLiteral("Renamed Config"));

        // The internalId must be the same stable UUID.
        QCOMPARE(cm->data(cm->index(0, 0), ConfigManager::InternalIdRole).toString(),
                 configId);

        // Pane data must still be accessible via the same directory.
        QCOMPARE(readMarker(cm->configDir(configId)), QStringLiteral("saved payload"));
        delete cm;
    }
}

void Test_ConfigManager::test_scenario_multipleInstancesDifferentIds()
{
    // Two panes each get their own ConfigManager with different IDs;
    // they must not share configs or data.

    ConfigManager cmA(m_workDir, QStringLiteral("PaneA"));
    ConfigManager cmB(m_workDir, QStringLiteral("PaneB"));

    cmA.addConfig();
    QCOMPARE(cmA.rowCount(), 2);
    QCOMPARE(cmB.rowCount(), 1); // PaneB untouched

    const QString idA = cmA.currentId();
    const QString idB = cmB.currentId();

    writeMarker(cmA.configDir(idA), QStringLiteral("data A"));
    writeMarker(cmB.configDir(idB), QStringLiteral("data B"));

    QCOMPARE(readMarker(cmA.configDir(idA)), QStringLiteral("data A"));
    QCOMPARE(readMarker(cmB.configDir(idB)), QStringLiteral("data B"));

    // The directories themselves must be distinct.
    QVERIFY(cmA.configDir(idA).absolutePath() != cmB.configDir(idB).absolutePath());
}

void Test_ConfigManager::test_scenario_addRenameSwitchAndVerifyDataIsolation()
{
    // Two configs, each with different stored data; switching selection must
    // not mix up the data.

    auto *cm = makeCm();
    const QString idA = cm->currentId(); // "Default"
    const QString idB = cm->addConfig();

    writeMarker(cm->configDir(idA), QStringLiteral("config A data"));
    writeMarker(cm->configDir(idB), QStringLiteral("config B data"));

    cm->renameConfig(idA, QStringLiteral("Config A"));
    cm->renameConfig(idB, QStringLiteral("Config B"));

    // Destroy and reload.
    delete cm;
    cm = makeCm();

    const int rowA = cm->rowForId(idA);
    const int rowB = cm->rowForId(idB);
    QVERIFY(rowA >= 0);
    QVERIFY(rowB >= 0);

    QCOMPARE(readMarker(cm->configDir(idA)), QStringLiteral("config A data"));
    QCOMPARE(readMarker(cm->configDir(idB)), QStringLiteral("config B data"));

    // Names survived rename+reload.
    QCOMPARE(cm->nameForId(idA), QStringLiteral("Config A"));
    QCOMPARE(cm->nameForId(idB), QStringLiteral("Config B"));

    delete cm;
}

QTEST_GUILESS_MAIN(Test_ConfigManager)
#include "test_config_manager.moc"
