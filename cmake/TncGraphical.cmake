# Sources TnC (mestoph) SDL3 natif — code compile dans t4c_client (≠ T4C_DATA runtime).
#
# Ordre de priorite a chaque configure :
#   1. client_graphical_sdl3_test/TnC_dev                — TnC patche SDL3 (compile)
#   2. client_graphical_path_to_follow/decode/TnC_dev    — labo mestoph (fallback)
set(_TNC_ROOT_CANDIDATES
    "${CMAKE_SOURCE_DIR}/../client_graphical_sdl3_test/TnC_dev"
    "${CMAKE_SOURCE_DIR}/../client_graphical_path_to_follow/decode/TnC_dev"
)

set(_TNC_RESOLVED "")
foreach(_cand IN LISTS _TNC_ROOT_CANDIDATES)
    if(EXISTS "${_cand}/VSFInterface/vsfinterface.cpp")
        set(_TNC_RESOLVED "${_cand}")
        break()
    endif()
endforeach()

if(_TNC_RESOLVED)
    set(TNC_GRAPHICAL_ROOT "${_TNC_RESOLVED}" CACHE PATH
        "Racine TnC_dev (sources MapInterface/VSFInterface — pas les assets .dec)" FORCE)
endif()

if(NOT TNC_GRAPHICAL_ROOT OR NOT EXISTS "${TNC_GRAPHICAL_ROOT}/VSFInterface/vsfinterface.cpp")
    message(WARNING "TNC_GRAPHICAL_ROOT invalide: ${TNC_GRAPHICAL_ROOT} — vue monde desactivee")
    set(T4C_HAS_WORLD_VIEW FALSE)
    return()
endif()

set(T4C_HAS_WORLD_VIEW TRUE)
message(STATUS "Vue monde TnC: ${TNC_GRAPHICAL_ROOT}")

set(TNC_GRAPHICAL_SOURCES
    ${TNC_GRAPHICAL_ROOT}/FontManager/fontmanager.cpp
    ${TNC_GRAPHICAL_ROOT}/MapInterface/mapinterface.cpp
    ${TNC_GRAPHICAL_ROOT}/MapInterface/mapi_full_redraw.cpp
    ${TNC_GRAPHICAL_ROOT}/MapInterface/mapi_get_map.cpp
    ${TNC_GRAPHICAL_ROOT}/MapInterface/mapi_move_map.cpp
    ${TNC_GRAPHICAL_ROOT}/TextManager/textmanager.cpp
    ${TNC_GRAPHICAL_ROOT}/NPCManager/npc_ajout.cpp
    ${TNC_GRAPHICAL_ROOT}/NPCManager/npc_config.cpp
    ${TNC_GRAPHICAL_ROOT}/NPCManager/npc_draw.cpp
    ${TNC_GRAPHICAL_ROOT}/NPCManager/npcmanager.cpp
    ${TNC_GRAPHICAL_ROOT}/VSFInterface/vsfi_create_hash.cpp
    ${TNC_GRAPHICAL_ROOT}/VSFInterface/vsfi_hash.cpp
    ${TNC_GRAPHICAL_ROOT}/VSFInterface/vsfi_indexage_pal.cpp
    ${TNC_GRAPHICAL_ROOT}/VSFInterface/vsfi_indexage_rep.cpp
    ${TNC_GRAPHICAL_ROOT}/VSFInterface/vsfi_indexage_spr.cpp
    ${TNC_GRAPHICAL_ROOT}/VSFInterface/vsfi_palettes.cpp
    ${TNC_GRAPHICAL_ROOT}/VSFInterface/vsfi_read_sprite.cpp
    ${TNC_GRAPHICAL_ROOT}/VSFInterface/vsfi_save_bmp.cpp
    ${TNC_GRAPHICAL_ROOT}/VSFInterface/vsfi_sprites.cpp
    ${TNC_GRAPHICAL_ROOT}/VSFInterface/vsfinterface.cpp
)

set(TNC_GRAPHICAL_INCLUDES
    ${TNC_GRAPHICAL_ROOT}/include
    ${TNC_GRAPHICAL_ROOT}/render
    ${TNC_GRAPHICAL_ROOT}
)

set(TNC_GRAPHICAL_CLIENT_SOURCES
    ${CMAKE_SOURCE_DIR}/src/game/GameWorldScreen.cpp
    ${CMAKE_SOURCE_DIR}/src/game/TncDataPaths.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/WorldSideMenu.cpp
    ${TNC_GRAPHICAL_ROOT}/render/Sdl3FramePresenter.cpp
)
