#include "editors/TrainUnitSourceLinker.h"

#include "scene/SceneImporter.h"
#include "scene/SceneReferences.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QScopedValueRollback>

#include <algorithm>
#include <set>
#include <utility>

namespace {

QString sourceFileSignature(const QString& path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return QStringLiteral("<missing>");
	return QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex());
}

QString absoluteSourcePath(const QString& path) {
	return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool sameTrainPhysical(const SceneTrainPhysical& left, const SceneTrainPhysical& right) {
	return left.mass_of_traction_unit_kg == right.mass_of_traction_unit_kg
		&& left.mass_of_a_wagon_kg == right.mass_of_a_wagon_kg
		&& left.number_of_wagons == right.number_of_wagons
		&& left.max_speed_ms == right.max_speed_ms
		&& left.max_deceleration_ms2 == right.max_deceleration_ms2
		&& left.frontal_area_m2 == right.frontal_area_m2
		&& left.resistance_coefficient == right.resistance_coefficient
		&& left.jerk_ms3 == right.jerk_ms3
		&& left.length_m == right.length_m;
}

} // namespace

TrainUnitSourceLinker::TrainUnitSourceLinker(QObject* parent) : QObject(parent) {
	m_debounceTimer.setSingleShot(true);
	m_debounceTimer.setInterval(150);
	connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString& path) { scheduleChange(path); });
	connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString& path) { scheduleChange(path); });
	connect(&m_debounceTimer, &QTimer::timeout, this, &TrainUnitSourceLinker::processChanges);
}

void TrainUnitSourceLinker::setDocument(SceneDocument* document) {
	QObject::disconnect(m_replacedConnection);
	m_document = document;
	if (m_document)
		m_replacedConnection = connect(m_document, &SceneDocument::replaced, this, &TrainUnitSourceLinker::clear);
}

bool TrainUnitSourceLinker::loaded() const {
	return m_document && m_document->isLoaded();
}

bool TrainUnitSourceLinker::runActive() const {
	return m_document && !m_document->isEditable();
}

const TrainUnitSourceLinker::Link* TrainUnitSourceLinker::linkOf(const std::string& unitId) const {
	const auto it = m_links.find(unitId);
	return it == m_links.end() ? nullptr : &it->second;
}

void TrainUnitSourceLinker::link(std::string unitId, bool traction) {
	if (!loaded() || !sceneTrainUnitById(m_document->model(), unitId))
		return;
	const quint64 sceneRevision = m_document->revision();
	const auto it = m_links.find(unitId);
	const QString oldPath = it == m_links.end() ? QString() : (traction ? it->second.tractionPath : it->second.dataPath);
	const QString selected = QFileDialog::getOpenFileName(m_dialogParent,
		traction ? QStringLiteral("Link traction source") : QStringLiteral("Link parameter source"),
		oldPath.isEmpty() ? QDir::homePath() : QFileInfo(oldPath).absolutePath());
	if (selected.isEmpty() || !loaded() || m_document->revision() != sceneRevision)
		return;
	linkFile(unitId, traction, selected);
}

void TrainUnitSourceLinker::linkFile(const std::string& unitId, bool traction, const QString& selected) {
	if (!loaded())
		return;
	const SceneTrainUnit* unit = sceneTrainUnitById(m_document->model(), unitId);
	if (!unit)
		return;
	const QString path = absoluteSourcePath(selected);
	Link& state = m_links[unitId];
	++state.generation;
	QString* linkedPath = traction ? &state.tractionPath : &state.dataPath;
	QString* signature = traction ? &state.tractionSignature : &state.dataSignature;
	QString* status = traction ? &state.tractionStatus : &state.dataStatus;
	if (*linkedPath != path) {
		signature->clear();
		status->clear();
	}
	if (traction)
		state.acceptedTraction = unit->tractionCurve;
	else
		state.acceptedPhysical = unit->physical;
	*linkedPath = path;
	const bool deferred = runActive();
	if (traction)
		state.tractionDeferred = deferred;
	else
		state.dataDeferred = deferred;
	*status = deferred ? QStringLiteral("Linked; pending reload after run") : QStringLiteral("Linked; waiting for reload");
	refreshWatches();
	m_pendingPaths.insert(path);
	m_debounceTimer.start();
	emit statusChanged();
}

