cmake_minimum_required(VERSION 3.20)

project(EasyBackupandDelay VERSION 1.14.6)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Dependencias ────────────────────────────────────────────────────────────────

# OBS (definir OBS_DIR o -DCMAKE_PREFIX_PATH=<obs-install>)
find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)

# Qt6 (viene con OBS Studio ≥28)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Svg)

# FFmpeg (libs de import del paquete deps; las DLLs vienen con el propio OBS)
find_path(FFMPEG_INCLUDE_DIR NAMES libavcodec/avcodec.h REQUIRED)
find_library(AVCODEC_LIB avcodec REQUIRED)
find_library(AVUTIL_LIB  avutil  REQUIRED)
find_library(SWSCALE_LIB swscale REQUIRED)

# nlohmann/json (solo headers, se descarga automáticamente)
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# ── Fuentes ──────────────────────────────────────────────────────────────────────

set(SOURCES
    src/plugin-main.cpp
    src/backup/scene-parser.cpp
    src/backup/backup-manifest.cpp
    src/backup/restore-helpers.cpp
    src/backup/backup-manager.cpp
    src/backup/settings-tab.cpp
    src/backup/delay-tab.cpp
    src/backup/backup-tab.cpp
    src/backup/restore-tab.cpp
    src/backup/backup-dialog.cpp
    src/delay/delay-filters.cpp
    src/delay/delay-dock.cpp
    src/delay/delay-visibility.cpp
    src/delay/mic-mute.cpp
    src/delay/delay-playing.cpp
    src/delay/delay-codec.cpp
    src/delay/ffmpeg-delayload.cpp
    src/web/web-dock.cpp
)

set(HEADERS
    src/i18n.hpp
    src/backup/scene-parser.hpp
    src/backup/backup-manifest.hpp
    src/backup/restore-helpers.hpp
    src/backup/backup-manager.hpp
    src/backup/settings-tab.hpp
    src/backup/delay-tab.hpp
    src/backup/backup-tab.hpp
    src/backup/restore-tab.hpp
    src/backup/backup-dialog.hpp
    src/delay/delay-codec.hpp
    src/delay/delay-dock.hpp
    src/delay/delay-visibility.hpp
    src/delay/mic-mute.hpp
    src/delay/delay-playing.hpp
    src/web/web-dock.hpp
)

# Recurso de versión de Windows para que Explorador → Propiedades → Detalles muestre la versión.
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/version.rc.in
               ${CMAKE_CURRENT_BINARY_DIR}/version.rc @ONLY)

add_library(EasyBackupandDelay MODULE ${SOURCES} ${HEADERS}
            ${CMAKE_CURRENT_BINARY_DIR}/version.rc)

# Única fuente de verdad para la versión que se muestra dentro de la UI del plugin.
target_compile_definitions(EasyBackupandDelay PRIVATE EASYOBS_VERSION="${PROJECT_VERSION}")

set_target_properties(EasyBackupandDelay PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
)

target_include_directories(EasyBackupandDelay PRIVATE
    src src/backup src/delay src/web ${FFMPEG_INCLUDE_DIR})

target_link_libraries(EasyBackupandDelay PRIVATE
    OBS::libobs
    OBS::obs-frontend-api
    Qt6::Core
    Qt6::Widgets
    Qt6::Svg
    winmm            # waveOut*: el aviso del audio demorado, fuera del mezclador de OBS
    nlohmann_json::nlohmann_json
    ${AVCODEC_LIB}
    ${AVUTIL_LIB}
    ${SWSCALE_LIB}
    shell32
)

# ── FFmpeg propio: delay-load + bundling (MSVC) ────────────────────────────────
# Las DLLs de avcodec/avutil/swscale traen su versión pegada al nombre (avcodec-NN)
# y OBS ata esa versión a la suya. Si el plugin se compiló contra otra versión, sin
# delay-load el módulo NO carga ("LoadLibrary error 126") y el delay pasa el directo
# en silencio.
#
# Solución: (1) DELAY-LOAD de esas DLLs → el módulo carga igual y la DLL se resuelve
# recién en la primera llamada; (2) empaquetamos NUESTRAS DLLs (las que se usaron al
# compilar) en la subcarpeta EasyBackupandDelay-ffmpeg\ y un notify-hook
# (ffmpeg-delayload.cpp) las carga por ruta absoluta, con fallback a las de OBS. Así
# el delay anda con cualquier versión de OBS. Si algo falla igual,
# easyobs_ffmpeg_available() lo detecta para avisar en pantalla.
#
# El nombre de la DLL (con su version) se deriva de cada IMPORT LIB — que es lo que
# el plugin realmente va a importar — y NO del bin/ (que puede estar desincronizado).
# Así /DELAYLOAD y el bundling siempre coinciden con el build. La DLL a empaquetar se
# copia desde bin/ si esta presente; si no, se avisa y se sigue con delay-load (que
# igual habilita el fallback a las DLLs de OBS + el aviso en pantalla).
function(ff_imported_dll_name lib prefix outvar)
    set(_name "")
    if(EXISTS "${lib}")
        file(STRINGS "${lib}" _hits REGEX "${prefix}-[0-9]+\\.dll" LIMIT_COUNT 1)
        foreach(_h ${_hits})
            if(_h MATCHES "(${prefix}-[0-9]+\\.dll)")
                set(_name "${CMAKE_MATCH_1}")
            endif()
        endforeach()
    endif()
    set(${outvar} "${_name}" PARENT_SCOPE)
