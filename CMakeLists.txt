cmake_minimum_required(VERSION 3.16)

project(PersonOS VERSION 0.1 LANGUAGES CXX)

# C++20（README 2.7.4 技术栈要求）
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Sql Network Test)

qt_standard_project_setup(REQUIRES 6.10)

# ================= v1.1 模块化底座（DD-001）=================
# 新代码遵守 presentation -> application -> domain；infrastructure 只实现端口。
# 旧 personos_core 在渐进迁移期间继续存在，但不得继续承担新的跨模块职责。
add_library(personos_domain STATIC
    src/domain/foundation/Clock.h
    src/domain/foundation/Uid.cpp
    src/domain/foundation/Uid.h
    src/domain/goal/Goal.h
    src/domain/route/Route.h
    src/domain/mel/Mel.h
    src/domain/review/Review.h
    src/domain/state/State.h
    src/domain/assessment/Assessment.h
    src/domain/knowledge/Knowledge.h
    src/domain/knowledge/FileAsset.h
    src/domain/knowledge/Retrieval.h
    src/domain/knowledge/Ingest.h
    src/domain/ai/Ai.h
    src/domain/operations/Operations.h
    src/domain/assessment/Retention.h
    src/domain/goal/ContentMap.h
    src/domain/review/Questionnaire.h
    src/domain/state/Calibration.h
)
target_include_directories(personos_domain PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(personos_application INTERFACE)
target_sources(personos_application INTERFACE
    src/application/foundation/ApplicationError.h
    src/application/foundation/Result.h
    src/application/audit/AuditEvent.h
    src/application/audit/Audit.h
    src/application/audit/Audit.cpp
    src/application/ports/AuditRepository.h
    src/application/ports/AiConfigStore.h
    src/application/ports/UnitOfWork.h
    src/application/ports/UuidPort.h
    src/application/ports/GoalRepository.h
    src/application/ports/RouteRepository.h
    src/application/usecases/goal/GoalUseCases.h
    src/application/usecases/goal/GoalUseCases.cpp
    src/application/usecases/route/RouteUseCases.h
    src/application/usecases/route/RouteUseCases.cpp
    src/application/usecases/mel/MelUseCases.h
    src/application/usecases/mel/MelUseCases.cpp
    src/application/ports/MelRepository.h
    src/application/usecases/review/ReviewUseCases.h
    src/application/usecases/review/ReviewUseCases.cpp
    src/application/ports/ReviewRepository.h
    src/application/usecases/state/StateUseCases.h
    src/application/usecases/state/StateUseCases.cpp
    src/application/ports/StateRepository.h
    src/application/usecases/assessment/AssessmentUseCases.h
    src/application/usecases/assessment/AssessmentUseCases.cpp
    src/application/ports/AssessmentRepository.h
    src/application/usecases/assessment/RetentionUseCases.h
    src/application/usecases/assessment/RetentionUseCases.cpp
    src/application/ports/RetentionRepository.h
    src/application/usecases/goal/ContentMapUseCases.h
    src/application/usecases/goal/ContentMapUseCases.cpp
    src/application/ports/ContentMapRepository.h
    src/application/usecases/review/QuestionnaireUseCases.h
    src/application/usecases/review/QuestionnaireUseCases.cpp
    src/application/ports/QuestionnaireRepository.h
    src/application/usecases/state/CalibrationUseCases.h
    src/application/usecases/state/CalibrationUseCases.cpp
    src/application/ports/CalibrationRepository.h
    src/application/usecases/detection/DetectionUseCases.h
    src/application/usecases/detection/DetectionUseCases.cpp
    src/application/usecases/advice/AdviceUseCases.h
    src/application/usecases/advice/AdviceUseCases.cpp
    src/application/usecases/domain/DomainRegistry.h
    src/application/usecases/domain/DomainRegistry.cpp
    src/application/usecases/domain/StateContextBuilder.h
    src/application/usecases/domain/StateContextBuilder.cpp
    src/application/usecases/domain/DecisionValidator.h
    src/application/usecases/domain/DecisionValidator.cpp
    src/application/ports/DomainManifestRepository.h
    src/application/ports/EmbeddingPort.h
    src/application/usecases/planning/AiPlanningUseCases.h
    src/application/usecases/planning/AiPlanningUseCases.cpp
    src/application/usecases/migration/LegacyMigration.h
    src/application/usecases/migration/LegacyMigration.cpp
    src/application/usecases/knowledge/KnowledgeUseCases.h
    src/application/usecases/knowledge/KnowledgeUseCases.cpp
    src/application/ports/KnowledgeRepository.h
    src/application/ports/SearchIndexPort.h
    src/application/ports/KnowledgeRetrievalPort.h
    src/application/foundation/SupportAssessor.h
    src/application/ports/AiPorts.h
    src/application/foundation/ContractValidator.h
    src/application/foundation/ContractValidator.cpp
    src/application/foundation/JsonSchemaValidator.h
    src/application/foundation/JsonSchemaValidator.cpp
    src/application/ports/ContentPorts.h
    src/application/usecases/knowledge/ContentIngestUseCases.h
    src/application/usecases/knowledge/ContentIngestUseCases.cpp
    src/application/usecases/knowledge/MethodDerivationUseCases.h
    src/application/usecases/knowledge/MethodDerivationUseCases.cpp
    src/application/usecases/knowledge/PaperAnalysisUseCases.h
    src/application/usecases/knowledge/PaperAnalysisUseCases.cpp
    src/application/ports/OperationPorts.h
    src/application/usecases/operations/OperationUseCases.h
    src/application/usecases/operations/OperationUseCases.cpp
)
target_include_directories(personos_application INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(personos_application INTERFACE personos_domain)

add_library(personos_infrastructure STATIC
    src/infrastructure/foundation/QtSystemClock.cpp
    src/infrastructure/foundation/QtSystemClock.h
    src/infrastructure/foundation/QtUidGenerator.cpp
    src/infrastructure/foundation/QtUidGenerator.h
    src/infrastructure/foundation/TimeFormat.cpp
    src/infrastructure/foundation/TimeFormat.h
    src/infrastructure/persistence/DatabaseConnectionFactory.cpp
    src/infrastructure/persistence/DatabaseConnectionFactory.h
    src/infrastructure/persistence/MigrationRunner.cpp
    src/infrastructure/persistence/MigrationRunner.h
    src/infrastructure/persistence/SqlUnitOfWork.cpp
    src/infrastructure/persistence/SqlUnitOfWork.h
    src/infrastructure/persistence/SqlGoalRepository.cpp
    src/infrastructure/persistence/SqlGoalRepository.h
    src/infrastructure/persistence/SqlRouteRepository.cpp
    src/infrastructure/persistence/SqlRouteRepository.h
    src/infrastructure/persistence/LegacyGoalReader.cpp
    src/infrastructure/persistence/LegacyGoalReader.h
    src/infrastructure/persistence/SqlMelRepository.cpp
    src/infrastructure/persistence/SqlMelRepository.h
    src/infrastructure/persistence/SqlReviewRepository.cpp
    src/infrastructure/persistence/SqlReviewRepository.h
    src/infrastructure/persistence/SqlStateRepository.cpp
    src/infrastructure/persistence/SqlStateRepository.h
    src/infrastructure/knowledge/StateDefinitionsSeed.cpp
    src/infrastructure/knowledge/StateDefinitionsSeed.h
    src/infrastructure/persistence/SqlAssessmentRepository.cpp
    src/infrastructure/persistence/SqlAssessmentRepository.h
    src/infrastructure/persistence/SqlKnowledgeRepository.cpp
    src/infrastructure/persistence/SqlKnowledgeRepository.h
    src/infrastructure/persistence/SqlFileAssetRepository.cpp
    src/infrastructure/persistence/SqlFileAssetRepository.h
    src/infrastructure/knowledge/SqlKnowledgeFtsIndex.cpp
    src/infrastructure/knowledge/SqlKnowledgeFtsIndex.h
    src/infrastructure/knowledge/CjkTokenize.h
    src/infrastructure/knowledge/SimplePdfTextExtractor.cpp
    src/infrastructure/knowledge/SimplePdfTextExtractor.h
    src/infrastructure/knowledge/ReliablePdfTextExtractor.cpp
    src/infrastructure/knowledge/ReliablePdfTextExtractor.h
    src/infrastructure/knowledge/SqlEmbeddingRepository.cpp
    src/infrastructure/knowledge/SqlEmbeddingRepository.h
    src/infrastructure/knowledge/CorpusPipeline.cpp
    src/infrastructure/knowledge/CorpusPipeline.h
    src/infrastructure/knowledge/MarkdownCorpusIndex.cpp
    src/infrastructure/knowledge/MarkdownCorpusIndex.h
    src/infrastructure/knowledge/KnowledgePackage.cpp
    src/infrastructure/knowledge/KnowledgePackage.h
    src/infrastructure/knowledge/SqlKnowledgeRetrieval.cpp
    src/infrastructure/knowledge/SqlKnowledgeRetrieval.h
    src/infrastructure/knowledge/SqlContentIngestRepository.cpp
    src/infrastructure/knowledge/SqlContentIngestRepository.h
    src/infrastructure/ai/SqlAiRepository.cpp
    src/infrastructure/ai/SqlAiRepository.h
    src/infrastructure/ai/AiGateway.cpp
    src/infrastructure/ai/AiGateway.h
    src/infrastructure/ai/WindowsCredentialStore.cpp
    src/infrastructure/ai/WindowsCredentialStore.h
    src/infrastructure/ai/OpenAiCompatibleProvider.cpp
    src/infrastructure/ai/OpenAiCompatibleProvider.h
    src/infrastructure/ai/HttpEmbeddingPort.cpp
    src/infrastructure/ai/HttpEmbeddingPort.h
    src/infrastructure/ai/ModelConnectionTestService.cpp
    src/infrastructure/ai/ModelConnectionTestService.h
    src/infrastructure/operations/SqlOperationsRepository.cpp
    src/infrastructure/operations/SqlOperationsRepository.h
    src/infrastructure/operations/SqliteBackup.cpp
    src/infrastructure/operations/SqliteBackup.h
    src/infrastructure/operations/DatabaseManagerSwitch.cpp
    src/infrastructure/operations/DatabaseManagerSwitch.h
    src/infrastructure/knowledge/LearningManifestSeed.cpp
    src/infrastructure/knowledge/LearningManifestSeed.h
    src/infrastructure/knowledge/ReviewQuestionnaireSeed.cpp
    src/infrastructure/knowledge/ReviewQuestionnaireSeed.h
    src/infrastructure/persistence/SqlRetentionRepository.cpp
    src/infrastructure/persistence/SqlRetentionRepository.h
    src/infrastructure/persistence/SqlContentMapRepository.cpp
    src/infrastructure/persistence/SqlContentMapRepository.h
    src/infrastructure/persistence/SqlQuestionnaireRepository.cpp
    src/infrastructure/persistence/SqlQuestionnaireRepository.h
    src/infrastructure/persistence/SqlCalibrationRepository.cpp
    src/infrastructure/persistence/SqlCalibrationRepository.h
    src/infrastructure/persistence/SqlAuditRepository.cpp
    src/infrastructure/persistence/SqlAuditRepository.h
    src/infrastructure/persistence/SqlDomainManifestRepository.cpp
    src/infrastructure/persistence/SqlDomainManifestRepository.h
)
target_include_directories(personos_infrastructure PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(personos_infrastructure
    PUBLIC personos_application personos_domain Qt6::Core Qt6::Gui Qt6::Sql Qt6::Network
)
# Windows 凭据库（Credential Manager）与 WinHTTP 传输
# （OpenAiCompatibleProvider 用系统 TLS 栈规避 CDN 风控误拦）
if(WIN32)
    target_link_libraries(personos_infrastructure PUBLIC advapi32 winhttp)
endif()

# ================= 核心库（领域模型 + 仓库 + 子系统服务）=================
qt_add_library(personos_core STATIC
    src/ai/AiConfig.cpp
    src/ai/AiConfig.h
    src/ai/AiEngine.cpp
    src/ai/AiEngine.h
    src/ai/OpenAiCompatibleAdapter.cpp
    src/ai/OpenAiCompatibleAdapter.h
    src/ai/ProviderAdapter.h
    src/database/DatabaseManager.cpp
    src/database/DatabaseManager.h
    src/database/RepoUtil.h
    src/database/CoreValueRepository.cpp
    src/database/CoreValueRepository.h
    src/database/PrincipleRepository.cpp
    src/database/PrincipleRepository.h
    src/database/GoalRepository.cpp
    src/database/GoalRepository.h
    src/database/ChangeLogRepository.cpp
    src/database/ChangeLogRepository.h
    src/database/EventRepository.cpp
    src/database/EventRepository.h
    src/database/PlanRepository.cpp
    src/database/PlanRepository.h
    src/database/ProposalRepository.cpp
    src/database/ProposalRepository.h
    src/database/ReviewRepository.cpp
    src/database/ReviewRepository.h
    src/database/StateRepository.cpp
    src/database/StateRepository.h
    src/database/TaskRepository.cpp
    src/database/TaskRepository.h
    src/database/VersionRepository.cpp
    src/database/VersionRepository.h
    src/evolution/EvolutionService.cpp
    src/evolution/EvolutionService.h
    src/execution/ExecutionService.cpp
    src/execution/ExecutionService.h
    src/feedback/FeedbackService.cpp
    src/feedback/FeedbackService.h
    src/goals/CoreAndGoalService.cpp
    src/goals/CoreAndGoalService.h
    src/planning/PlanningService.cpp
    src/planning/PlanningService.h
    src/review/ReviewService.cpp
    src/review/ReviewService.h
    src/services/ApplicationService.cpp
    src/services/ApplicationService.h
    src/services/Items.cpp
    src/services/Items.h
    src/state/StateService.cpp
    src/state/StateService.h
    src/models/ChangeLog.h
    src/models/CoreValue.h
    src/models/Event.h
    src/models/Goal.h
    src/models/Plan.h
    src/models/Principle.h
    src/models/Proposal.h
    src/models/Review.h
    src/models/StateSnapshot.h
    src/models/Task.h
    src/models/Version.h
    src/models/PersonalModelParam.h
    src/models/CalibrationRecord.h
    src/models/Metric.h
    src/models/GuardrailWarning.h
    src/database/PersonalModelRepository.cpp
    src/database/PersonalModelRepository.h
    src/database/CalibrationRepository.cpp
    src/database/CalibrationRepository.h
    src/database/MetricRepository.cpp
    src/database/MetricRepository.h
    src/database/GuardrailRepository.cpp
    src/database/GuardrailRepository.h
    src/state/PersonalModelService.cpp
    src/state/PersonalModelService.h
    src/execution/MetricsService.cpp
    src/execution/MetricsService.h
    src/planning/FeasibilityCalculator.cpp
    src/planning/FeasibilityCalculator.h
    src/feedback/GuardrailService.cpp
    src/feedback/GuardrailService.h
)

# 头文件统一以 src 为根：如 #include "database/DatabaseManager.h"
target_include_directories(personos_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(personos_core
    PUBLIC personos_domain personos_application personos_infrastructure
    PUBLIC Qt6::Core Qt6::Sql Qt6::Network
)

# ================= 表现层（页面 ViewModel；依赖方向 presentation->application）=================
add_library(personos_presentation STATIC
    src/presentation/viewmodels/DashboardViewModel.cpp
    src/presentation/viewmodels/DashboardViewModel.h
    src/presentation/viewmodels/ListItemModel.cpp
    src/presentation/viewmodels/ListItemModel.h
    src/presentation/viewmodels/VmSupport.cpp
    src/presentation/viewmodels/VmSupport.h
    src/presentation/viewmodels/AiServices.cpp
    src/presentation/viewmodels/AiServices.h
    src/presentation/viewmodels/GoalRouteViewModel.cpp
    src/presentation/viewmodels/GoalRouteViewModel.h
    src/presentation/viewmodels/MelViewModel.cpp
    src/presentation/viewmodels/MelViewModel.h
    src/presentation/viewmodels/CalendarViewModel.cpp
    src/presentation/viewmodels/CalendarViewModel.h
    src/presentation/viewmodels/ReviewViewModel.cpp
    src/presentation/viewmodels/ReviewViewModel.h
    src/presentation/viewmodels/KnowledgeViewModel.cpp
    src/presentation/viewmodels/KnowledgeViewModel.h
    src/presentation/viewmodels/AdvisorViewModel.cpp
    src/presentation/viewmodels/AdvisorViewModel.h
    src/presentation/viewmodels/HistoryAchievementViewModel.cpp
    src/presentation/viewmodels/HistoryAchievementViewModel.h
    src/presentation/viewmodels/SettingsViewModel.cpp
    src/presentation/viewmodels/SettingsViewModel.h
    src/presentation/viewmodels/ThemeTokens.cpp
    src/presentation/viewmodels/ThemeTokens.h
)
target_include_directories(personos_presentation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(personos_presentation
    PUBLIC personos_infrastructure personos_application personos_domain
    PUBLIC Qt6::Core Qt6::Gui Qt6::Sql
)
# ViewModel 的 moc 由 app/test 的 QML 模块（SOURCES 列表）生成，
# 本静态库不再重复生成，避免与 QML_ELEMENT 注册代码重复定义。
set_target_properties(personos_presentation PROPERTIES AUTOMOC OFF)

# QML_ELEMENT 注册所需的 Q_OBJECT 头文件（app 与 QML 冒烟测试共用同一列表）
set(PERSONOS_QML_REGISTERED_HEADERS
    src/presentation/viewmodels/DashboardViewModel.h
    src/presentation/viewmodels/GoalRouteViewModel.h
    src/presentation/viewmodels/MelViewModel.h
    src/presentation/viewmodels/CalendarViewModel.h
    src/presentation/viewmodels/ReviewViewModel.h
    src/presentation/viewmodels/KnowledgeViewModel.h
    src/presentation/viewmodels/AdvisorViewModel.h
    src/presentation/viewmodels/HistoryAchievementViewModel.h
    src/presentation/viewmodels/SettingsViewModel.h
    src/presentation/viewmodels/ListItemModel.h
    src/presentation/viewmodels/ThemeTokens.h
)

# ================= 应用（GUI）=================
qt_add_executable(appPersonOS
    src/application/main.cpp
)

set(PERSONOS_QML_FILES
    qml/Main.qml
    qml/components/PageHeader.qml
    qml/components/Card.qml
    qml/components/StatusBadge.qml
    qml/components/EmptyState.qml
    qml/components/InfoBanner.qml
    qml/components/StateViews.qml
    qml/components/AppProgressBar.qml
    qml/components/AppButton.qml
    qml/components/NavItem.qml
    qml/pages/DashboardPage.qml
    qml/pages/GoalRoutePage.qml
    qml/pages/MelPage.qml
    qml/pages/CalendarPage.qml
    qml/pages/ReviewPage.qml
    qml/pages/HistoryAchievementPage.qml
    qml/pages/AdvisorPage.qml
    qml/pages/KnowledgePage.qml
    qml/pages/SettingsPage.qml
)

qt_add_qml_module(appPersonOS
    URI PersonOS
    VERSION 1.0
    QML_FILES
        ${PERSONOS_QML_FILES}
    SOURCES
        ${PERSONOS_QML_REGISTERED_HEADERS}
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(appPersonOS PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appPersonOS
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appPersonOS
    PRIVATE Qt6::Quick
    PRIVATE personos_core
    PRIVATE personos_presentation
)
# QML_ELEMENT 生成的注册代码以头文件基名 include
target_include_directories(appPersonOS PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/presentation/viewmodels)

install(TARGETS appPersonOS
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# ================= 冒烟测试（控制台程序，输出可见）=================
# 逐步添加：Step 5 goals_smoke；Step 6 state_smoke；Step 13 起改为 Qt Test 完整测试
qt_add_executable(smoke_goals
    tests/goals_smoke.cpp
    tests/TestLogging.h
)

target_link_libraries(smoke_goals
    PRIVATE Qt6::Core
    PRIVATE personos_core
)

qt_add_executable(smoke_state
    tests/state_smoke.cpp
    tests/TestLogging.h
)

target_link_libraries(smoke_state
    PRIVATE Qt6::Core
    PRIVATE personos_core
)

qt_add_executable(smoke_planning
    tests/planning_smoke.cpp
    tests/TestLogging.h
)

target_link_libraries(smoke_planning
    PRIVATE Qt6::Core
    PRIVATE personos_core
)

qt_add_executable(smoke_execution
    tests/execution_smoke.cpp
    tests/TestLogging.h
)

target_link_libraries(smoke_execution
    PRIVATE Qt6::Core
    PRIVATE personos_core
)

qt_add_executable(smoke_feedback
    tests/feedback_smoke.cpp
    tests/TestLogging.h
)

target_link_libraries(smoke_feedback
    PRIVATE Qt6::Core
    PRIVATE personos_core
)

qt_add_executable(smoke_review
    tests/review_smoke.cpp
    tests/TestLogging.h
)

target_link_libraries(smoke_review
    PRIVATE Qt6::Core
    PRIVATE personos_core
)

qt_add_executable(smoke_ai
    tests/ai_smoke.cpp
    tests/TestLogging.h
)

target_link_libraries(smoke_ai
    PRIVATE Qt6::Core
    PRIVATE personos_core
)

qt_add_executable(smoke_evolution
    tests/evolution_smoke.cpp
    tests/TestLogging.h
)

target_link_libraries(smoke_evolution
    PRIVATE Qt6::Core
    PRIVATE personos_core
)

qt_add_executable(smoke_metrics
    tests/metrics_smoke.cpp
    tests/TestLogging.h
)

target_link_libraries(smoke_metrics
    PRIVATE Qt6::Core
    PRIVATE personos_core
)

qt_add_executable(smoke_feasibility
    tests/feasibility_smoke.cpp
    tests/TestLogging.h
)

target_link_libraries(smoke_feasibility
    PRIVATE Qt6::Core
    PRIVATE personos_core
)

qt_add_executable(smoke_guardrail
    tests/guardrail_smoke.cpp
    tests/TestLogging.h
)

target_link_libraries(smoke_guardrail
    PRIVATE Qt6::Core
    PRIVATE personos_core
)

# ================= MVP 闭环验收测试（Qt Test，README 3.14）=================
qt_add_executable(tst_mvp_loop
    tests/tst_mvp_loop.cpp
    tests/TestLogging.h
)

qt_add_executable(tst_foundation
    tests/tst_foundation.cpp
)

target_link_libraries(tst_foundation
    PRIVATE Qt6::Test Qt6::Core Qt6::Sql
    PRIVATE personos_infrastructure
)

qt_add_executable(tst_database_migrations
    tests/tst_database_migrations.cpp
)
target_link_libraries(tst_database_migrations
    PRIVATE Qt6::Test Qt6::Core Qt6::Sql
    PRIVATE personos_core
)

qt_add_executable(tst_goal_route
    tests/tst_goal_route.cpp
)
target_link_libraries(tst_goal_route
    PRIVATE Qt6::Test Qt6::Core Qt6::Sql
    PRIVATE personos_infrastructure personos_core
)

qt_add_executable(tst_mel
    tests/tst_mel.cpp
)
target_link_libraries(tst_mel
    PRIVATE Qt6::Test Qt6::Core Qt6::Sql
    PRIVATE personos_infrastructure personos_core
)

qt_add_executable(tst_review_state
    tests/tst_review_state.cpp
)
target_link_libraries(tst_review_state
    PRIVATE Qt6::Test Qt6::Core Qt6::Sql
    PRIVATE personos_infrastructure personos_core
)

qt_add_executable(tst_assessment
    tests/tst_assessment.cpp
)
target_link_libraries(tst_assessment
    PRIVATE Qt6::Test Qt6::Core Qt6::Sql
    PRIVATE personos_infrastructure personos_core
)

qt_add_executable(tst_knowledge
    tests/tst_knowledge.cpp
)
target_link_libraries(tst_knowledge
    PRIVATE Qt6::Test Qt6::Core Qt6::Sql
    PRIVATE personos_infrastructure personos_core
)

qt_add_executable(tst_knowledge_fts
    tests/tst_knowledge_fts.cpp
)
target_link_libraries(tst_knowledge_fts
    PRIVATE Qt6::Test Qt6::Core Qt6::Sql
    PRIVATE personos_infrastructure personos_core
)

qt_add_executable(tst_retrieval
    tests/tst_retrieval.cpp
)
target_link_libraries(tst_retrieval
    PRIVATE Qt6::Test Qt6::Core Qt6::Sql
    PRIVATE personos_infrastructure personos_core
)

qt_add_executable(tst_ai_gateway
    tests/tst_ai_gateway.cpp
)
target_link_libraries(tst_ai_gateway
    PRIVATE Qt6::Test Qt6::Core Qt6::Sql
    PRIVATE personos_infrastructure personos_core
)

qt_add_executable(tst_content_ingest
    tests/tst_content_ingest.cpp
)
target_link_libraries(tst_content_ingest
    PRIVATE Qt6::Test Qt6::Core Qt6::Sql
    PRIVATE personos_infrastructure personos_core
)

qt_add_executable(tst_operations
    tests/tst_operations.cpp
)
target_link_libraries(tst_operations
    PRIVATE Qt6::Test Qt6::Core Qt6::Sql
    PRIVATE personos_infrastructure personos_core
)

qt_add_executable(tst_retention
    tests/tst_retention.cpp
)
target_link_libraries(tst_retention
    PRIVATE Qt6::Test Qt6::Core Qt6::Sql
    PRIVATE personos_infrastructure personos_core
)

qt_add_executable(tst_content_map
    tests/tst_content_map.cpp
)
target_link_libraries(tst_content_map
    PRIVATE Qt6::Test Qt6::Core Qt6::Sql
    PRIVATE personos_infrastructure personos_core
)

qt_add_executable(tst_questionnaire_calibration
    tests/tst_questionnaire_calibration.cpp
)
target_link_libraries(tst_questionnaire_calibration
    PRIVATE Qt6::Test Qt6::Core Qt6::Sql
    PRIVATE personos_infrastructure personos_core
)

qt_add_executable(tst_audit
    tests/tst_audit.cpp
)

qt_add_executable(tst_detection_advice
    tests/tst_detection_advice.cpp
)

qt_add_executable(tst_domain_runtime
    tests/tst_domain_runtime.cpp
)

qt_add_executable(tst_ai_planning
    tests/tst_ai_planning.cpp
)

qt_add_executable(tst_provider
    tests/tst_provider.cpp
)

qt_add_executable(tst_pdf_extractor
    tests/tst_pdf_extractor.cpp
)

qt_add_executable(tst_corpus_scaffold
    tests/tst_corpus_scaffold.cpp
)
qt_add_executable(tst_paper_analysis
    tests/tst_paper_analysis.cpp
)
qt_add_executable(tst_model_connection
    tests/tst_model_connection.cpp
)

qt_add_executable(tst_markdown_corpus_index
    tests/tst_markdown_corpus_index.cpp
)

qt_add_executable(tst_legacy_migration
    tests/tst_legacy_migration.cpp
)

qt_add_executable(tst_e2e_three_mels
    tests/tst_e2e_three_mels.cpp
)
target_link_libraries(tst_e2e_three_mels
    PRIVATE Qt6::Test Qt6::Core Qt6::Sql
    PRIVATE personos_infrastructure personos_core
)

qt_add_executable(tst_security_audit
    tests/tst_security_audit.cpp
)

qt_add_executable(tst_benchmark
    tests/tst_benchmark.cpp
)

qt_add_executable(tst_restore
    tests/tst_restore.cpp
)

qt_add_executable(tst_embedding
    tests/tst_embedding.cpp
)

qt_add_executable(tst_conversation_state
    tests/tst_conversation_state.cpp
)
target_link_libraries(tst_conversation_state
    PRIVATE Qt6::Test Qt6::Core Qt6::Sql
    PRIVATE personos_infrastructure personos_core
)
target_link_libraries(tst_embedding
    PRIVATE Qt6::Test Qt6::Core Qt6::Sql
    PRIVATE personos_infrastructure personos_core
)
target_link_libraries(tst_restore
    PRIVATE Qt6::Test Qt6::Core Qt6::Sql
    PRIVATE personos_infrastructure personos_core
)
target_link_libraries(tst_benchmark
    PRIVATE Qt6::Test Qt6::Core Qt6::Sql
    PRIVATE personos_infrastructure personos_core
)
target_link_libraries(tst_security_audit
    PRIVATE Qt6::Test Qt6::Core Qt6::Sql
    PRIVATE personos_infrastructure personos_core
)
target_link_libraries(tst_legacy_migration
    PRIVATE Qt6::Test Qt6::Core Qt6::Sql
    PRIVATE personos_infrastructure personos_core
)
target_link_libraries(tst_corpus_scaffold
    PRIVATE Qt6::Test Qt6::Core Qt6::Sql
    PRIVATE personos_infrastructure personos_core
)
target_link_libraries(tst_markdown_corpus_index
    PRIVATE Qt6::Test Qt6::Core
    PRIVATE personos_infrastructure
)
target_link_libraries(tst_paper_analysis
    PRIVATE Qt6::Test Qt6::Core Qt6::Sql
    PRIVATE personos_infrastructure personos_core
)
target_link_libraries(tst_model_connection
    PRIVATE Qt6::Test Qt6::Core Qt6::Sql
    PRIVATE personos_infrastructure personos_core
)

qt_add_executable(corpus_scaffold
    tools/corpus_scaffold/main.cpp
)
target_link_libraries(corpus_scaffold
    PRIVATE Qt6::Core Qt6::Sql Qt6::Network
    PRIVATE personos_core personos_infrastructure
)
target_link_libraries(tst_pdf_extractor
    PRIVATE Qt6::Test Qt6::Core Qt6::Sql
    PRIVATE personos_infrastructure personos_core
)
target_link_libraries(tst_provider
    PRIVATE Qt6::Test Qt6::Core Qt6::Sql Qt6::Network
    PRIVATE personos_infrastructure personos_core
)
target_link_libraries(tst_ai_planning
    PRIVATE Qt6::Test Qt6::Core Qt6::Sql Qt6::Network
    PRIVATE personos_infrastructure personos_core
)
target_link_libraries(tst_domain_runtime
    PRIVATE Qt6::Test Qt6::Core Qt6::Sql
    PRIVATE personos_infrastructure personos_core
)
target_link_libraries(tst_detection_advice
    PRIVATE Qt6::Test Qt6::Core Qt6::Sql
    PRIVATE personos_infrastructure personos_core
)
target_link_libraries(tst_audit
    PRIVATE Qt6::Test Qt6::Core Qt6::Sql
    PRIVATE personos_infrastructure personos_core
)

qt_add_executable(tst_qml_smoke
    tests/tst_qml_smoke.cpp
)
# 与 app 相同的 QML 模块注册（静态库不生成 moc，注册由本目标承担）；
# 输出目录单独指定，避免与 appPersonOS 模块产物冲突
qt_add_qml_module(tst_qml_smoke
    URI PersonOS
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tst_qml_smoke_module
    QML_FILES
        ${PERSONOS_QML_FILES}
    SOURCES
        ${PERSONOS_QML_REGISTERED_HEADERS}
)
target_link_libraries(tst_qml_smoke
    PRIVATE Qt6::Test Qt6::Quick Qt6::Sql
    PRIVATE personos_infrastructure personos_presentation personos_core
)
target_include_directories(tst_qml_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# QML_ELEMENT 生成的注册代码以头文件基名 include（与 app 一致）
target_include_directories(tst_qml_smoke PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/presentation/viewmodels)

target_link_libraries(tst_mvp_loop
    PRIVATE Qt6::Test
    PRIVATE Qt6::Core
    PRIVATE personos_core
)

# ================= ctest 一键回归 =================
enable_testing()

add_test(NAME smoke_goals     COMMAND smoke_goals)
add_test(NAME smoke_state     COMMAND smoke_state)
add_test(NAME smoke_planning  COMMAND smoke_planning)
add_test(NAME smoke_execution COMMAND smoke_execution)
add_test(NAME smoke_feedback  COMMAND smoke_feedback)
add_test(NAME smoke_review    COMMAND smoke_review)
add_test(NAME smoke_evolution COMMAND smoke_evolution)
add_test(NAME smoke_metrics   COMMAND smoke_metrics)
add_test(NAME smoke_feasibility COMMAND smoke_feasibility)
add_test(NAME smoke_guardrail   COMMAND smoke_guardrail)
add_test(NAME tst_mvp_loop    COMMAND tst_mvp_loop)
add_test(NAME tst_foundation  COMMAND tst_foundation)
add_test(NAME tst_database_migrations COMMAND tst_database_migrations)
add_test(NAME tst_goal_route   COMMAND tst_goal_route)
add_test(NAME tst_mel          COMMAND tst_mel)
add_test(NAME tst_review_state COMMAND tst_review_state)
add_test(NAME tst_assessment    COMMAND tst_assessment)
add_test(NAME tst_knowledge     COMMAND tst_knowledge)
add_test(NAME tst_knowledge_fts COMMAND tst_knowledge_fts)
add_test(NAME tst_retrieval     COMMAND tst_retrieval)
add_test(NAME tst_ai_gateway    COMMAND tst_ai_gateway)
add_test(NAME tst_content_ingest COMMAND tst_content_ingest)
add_test(NAME tst_operations     COMMAND tst_operations)
add_test(NAME tst_retention      COMMAND tst_retention)
add_test(NAME tst_content_map    COMMAND tst_content_map)
add_test(NAME tst_questionnaire_calibration COMMAND tst_questionnaire_calibration)
add_test(NAME tst_audit         COMMAND tst_audit)
add_test(NAME tst_detection_advice COMMAND tst_detection_advice)
add_test(NAME tst_domain_runtime  COMMAND tst_domain_runtime)
add_test(NAME tst_ai_planning    COMMAND tst_ai_planning)
add_test(NAME tst_provider       COMMAND tst_provider)
add_test(NAME tst_pdf_extractor  COMMAND tst_pdf_extractor)
add_test(NAME tst_corpus_scaffold COMMAND tst_corpus_scaffold)
add_test(NAME tst_markdown_corpus_index COMMAND tst_markdown_corpus_index)
add_test(NAME tst_paper_analysis COMMAND tst_paper_analysis)
add_test(NAME tst_model_connection COMMAND tst_model_connection)
add_test(NAME tst_legacy_migration COMMAND tst_legacy_migration)
add_test(NAME tst_e2e_three_mels   COMMAND tst_e2e_three_mels)
add_test(NAME tst_security_audit   COMMAND tst_security_audit)
add_test(NAME tst_benchmark        COMMAND tst_benchmark)
add_test(NAME tst_restore         COMMAND tst_restore)
add_test(NAME tst_embedding      COMMAND tst_embedding)
add_test(NAME tst_conversation_state COMMAND tst_conversation_state)
add_test(NAME tst_qml_smoke      COMMAND tst_qml_smoke)
set_tests_properties(tst_qml_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
# 注：smoke_ai 需要网络与 API key，未加入 ctest；手动运行 build 目录下的 smoke_ai.exe
