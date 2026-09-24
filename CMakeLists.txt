cmake_minimum_required(VERSION 3.24)
project(EChat VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Widgets Network Bluetooth Sql)

find_package(unofficial-sodium CONFIG QUIET)
if (TARGET unofficial-sodium::sodium)
    set(ECHAT_SODIUM_TARGET unofficial-sodium::sodium)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)
    set(ECHAT_SODIUM_TARGET PkgConfig::SODIUM)
endif()

set(ECHAT_CORE_SOURCES
    src/core/Types.h
    src/core/Protocol.h
    src/core/Protocol.cpp
    src/core/PeerManager.h
    src/core/PeerManager.cpp
    src/core/ConversationManager.h
    src/core/ConversationManager.cpp
    src/crypto/CryptoEngine.h
    src/crypto/CryptoEngine.cpp
    src/storage/Database.h
    src/storage/Database.cpp
    src/transport/Transport.h
    src/transport/TransportManager.h
    src/transport/TransportManager.cpp
    src/transport/bluetooth/BluetoothTransport.h
    src/transport/bluetooth/BluetoothTransport.cpp
    src/transport/lan/LanTransport.h
    src/transport/lan/LanTransport.cpp
    src/transport/internet/RelayTransport.h
    src/transport/internet/RelayTransport.cpp
)

add_library(echat_core STATIC ${ECHAT_CORE_SOURCES})
target_include_directories(echat_core PUBLIC src)
target_link_libraries(echat_core PUBLIC
    Qt6::Core Qt6::Network Qt6::Bluetooth Qt6::Sql ${ECHAT_SODIUM_TARGET}
)

qt_add_executable(EChat
    src/main.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
)
target_link_libraries(EChat PRIVATE echat_core Qt6::Widgets)
set_target_properties(EChat PROPERTIES WIN32_EXECUTABLE TRUE)

qt_add_executable(EChatCLI
    src/cli/main_cli.cpp
    src/cli/CliController.h
    src/cli/CliController.cpp
)
target_link_libraries(EChatCLI PRIVATE echat_core)
set_target_properties(EChatCLI PROPERTIES OUTPUT_NAME "echat-cli")

if (MSVC)
    target_compile_options(echat_core PRIVATE /W4)
else()
    target_compile_options(echat_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if (WIN32 AND MINGW)
    message(FATAL_ERROR "EChat Bluetooth on Windows requires a Qt MSVC build; MinGW is not supported by the Qt Bluetooth Windows backend used here.")
endif()

option(ECHAT_BUILD_TESTS "Build EChat self-tests" OFF)
if (ECHAT_BUILD_TESTS)
    qt_add_executable(EChatCryptoSelfTest tests/CryptoSelfTest.cpp)
    target_link_libraries(EChatCryptoSelfTest PRIVATE echat_core)
endif()
