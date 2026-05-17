#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <QAbstractTableModel>
#include <QDir>
#include <QList>
#include <QString>

// Manages a named list of configurations for one pane.
//
// Each entry has a stable internal UUID (never changes, survives renames) and
// a user-visible display name. The list and current selection are persisted to
//   <workingDir>/<id>/index.ini
// Config data written by the pane lives under
//   <workingDir>/<id>/<internalId>/
// so renaming a config never affects where its data is stored.
//
// Usage in a pane:
//   1. Construct with workingDir and a pane-specific id ("Prompt", "Video", …).
//   2. Set as model on the pane's QListView.
//   3. Connect QListView::selectionModel()::currentChanged to a slot that
//      saves data for the OLD internalId and loads data for the NEW one.
//   4. Use configDir(internalId) to get the QDir where pane data is stored.
class ConfigManager : public QAbstractTableModel
{
    Q_OBJECT

public:
    // Custom data role that returns the stable internalId for a row.
    static constexpr int InternalIdRole = Qt::UserRole + 1;

    explicit ConfigManager(const QDir    &workingDir,
                           const QString &id,
                           QObject       *parent = nullptr);

    // -----------------------------------------------------------------------
    // Query
    // -----------------------------------------------------------------------

    // Currently selected config's internalId.
    QString currentId() const;

    // Display name for an internalId. Empty string if not found.
    QString nameForId(const QString &internalId) const;

    // Row index for an internalId. -1 if not found.
    int rowForId(const QString &internalId) const;

    // Directory for storing pane data for this config.
    // Created on first call; remains stable across renames.
    QDir configDir(const QString &internalId) const;

    // -----------------------------------------------------------------------
    // Mutations
    // -----------------------------------------------------------------------

    // Add a new empty config. Returns its internalId.
    QString addConfig();

    // Duplicate an existing config, copying its data directory.
    // Returns the new internalId, or empty string if sourceId is unknown.
    QString duplicateConfig(const QString &sourceId);

    // Remove a config and delete its data directory.
    // Does nothing and returns false when only one config remains.
    bool removeConfig(const QString &internalId);

    // Rename a config (display name only; internalId is unchanged).
    void renameConfig(const QString &internalId, const QString &newName);

    // Set the current config. Emits currentConfigChanged if it changes.
    void selectConfig(const QString &internalId);

    // -----------------------------------------------------------------------
    // QAbstractTableModel
    // -----------------------------------------------------------------------

    int      rowCount   (const QModelIndex &parent = {}) const override;
    int      columnCount(const QModelIndex &parent = {}) const override;
    QVariant data       (const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool     setData    (const QModelIndex &index, const QVariant &value,
                         int role = Qt::EditRole) override;
    QVariant headerData (int section, Qt::Orientation orientation,
                         int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags (const QModelIndex &index) const override;

signals:
    void currentConfigChanged(const QString &internalId);

private:
    struct ConfigEntry {
        QString internalId;
        QString displayName;
    };

    void _load();
    void _saveIndex() const;
    void _ensureDefault();
    QString _uniqueName(const QString &baseName) const;
    static void _copyDir(const QDir &src, const QDir &dst);

    QDir             m_baseDir; // workingDir/<id>/
    QList<ConfigEntry> m_entries;
    QString          m_currentId;
};

#endif // CONFIGMANAGER_H