void TrainUnitSourceLinker::unlink(const std::string& unitId, bool traction) {
	const auto it = m_links.find(unitId);
	if (!loaded() || it == m_links.end())
		return;
	++it->second.generation;
	if (traction) {
		it->second.tractionPath.clear();
		it->second.tractionSignature.clear();
		it->second.acceptedTraction.clear();
		it->second.tractionStatus.clear();
		it->second.tractionDeferred = false;
	} else {
		it->second.dataPath.clear();
		it->second.dataSignature.clear();
		it->second.acceptedPhysical.reset();
		it->second.dataStatus.clear();
		it->second.dataDeferred = false;
	}
	if (it->second.dataPath.isEmpty() && it->second.tractionPath.isEmpty())
		m_links.erase(it);
	refreshWatches();
	emit statusChanged();
}

void TrainUnitSourceLinker::retry(const std::string& unitId, bool traction) {
	const auto it = m_links.find(unitId);
	if (!loaded() || it == m_links.end())
		return;
	const QString path = traction ? it->second.tractionPath : it->second.dataPath;
	if (path.isEmpty())
		return;
	const bool deferred = runActive();
	const QString status = deferred ? QStringLiteral("Retry pending after run") : QStringLiteral("Retrying");
	if (traction) {
		it->second.tractionSignature.clear();
		it->second.tractionDeferred = deferred;
		it->second.tractionStatus = status;
	} else {
		it->second.dataSignature.clear();
		it->second.dataDeferred = deferred;
		it->second.dataStatus = status;
	}
	refreshWatches();
	m_pendingPaths.insert(path);
	m_debounceTimer.start();
	emit statusChanged();
}

void TrainUnitSourceLinker::renameUnit(const std::string& oldId, const std::string& newId) {
	const auto it = m_links.find(oldId);
	if (it != m_links.end()) {
		Link state = std::move(it->second);
		m_links.erase(it);
		m_links.emplace(newId, std::move(state));
	}
	refreshWatches();
	emit statusChanged();
}

void TrainUnitSourceLinker::forgetUnit(const std::string& unitId) {
	m_links.erase(unitId);
	refreshWatches();
	emit statusChanged();
}

void TrainUnitSourceLinker::clear() {
	m_debounceTimer.stop();
	m_pendingPaths.clear();
	m_links.clear();
	const QStringList watched = m_watcher.files() + m_watcher.directories();
	if (!watched.isEmpty())
		m_watcher.removePaths(watched);
	emit linksCleared();
	emit statusChanged();
}

void TrainUnitSourceLinker::refreshWatches() {
	QSet<QString> files;
	QSet<QString> directories;
	for (const auto& entry : m_links) {
		const Link& state = entry.second;
		for (const QString& path : {state.dataPath, state.tractionPath}) {
			if (path.isEmpty())
				continue;
			if (QFileInfo(path).isFile())
				files.insert(path);
			const QString parent = QFileInfo(path).absolutePath();
			if (!parent.isEmpty())
				directories.insert(parent);
		}
	}
	const auto reconcile = [this](const QStringList& watched, const QSet<QString>& wanted) {
		for (const QString& path : watched)
			if (!wanted.contains(path))
				m_watcher.removePath(path);
		for (const QString& path : wanted)
			if (!watched.contains(path))
				m_watcher.addPath(path);
	};
	reconcile(m_watcher.files(), files);
	reconcile(m_watcher.directories(), directories);
}

void TrainUnitSourceLinker::scheduleChange(const QString& changedPath) {
	const QString path = absoluteSourcePath(changedPath);
	const bool running = runActive();
	for (auto& entry : m_links) {
		Link& state = entry.second;
		const auto consider = [this, &path, running](const QString& linkedPath, bool traction, Link& current) {
			if (linkedPath.isEmpty())
				return;
			const QString parent = QFileInfo(linkedPath).absolutePath();
			if (path != linkedPath && path != parent)
				return;
			m_pendingPaths.insert(linkedPath);
			if (running && sourceFileSignature(linkedPath) != (traction ? current.tractionSignature : current.dataSignature)) {
				if (traction) {
					current.tractionDeferred = true;
					current.tractionStatus = QStringLiteral("Changed during run; pending reload");
				} else {
					current.dataDeferred = true;
					current.dataStatus = QStringLiteral("Changed during run; pending reload");
				}
			}
		};
		consider(state.dataPath, false, state);
		consider(state.tractionPath, true, state);
	}
	if (m_pendingPaths.isEmpty())
		return;
	if (running)
		emit statusChanged();
	m_debounceTimer.start();
}

