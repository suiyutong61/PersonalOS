cmake_minimum_required(VERSION 3.16)

project(PersonOS VERSION 0.1 LANGUAGES CXX)

# C++20（README 2.7.4 技术栈要求）
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Sql Network Test)

qt_standard_project_setup(REQUIRES 6.10)

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
)

# 头文件统一以 src 为根：如 #include "database/DatabaseManager.h"
target_include_directories(personos_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(personos_core PUBLIC Qt6::Core Qt6::Sql Qt6::Network)

# ================= 应用（GUI）=================
qt_add_executable(appPersonOS
    src/application/main.cpp
)

qt_add_qml_module(appPersonOS
    URI PersonOS
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/pages/TodayPage.qml
        qml/pages/GoalsPage.qml
        qml/pages/StatePage.qml
        qml/pages/ReviewPage.qml
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
)

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

# ================= MVP 闭环验收测试（Qt Test，README 3.14）=================
qt_add_executable(tst_mvp_loop
    tests/tst_mvp_loop.cpp
    tests/TestLogging.h
)

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
add_test(NAME tst_mvp_loop    COMMAND tst_mvp_loop)
# 注：smoke_ai 需要网络与 API key，未加入 ctest；手动运行 build 目录下的 smoke_ai.exe
