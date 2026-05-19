# Sources TnC (mestoph) + shim SDL3 — code compile dans t4c_client (≠ T4C_DATA runtime).
set(_TNC_ROOT_CANDIDATES
    "${CMAKE_SOURCE_DIR}/../client_graphical_path_to_follow/decode/TnC_dev"
    "${CMAKE_SOURCE_DIR}/../client_graphical_sdl3_test/TnC_dev"
)
if(NOT TNC_GRAPHICAL_ROOT OR NOT EXISTS "${TNC_GRAPHICAL_ROOT}/VSFInterface/vsfinterface.cpp")
    set(TNC_GRAPHICAL_ROOT "")
    foreach(_cand IN LISTS _TNC_ROOT_CANDIDATES)
        if(EXISTS "${_cand}/VSFInterface/vsfinterface.cpp")
            set(TNC_GRAPHICAL_ROOT "${_cand}")
            break()
        endif()
    endforeach()
endif()
set(TNC_GRAPHICAL_ROOT "${TNC_GRAPHICAL_ROOT}" CACHE PATH
    "Racine TnC_dev (sources MapInterface/VSFInterface — pas les assets .dec)")

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
    ${CMAKE_SOURCE_DIR}/third_party/tnc_sdl3/include
    ${CMAKE_SOURCE_DIR}/third_party/tnc_sdl3/render
    ${TNC_GRAPHICAL_ROOT}
)

set(TNC_GRAPHICAL_CLIENT_SOURCES
    ${CMAKE_SOURCE_DIR}/src/game/GameWorldScreen.cpp
    ${CMAKE_SOURCE_DIR}/src/game/TncDataPaths.cpp
    ${CMAKE_SOURCE_DIR}/third_party/tnc_sdl3/render/Sdl3FramePresenter.cpp
)
