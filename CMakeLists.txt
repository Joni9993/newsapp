cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Altes 2"-Panel: Version bekommt "-legacy" (die Firmware erkennt daran beim
# Upload, ob die Datei zur Anzeige passt). CI setzt PROJECT_VER selbst.
if(NEWSBOARD_LEGACY_PANEL)
    if(NOT PROJECT_VER)
        execute_process(COMMAND git describe --always --tags --dirty
                        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
                        OUTPUT_VARIABLE NB_GIT_VER RESULT_VARIABLE NB_GIT_RC
                        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(NOT NB_GIT_RC EQUAL 0 OR NB_GIT_VER STREQUAL "")
            set(NB_GIT_VER "dev")
        endif()
        set(PROJECT_VER "${NB_GIT_VER}-legacy")
    elseif(NOT PROJECT_VER MATCHES "-legacy$")
        set(PROJECT_VER "${PROJECT_VER}-legacy")
    endif()
endif()
project(nachrichtenbote)
