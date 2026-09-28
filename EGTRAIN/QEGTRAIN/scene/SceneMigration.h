#ifndef SCENEMIGRATION_H
#define SCENEMIGRATION_H

#include "scene/SceneCompatibility.h"

#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

enum class SceneMigrationStepKind {
	Schema,
	Bundle,
};

struct SceneMigrationStep {
	using Transform = std::function<bool(const std::filesystem::path& stagedScene,
			std::vector<SceneDiagnostic>& diagnostics)>;

	int fromVersion;
	int toVersion;
	std::string id;
	Transform transform;

	SceneMigrationStep(int from, int to, Transform operation, std::string stepId = {})
		: fromVersion(from), toVersion(to), id(std::move(stepId)),
		  transform(std::move(operation)) {}
};

class SceneMigrationRegistry {
public:
	void addSchemaStep(SceneMigrationStep step);
	void addBundleStep(SceneMigrationStep step);

	const std::vector<SceneMigrationStep>& schemaSteps() const { return m_schemaSteps; }
	const std::vector<SceneMigrationStep>& bundleSteps() const { return m_bundleSteps; }

private:
	std::vector<SceneMigrationStep> m_schemaSteps;
	std::vector<SceneMigrationStep> m_bundleSteps;
};

// The shipped registry is deliberately empty until a product migration is
// explicitly reviewed and registered.
const SceneMigrationRegistry& productionSceneMigrationRegistry();

bool sceneMigrationPathAvailable(const SceneMigrationRegistry& registry,
		SceneMigrationStepKind kind, int fromVersion, int toVersion);
bool applySceneMigrationChain(const SceneMigrationRegistry& registry,
		SceneMigrationStepKind kind, int fromVersion, int toVersion,
		const std::filesystem::path& stagedScene,
		std::vector<SceneDiagnostic>& diagnostics);

struct SceneMigrationResult {
	bool migrated = false;
	std::vector<SceneDiagnostic> diagnostics;

	bool success() const;
};

SceneMigrationResult migrateSceneCopy(const std::string& sourcePath,
	const std::string& destinationPath,
		const SceneMigrationRegistry& registry = productionSceneMigrationRegistry());

#endif // SCENEMIGRATION_H
