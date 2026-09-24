cmake_minimum_required(VERSION 3.24)
project(EChat VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Widgets Network Bluetooth Sql)

set(MSQUIC_INCLUDE_DIR "" CACHE PATH "Directory containing msquic.h")
set(MSQUIC_LIBRARY "" CACHE FILEPATH "Path to the MsQuic library")

find_package(msquic CONFIG QUIET)
if (TARGET msquic)
    set(ECHAT_MSQUIC_TARGET msquic)
    set(ECHAT_HAVE_MSQUIC 1)
else()
    if (NOT MSQUIC_INCLUDE_DIR)
        find_path(MSQUIC_INCLUDE_DIR NAMES msquic.h)
    endif()
    if (NOT MSQUIC_LIBRARY)
        find_library(MSQUIC_LIBRARY NAMES msquic libmsquic)
    endif()
    if (MSQUIC_INCLUDE_DIR AND MSQUIC_LIBRARY)
        add_library(echat_msquic UNKNOWN IMPORTED)
        set_target_properties(echat_msquic PROPERTIES
            IMPORTED_LOCATION "${MSQUIC_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${MSQUIC_INCLUDE_DIR}")
        set(ECHAT_MSQUIC_TARGET echat_msquic)
        set(ECHAT_HAVE_MSQUIC 1)
    else()
        set(ECHAT_HAVE_MSQUIC 0)
        message(WARNING "MsQuic not found: EChat will build with LAN TCP fallback only")
    endif()
endif()

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
    src/transport/lan/QuicEndpoint.h
    src/transport/lan/QuicEndpoint.cpp
    src/transport/internet/RelayTransport.h
    src/transport/internet/RelayTransport.cpp
)

add_library(echat_core STATIC ${ECHAT_CORE_SOURCES})
target_include_directories(echat_core PUBLIC src)
target_link_libraries(echat_core PUBLIC
    Qt6::Core Qt6::Network Qt6::Bluetooth Qt6::Sql ${ECHAT_SODIUM_TARGET}
)
target_compile_definitions(echat_core PUBLIC ECHAT_HAVE_MSQUIC=${ECHAT_HAVE_MSQUIC})
if (ECHAT_HAVE_MSQUIC)
    target_link_libraries(echat_core PUBLIC ${ECHAT_MSQUIC_TARGET})
endif()

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
