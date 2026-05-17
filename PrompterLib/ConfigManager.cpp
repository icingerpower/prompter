#include "ConfigManager.h"

#include <QFileInfo>
#include <QFileInfoList>
#include <QSettings>
#include <QUuid>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ConfigManager::ConfigManager(const QDir &workingDir, const QString &id, QObject *parent)
    : QAbstractTableModel(parent)
    , m_baseDir(workingDir.filePath(id))
{
    m_baseDir.mkpath(QStringLiteral("."));
    _load();
    _ensureDefault();
}

// ---------------------------------------------------------------------------
// Query
// ---------------------------------------------------------------------------

QString ConfigManager::currentId() const
{
    return m_currentId;
}

QString ConfigManager::nameForId(const QString &internalId) const
{
    for (const auto &e : m_entries) {
        if (e.internalId == internalId) {
            return e.displayName;
        }
    }
    return {};
}

int ConfigManager::rowForId(const QString &internalId) const
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].internalId == internalId) {
            return i;
        }
    }
    return -1;
}

QDir ConfigManager::configDir(const QString &internalId) const
{
    m_baseDir.mkpath(internalId);
    return QDir(m_baseDir.filePath(internalId));
}

// ---------------------------------------------------------------------------
// Mutations
// ---------------------------------------------------------------------------

QString ConfigManager::addConfig()
{
    const QString newId   = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString newName = _uniqueName(tr("New Config"));

    beginInsertRows({}, m_entries.size(), m_entries.size());
    m_entries.append({newId, newName});
    endInsertRows();

    _saveIndex();
    return newId;
}

QString ConfigManager::duplicateConfig(const QString &sourceId)
{
    const int sourceRow = rowForId(sourceId);
    if (sourceRow < 0) {
        return {};
    }

    const QString newId   = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString newName = _uniqueName(tr("Copy of %1").arg(m_entries[sourceRow].displayName));

    _copyDir(configDir(sourceId), configDir(newId));

    beginInsertRows({}, m_entries.size(), m_entries.size());
    m_entries.append({newId, newName});
    endInsertRows();

    _saveIndex();
    return newId;
}

bool ConfigManager::removeConfig(const QString &internalId)
{
    if (m_entries.size() <= 1) {
        return false;
    }
    const int row = rowForId(internalId);
    if (row < 0) {
        return false;
    }

    // Pick a new current before removing so the selection is never invalid.
    const bool wasCurrent = (m_currentId == internalId);
    if (wasCurrent) {
        m_currentId = m_entries[row == 0 ? 1 : row - 1].internalId;
    }

    // Delete the config's data directory.
    QDir(m_baseDir.filePath(internalId)).removeRecursively();

    beginRemoveRows({}, row, row);
    m_entries.removeAt(row);
    endRemoveRows();

    _saveIndex();

    if (wasCurrent) {
        emit currentConfigChanged(m_currentId);
    }
    return true;
}

void ConfigManager::renameConfig(const QString &internalId, const QString &newName)
{
    const int row = rowForId(internalId);
    if (row < 0) {
        return;
    }
    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    m_entries[row].displayName = trimmed;
    const QModelIndex idx = index(row, 0);
    emit dataChanged(idx, idx, {Qt::DisplayRole, Qt::EditRole});
    _saveIndex();
}

void ConfigManager::selectConfig(const QString &internalId)
{
    if (m_currentId == internalId || rowForId(internalId) < 0) {
        return;
    }
    m_currentId = internalId;
    _saveIndex();
    emit currentConfigChanged(m_currentId);
}

// ---------------------------------------------------------------------------
// QAbstractTableModel
// ---------------------------------------------------------------------------

int ConfigManager::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

int ConfigManager::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : 1;
}

QVariant ConfigManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
        return {};
    }
    const auto &entry = m_entries[index.row()];
    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
        return entry.displayName;
    case InternalIdRole:
        return entry.internalId;
    default:
        return {};
    }
}

bool ConfigManager::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role != Qt::EditRole || !index.isValid()
        || index.row() < 0 || index.row() >= m_entries.size()) {
        return false;
    }
    const QString trimmed = value.toString().trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    m_entries[index.row()].displayName = trimmed;
    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
    _saveIndex();
    return true;
}

QVariant ConfigManager::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole && section == 0) {
        return tr("Config");
    }
    return QAbstractTableModel::headerData(section, orientation, role);
}

Qt::ItemFlags ConfigManager::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void ConfigManager::_load()
{
    m_entries.clear();
    QSettings s(m_baseDir.filePath(QStringLiteral("index.ini")), QSettings::IniFormat);

    m_currentId = s.value(QStringLiteral("General/current")).toString();

    const int n = s.beginReadArray(QStringLiteral("configs"));
    for (int i = 0; i < n; ++i) {
        s.setArrayIndex(i);
        ConfigEntry e;
        e.internalId  = s.value(QStringLiteral("internalId")).toString();
        e.displayName = s.value(QStringLiteral("displayName")).toString();
        if (!e.internalId.isEmpty() && !e.displayName.isEmpty()) {
            m_entries.append(e);
        }
    }
    s.endArray();
}

void ConfigManager::_saveIndex() const
{
    QSettings s(m_baseDir.filePath(QStringLiteral("index.ini")), QSettings::IniFormat);
    s.setValue(QStringLiteral("General/current"), m_currentId);
    s.beginWriteArray(QStringLiteral("configs"), m_entries.size());
    for (int i = 0; i < m_entries.size(); ++i) {
        s.setArrayIndex(i);
        s.setValue(QStringLiteral("internalId"),  m_entries[i].internalId);
        s.setValue(QStringLiteral("displayName"), m_entries[i].displayName);
    }
    s.endArray();
}

void ConfigManager::_ensureDefault()
{
    if (!m_entries.isEmpty()) {
        // Make sure currentId points to an existing entry.
        if (rowForId(m_currentId) < 0) {
            m_currentId = m_entries.first().internalId;
            _saveIndex();
        }
        return;
    }
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_entries.append({id, tr("Default")});
    m_currentId = id;
    _saveIndex();
}

QString ConfigManager::_uniqueName(const QString &baseName) const
{
    QStringList names;
    for (const auto &e : m_entries) {
        names << e.displayName;
    }
    if (!names.contains(baseName)) {
        return baseName;
    }
    for (int n = 2; ; ++n) {
        const QString candidate = QStringLiteral("%1 (%2)").arg(baseName).arg(n);
        if (!names.contains(candidate)) {
            return candidate;
        }
    }
}

void ConfigManager::_copyDir(const QDir &src, const QDir &dst)
{
    if (!src.exists()) {
        return;
    }
    dst.mkpath(QStringLiteral("."));
    const QFileInfoList entries =
        src.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &fi : entries) {
        if (fi.isDir()) {
            _copyDir(QDir(fi.filePath()), QDir(dst.filePath(fi.fileName())));
        } else {
            QFile::copy(fi.filePath(), dst.filePath(fi.fileName()));
        }
    }
}
