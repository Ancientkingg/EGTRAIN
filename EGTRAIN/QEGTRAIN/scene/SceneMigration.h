#ifndef SCENEMIGRATION_H
#define SCENEMIGRATION_H

#include "scene/SceneCompatibility.h"

#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

/// What a migration step upgrades.
enum class SceneMigrationStepKind {
	Schema, ///< The schema_version of the scene files.
	Bundle, ///< The bundle_version of an .egscene layout.
};

/// One step that upgrades a staged copy of a scene from one version to a higher one.
struct SceneMigrationStep {
	/// Rewrites the scene directory stagedScene in place. Returns false on failure and may add diagnostics.
	using Transform = std::function<bool(const std::filesystem::path& stagedScene,
		std::vector<SceneDiagnostic>& diagnostics)>;

	int fromVersion;	 ///< The version it reads.
	int toVersion;		 ///< The version it writes.
	std::string id;		 ///< Name used in diagnostics; may be empty.
	Transform transform; ///< The operation.

	/// A step from version from to version to.
	SceneMigrationStep(int from, int to, Transform operation, std::string stepId = {})
		: fromVersion(from), toVersion(to), id(std::move(stepId)),
		  transform(std::move(operation)) {}
};

/// The migration steps a migration may chain.
class SceneMigrationRegistry {
public:
	/// Adds a schema step. A step that does not raise the version or has no operation is ignored.
	void addSchemaStep(SceneMigrationStep step);
	/// Adds a bundle step. A step that does not raise the version or has no operation is ignored.
	void addBundleStep(SceneMigrationStep step);

	const std::vector<SceneMigrationStep>& schemaSteps() const { return m_schemaSteps; } ///< The schema steps added.
	const std::vector<SceneMigrationStep>& bundleSteps() const { return m_bundleSteps; } ///< The bundle steps added.

private:
	std::vector<SceneMigrationStep> m_schemaSteps;
	std::vector<SceneMigrationStep> m_bundleSteps;
};

/// The shipped registry. It is empty until a product migration is
/// reviewed and registered.
const SceneMigrationRegistry& productionSceneMigrationRegistry();

/// True when steps of this kind chain from fromVersion up to toVersion, which must be higher.
bool sceneMigrationPathAvailable(const SceneMigrationRegistry& registry,
	SceneMigrationStepKind kind, int fromVersion, int toVersion);
/// Runs the chain of steps of this kind from fromVersion to toVersion on the staged scene directory.
/// Equal versions do nothing. Returns false, with a diagnostic, for a downgrade, a missing path or a failed step.
bool applySceneMigrationChain(const SceneMigrationRegistry& registry,
	SceneMigrationStepKind kind, int fromVersion, int toVersion,
	const std::filesystem::path& stagedScene,
	std::vector<SceneDiagnostic>& diagnostics);

/// The result of migrateSceneCopy.
struct SceneMigrationResult {
	bool migrated = false;					  ///< True when the upgraded copy was written.
	std::vector<SceneDiagnostic> diagnostics; ///< What the probe, the steps, loading, validation and saving found.

	bool success() const; ///< True when migrated is set and no diagnostic is an error.
};

/// Upgrades a copy of an older scene directory or bundle at sourcePath and writes it to destinationPath,
/// as a bundle when that path ends in .egscene. The source is not changed. The copy is written only when
/// the upgraded scene loads and validates without errors.
SceneMigrationResult migrateSceneCopy(const std::string& sourcePath,
	const std::string& destinationPath,
	const SceneMigrationRegistry& registry = productionSceneMigrationRegistry());

#endif // SCENEMIGRATION_H
