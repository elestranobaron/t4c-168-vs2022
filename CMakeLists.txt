cmake_minimum_required(VERSION 3.16)
project(T4CClientLinux)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Force le build en Debug pour l'investigation dans Cursor / GDB
set(CMAKE_BUILD_TYPE Debug)

# --- RECHERCHE DES DEPENDANCES (Ton excellente base SDL3) ---
find_package(PkgConfig REQUIRED)
find_package(SDL3 REQUIRED CONFIG)

# SDL3_image
find_package(SDL3_image QUIET CONFIG)
if(NOT SDL3_image_FOUND)
    message(STATUS "SDL3_image not found, fetching from source...")
    include(FetchContent)
    FetchContent_Declare(
        SDL3_image
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_image.git
        GIT_TAG release-3.2.4
    )
    FetchContent_MakeAvailable(SDL3_image)
    set(SDL3_IMAGE_INCLUDE_DIR ${sdl3_image_SOURCE_DIR}/include)
    set(SDL3_IMAGE_LIBRARY ${sdl3_image_BINARY_DIR}/libSDL3_image.so)
    set(SDL3_image_FOUND TRUE)
endif()

# SDL3_ttf
find_package(SDL3_ttf QUIET CONFIG)
if(NOT SDL3_ttf_FOUND)
    message(STATUS "SDL3_ttf not found, fetching from source...")
    include(FetchContent)
    FetchContent_Declare(
        SDL3_ttf
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
        GIT_TAG release-3.2.3
    )
    FetchContent_MakeAvailable(SDL3_ttf)
    set(SDL3_TTF_INCLUDE_DIR ${sdl3_ttf_SOURCE_DIR}/include)
    set(SDL3_TTF_LIBRARY ${sdl3_ttf_BINARY_DIR}/libSDL3_ttf.so)
    set(SDL3_ttf_FOUND TRUE)
endif()

find_package(Threads REQUIRED)

set(T4C_CLIENT_DIR "${CMAKE_SOURCE_DIR}/CLIENT168_RC14h_OK/1.68RC14h/Client/T4C Client")

# --- INCLUSIONS DES DOSSIERS ---
include_directories(${SDL3_INCLUDE_DIRS})
include_directories(${SDL3_IMAGE_INCLUDE_DIR})
include_directories(${SDL3_TTF_INCLUDE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories("${T4C_CLIENT_DIR}")
include_directories("${T4C_CLIENT_DIR}/UDP")

# --- DEFINITIONS DE PRECOMPILATION CRUCIALES ---
# On indique au code qu'on est sous Linux pour désactiver les headers Windows indisponibles
add_compile_definitions(LINUX_PORT)

# --- Vue monde TnC (SDL3) ---
include(${CMAKE_SOURCE_DIR}/cmake/TncGraphical.cmake)

# --- LES SOURCES ---
set(SOURCES
    src/main.cpp
    src/gui/LoginScreen.cpp
    src/gui/IntroductionScreen.cpp
    src/gui/CharacterSelectScreen.cpp
    src/gui/CharacterCreateScreen.cpp
    src/gui/T4CCharacterQuestionnaire.cpp
    src/gui/LoginClientConfig.cpp
    src/gui/T4CUiFont.cpp
    src/gui/T4CScrollingBanner.cpp
    src/gui/LauncherChrome.cpp
    src/network/T4CNetworkDebugLog.cpp
    src/network/T4CLoginSession.cpp
    src/network/IOCPCompat.cpp
    src/network/T4CLinuxCommPort.cpp
    src/network/CryptStubLinux.cpp
    "${CMAKE_SOURCE_DIR}/CLIENT168_RC14h_OK/1.68RC14h/CryptMestoph/crypt.cpp"
    src/network/CommCenter.cpp
    "${T4C_CLIENT_DIR}/Random.cpp"
    "${T4C_CLIENT_DIR}/UDP/Mutex.cpp"
    "${T4C_CLIENT_DIR}/UDP/Semaphore.cpp"
)

if(T4C_HAS_WORLD_VIEW)
    list(APPEND SOURCES ${TNC_GRAPHICAL_SOURCES} ${TNC_GRAPHICAL_CLIENT_SOURCES})
endif()

# C'est ici que tu ajouteras TFCPacket.cpp, Comm.cpp, etc. au fur et à mesure

# --- EXECUTABLE ---
add_executable(t4c_client ${SOURCES})

target_include_directories(t4c_client PRIVATE "${CMAKE_SOURCE_DIR}/CLIENT168_RC14h_OK/1.68RC14h/CryptMestoph")

if(T4C_HAS_WORLD_VIEW)
    target_include_directories(t4c_client BEFORE PRIVATE ${TNC_GRAPHICAL_INCLUDES})
    target_compile_definitions(t4c_client PRIVATE T4C_HAS_WORLD_VIEW=1)
endif()

# --- LINKAGE DES LIBRARIES ---
target_link_libraries(t4c_client PRIVATE SDL3::SDL3)

if(SDL3_image_FOUND AND TARGET SDL3_image::SDL3_image)
    target_link_libraries(t4c_client PRIVATE SDL3_image::SDL3_image)
else()
    target_link_libraries(t4c_client PRIVATE ${SDL3_IMAGE_LIBRARY})
endif()

if(SDL3_ttf_FOUND AND TARGET SDL3_ttf::SDL3_ttf)
    target_link_libraries(t4c_client PRIVATE SDL3_ttf::SDL3_ttf)
else()
    target_link_libraries(t4c_client PRIVATE ${SDL3_TTF_LIBRARY})
endif()

target_link_libraries(t4c_client PRIVATE Threads::Threads m)

find_package(ZLIB REQUIRED)
if(T4C_HAS_WORLD_VIEW)
    target_link_libraries(t4c_client PRIVATE ZLIB::ZLIB)
    set(T4C_DATA_ROOT "${CMAKE_SOURCE_DIR}/data")
    if(EXISTS "${T4C_DATA_ROOT}/sprites" AND EXISTS "${T4C_DATA_ROOT}/maps")
        add_custom_command(TARGET t4c_client POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:t4c_client>/data"
            COMMAND ${CMAKE_COMMAND} -E create_symlink
                "${T4C_DATA_ROOT}"
                "$<TARGET_FILE_DIR:t4c_client>/data"
        )
    else()
        message(WARNING "client/data/ absent (sprites+maps) — lancez scripts/assemble_t4c_data.sh")
    endif()
endif()