void TrainUnitSourceLinker::processChanges() {
	if (m_processing || m_pendingPaths.isEmpty() || runActive())
		return;
	m_debounceTimer.stop();
	QScopedValueRollback<bool> processing(m_processing, true);
	const QSet<QString> pending = m_pendingPaths;
	m_pendingPaths.clear();
	std::vector<std::pair<QString, bool>> associations;
	for (const auto& entry : m_links) {
		for (const auto& association : std::array<std::pair<QString, bool>, 2>{
				 std::make_pair(entry.second.dataPath, false), std::make_pair(entry.second.tractionPath, true)}) {
			if (!association.first.isEmpty() && pending.contains(association.first))
				associations.push_back(association);
		}
	}
	QSet<QString> processed;
	for (const auto& association : associations) {
		const QString key = QString(association.second ? "traction\n" : "physical\n") + association.first;
		if (processed.contains(key))
			continue;
		processed.insert(key);
		processFile(association.first, association.second);
	}
	if (!m_pendingPaths.isEmpty())
		m_debounceTimer.start();
}

void TrainUnitSourceLinker::processFile(const QString& path, bool traction) {
	if (!loaded() || runActive())
		return;
	emit commitPendingEditsRequested();
	std::vector<std::string> affected;
	for (const auto& entry : m_links) {
		const QString linkedPath = traction ? entry.second.tractionPath : entry.second.dataPath;
		if (linkedPath == path)
			affected.push_back(entry.first);
	}
	if (affected.empty()) {
		refreshWatches();
		return;
	}
	const QString signature = sourceFileSignature(path);
	bool needsProcessing = false;
	for (const std::string& unitId : affected) {
		const auto it = m_links.find(unitId);
		if (it == m_links.end())
			continue;
		const QString handled = traction ? it->second.tractionSignature : it->second.dataSignature;
		needsProcessing = needsProcessing || handled != signature;
	}
	if (!needsProcessing) {
		for (const std::string& unitId : affected) {
			auto& state = m_links.at(unitId);
			bool& deferred = traction ? state.tractionDeferred : state.dataDeferred;
			if (deferred) {
				(traction ? state.tractionStatus : state.dataStatus) = QStringLiteral("Up to date");
				deferred = false;
			}
		}
		refreshWatches();
		emit statusChanged();
		return;
	}

	SceneTrainPhysicalSourceResult physicalResult;
	SceneTrainTractionSourceResult tractionResult;
	if (traction)
		tractionResult = parseTrainTractionSourceFile(path.toStdString());
	else
		physicalResult = parseTrainPhysicalSourceFile(path.toStdString());
	const bool parsed = traction ? tractionResult.success() : physicalResult.success();
	const QString error = traction ? QString::fromStdString(tractionResult.error)
								   : QString::fromStdString(physicalResult.error);
	if (!parsed) {
		for (const std::string& unitId : affected) {
			auto it = m_links.find(unitId);
			if (it == m_links.end() || (traction ? it->second.tractionPath : it->second.dataPath) != path)
				continue;
			if (traction) {
				it->second.tractionSignature = signature;
				it->second.tractionStatus = QStringLiteral("Rejected: %1; Retry available").arg(error);
				it->second.tractionDeferred = false;
			} else {
				it->second.dataSignature = signature;
				it->second.dataStatus = QStringLiteral("Rejected: %1; Retry available").arg(error);
				it->second.dataDeferred = false;
			}
		}
		refreshWatches();
		emit statusChanged();
		emit statusMessage(QString("Source reload rejected: %1").arg(error), 8000);
		return;
	}

	QStringList conflicts;
	QStringList affectedNames;
	std::map<std::string, quint64> generations;
	bool deferredChange = false;
	for (const std::string& unitId : affected) {
		const auto it = m_links.find(unitId);
		if (it == m_links.end() || (traction ? it->second.tractionPath : it->second.dataPath) != path)
			continue;
		affectedNames << QString::fromStdString(unitId);
		generations.emplace(unitId, it->second.generation);
		deferredChange = deferredChange || (traction ? it->second.tractionDeferred : it->second.dataDeferred);
		const SceneTrainUnit* unit = sceneTrainUnitById(m_document->model(), unitId);
		if (!unit)
			continue;
		const bool localChanged = traction
			? (unit->tractionCurve != it->second.acceptedTraction)
			: (it->second.acceptedPhysical && !sameTrainPhysical(unit->physical, *it->second.acceptedPhysical));
		const bool candidateChanged = traction
			? unit->tractionCurve != tractionResult.tractionCurve
			: !sameTrainPhysical(unit->physical, physicalResult.physical);
		if (localChanged && candidateChanged)
			conflicts << QString::fromStdString(unitId);
	}
	if (affectedNames.isEmpty()) {
		refreshWatches();
		return;
	}
	const quint64 sceneRevisionAtDecision = m_document->revision();
	bool reload = true;
	if (!conflicts.isEmpty() || deferredChange) {
		QMessageBox dialog(m_dialogParent);
		dialog.setIcon(QMessageBox::Question);
		dialog.setWindowTitle("Linked source changed");
		dialog.setText(QString("%1 changed for %2.")
				.arg(traction ? QStringLiteral("The traction source") : QStringLiteral("The parameter source"),
					affectedNames.join(", ")));
		dialog.setInformativeText(conflicts.isEmpty()
				? QStringLiteral("The source changed during a run. Reload the external values or keep local edits?")
				: QStringLiteral("Local edits conflict in %1. Reload the external values or keep local edits?")
					  .arg(conflicts.join(", ")));
		QPushButton* reloadButton = dialog.addButton("Reload", QMessageBox::AcceptRole);
		QPushButton* keepButton = dialog.addButton("Keep local", QMessageBox::RejectRole);
		dialog.setDefaultButton(keepButton);
		dialog.exec();
		reload = dialog.clickedButton() == reloadButton;
	}
	const auto requeueCurrentAssociations = [this, &path, &affected, traction]() {
		for (const auto& entry : m_links) {
			const QString currentPath = traction ? entry.second.tractionPath : entry.second.dataPath;
			if (currentPath == path || std::find(affected.begin(), affected.end(), entry.first) != affected.end()) {
				if (!currentPath.isEmpty())
					m_pendingPaths.insert(currentPath);
			}
		}
		refreshWatches();
	};
	bool staleDecision = !loaded() || runActive() || m_document->revision() != sceneRevisionAtDecision;
	if (!staleDecision) {
		for (const std::string& unitId : affected) {
			const auto it = m_links.find(unitId);
			const auto generation = generations.find(unitId);
			if (it == m_links.end() || generation == generations.end()
				|| (traction ? it->second.tractionPath : it->second.dataPath) != path
				|| it->second.generation != generation->second) {
				staleDecision = true;
				break;
			}
		}
	}
	if (staleDecision) {
		requeueCurrentAssociations();
		emit statusChanged();
		return;
	}
	if (!reload) {
		for (const std::string& unitId : affected) {
			auto it = m_links.find(unitId);
			if (it == m_links.end() || (traction ? it->second.tractionPath : it->second.dataPath) != path)
				continue;
			if (traction) {
				it->second.tractionSignature = signature;
				it->second.tractionStatus = QStringLiteral("Kept local; Retry available");
				it->second.tractionDeferred = false;
			} else {
				it->second.dataSignature = signature;
				it->second.dataStatus = QStringLiteral("Kept local; Retry available");
				it->second.dataDeferred = false;
			}
		}
		refreshWatches();
		emit statusChanged();
		return;
	}

	bool changed = false;
	std::set<std::string> changedTractionUnits;
	for (const std::string& unitId : affected) {
		auto linkIt = m_links.find(unitId);
		if (linkIt == m_links.end())
			continue;
		SceneTrainUnit* unit = nullptr;
		for (auto& candidate : m_document->mutableModel().trainUnits) {
			if (candidate.id == unitId) {
				unit = &candidate;
				break;
			}
		}
		if (!unit)
			continue;
		if (traction) {
			const bool unitChanged = unit->tractionCurve != tractionResult.tractionCurve;
			if (unitChanged) {
				unit->tractionCurve = tractionResult.tractionCurve;
				changed = true;
				changedTractionUnits.insert(unitId);
			}
			linkIt->second.acceptedTraction = tractionResult.tractionCurve;
			linkIt->second.tractionSignature = signature;
			linkIt->second.tractionStatus = unitChanged ? QStringLiteral("Reloaded") : QStringLiteral("Up to date");
			linkIt->second.tractionDeferred = false;
		} else {
			const bool unitChanged = !sameTrainPhysical(unit->physical, physicalResult.physical);
			if (unitChanged) {
				unit->physical = physicalResult.physical;
				unit->hasPhysical = true;
				changed = true;
			}
			linkIt->second.acceptedPhysical = physicalResult.physical;
			linkIt->second.dataSignature = signature;
			linkIt->second.dataStatus = unitChanged ? QStringLiteral("Reloaded") : QStringLiteral("Up to date");
			linkIt->second.dataDeferred = false;
		}
	}
	if (changed)
		m_document->markChanged(SceneDocument::TrainUnits, "Reload linked source");
	QStringList changedTractionNames;
	for (const std::string& unitId : changedTractionUnits)
		changedTractionNames << QString::fromStdString(unitId);
	emit reloaded(changedTractionNames);
	refreshWatches();
	emit statusChanged();
}