endfunction()

# Cierre transitivo de DLLs: avcodec depende de avutil/swresample/libx264/zlib…, así
# que empaquetar solo las 3 no alcanza — hay que llevar TODA la cadena. La calculamos
# con dumpbin partiendo de las 3 y siguiendo dependencias que existan en bin/.
get_filename_component(_cl_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
find_program(DUMPBIN_EXE dumpbin HINTS "${_cl_dir}")

function(ff_closure startdlls bindir outvar)
    set(_seen "")
    set(_stack ${startdlls})
    while(_stack)
        list(POP_FRONT _stack _cur)
        if(NOT (_cur IN_LIST _seen) AND EXISTS "${bindir}/${_cur}")
            list(APPEND _seen "${_cur}")
            if(DUMPBIN_EXE)
                execute_process(COMMAND "${DUMPBIN_EXE}" /DEPENDENTS "${bindir}/${_cur}"
                                OUTPUT_VARIABLE _o ERROR_QUIET)
                string(REGEX MATCHALL "[A-Za-z0-9_.-]+\\.dll" _deps "${_o}")
                foreach(_d ${_deps})
                    if(EXISTS "${bindir}/${_d}")
                        list(APPEND _stack "${_d}")
                    endif()
                endforeach()
            endif()
        endif()
    endwhile()
    set(${outvar} ${_seen} PARENT_SCOPE)
endfunction()

if(MSVC)
    get_filename_component(_ff_libdir "${AVCODEC_LIB}" DIRECTORY)
    set(_ff_bindir "${_ff_libdir}/../bin")
    set(_bundle_dir "$<TARGET_FILE_DIR:EasyBackupandDelay>/EasyBackupandDelay-ffmpeg")

    # Nombres reales que el plugin importará (de los import libs) → /DELAYLOAD.
    set(_ff_primaries "")
    foreach(_pair "${AVCODEC_LIB}=avcodec" "${AVUTIL_LIB}=avutil" "${SWSCALE_LIB}=swscale")
        string(REPLACE "=" ";" _kv "${_pair}")
        list(GET _kv 0 _lib)
        list(GET _kv 1 _prefix)
        ff_imported_dll_name("${_lib}" "${_prefix}" _dllname)
        if(_dllname)
            list(APPEND _ff_primaries "${_dllname}")
            target_link_options(EasyBackupandDelay PRIVATE "/DELAYLOAD:${_dllname}")
        endif()
    endforeach()

    if(_ff_primaries)
        target_link_libraries(EasyBackupandDelay PRIVATE delayimp)

        # Empaquetar el cierre transitivo completo al lado del plugin.
        ff_closure("${_ff_primaries}" "${_ff_bindir}" _ff_bundle)
        add_custom_command(TARGET EasyBackupandDelay POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_bundle_dir}")
        set(_ff_missing "")
        foreach(_p ${_ff_primaries})
            if(NOT (_p IN_LIST _ff_bundle))
                list(APPEND _ff_missing "${_p}")
            endif()
        endforeach()
        if(_ff_missing)
            message(WARNING "FFmpeg bundle: ${_ff_missing} no esta en ${_ff_bindir} — el "
                            "delay usara la FFmpeg de OBS (fallback). Sincroniza bin/ con "
                            "lib/ en las deps para empaquetar.")
        endif()
        foreach(_d ${_ff_bundle})
            add_custom_command(TARGET EasyBackupandDelay POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_ff_bindir}/${_d}" "${_bundle_dir}/")
        endforeach()
        message(STATUS "FFmpeg bundle (cierre): ${_ff_bundle}")
    endif()
endif()

# ── Test de ida y vuelta del códec (se compila a demanda: --target codec-test) ──

add_executable(codec-test EXCLUDE_FROM_ALL
    tools/codec-test.cpp
    src/delay/delay-codec.cpp
)
target_include_directories(codec-test PRIVATE src src/delay ${FFMPEG_INCLUDE_DIR})
target_link_libraries(codec-test PRIVATE ${AVCODEC_LIB} ${AVUTIL_LIB} ${SWSCALE_LIB})

# ── Instalación ──────────────────────────────────────────────────────────────────

install(TARGETS EasyBackupandDelay
    LIBRARY DESTINATION "obs-plugins/64bit"
)
