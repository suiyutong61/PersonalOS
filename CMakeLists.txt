cmake_minimum_required(VERSION 3.16)

project(PersonOS VERSION 0.1 LANGUAGES CXX)

# C++20（README 2.7.4 技术栈要求）
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Sql)

qt_standard_project_setup(REQUIRES 6.10)

# ================= 核心库（领域模型 + 仓库 + 子系统服务）=================
qt_add_library(personos_core STATIC
    src/database/DatabaseManager.cpp
    src/database/DatabaseManager.h
    src/database/RepoUtil.h
    src/database/CoreValueRepository.cpp
    src/database/CoreValueRepository.h
    src/database/PrincipleRepository.cpp
    src/database/PrincipleRepository.h
    src/database/GoalRepository.cpp
    src/database/GoalRepository.h
    src/goals/CoreAndGoalService.cpp
    src/goals/CoreAndGoalService.h
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
target_link_libraries(personos_core PUBLIC Qt6::Core Qt6::Sql)

# ================= 应用（GUI）=================
qt_add_executable(appPersonOS
    src/application/main.cpp
)

qt_add_qml_module(appPersonOS
    URI PersonOS
    VERSION 1.0
    QML_FILES
        qml/Main.qml
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
# 逐步添加：Step 5 goals_smoke；Step 13 起改为 Qt Test 完整测试
qt_add_executable(smoke_goals
    tests/goals_smoke.cpp
)

target_link_libraries(smoke_goals
    PRIVATE Qt6::Core
    PRIVATE personos_core
)
