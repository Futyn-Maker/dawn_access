cmake_minimum_required(VERSION 3.22)

# Dawn Access (DawnAccess) — a UE4SS C++ mod that voices Until Dawn's interface and gameplay
# through Prism (NVDA / JAWS / Narrator / SAPI).
project(DawnAccessMod LANGUAGES C CXX)

# The mod's version is the first line of the VERSION file and is written nowhere else: the
# mod reports it at start and the release folder carries it in its name.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" DA_VERSION LIMIT_COUNT 1)
string(STRIP "${DA_VERSION}" DA_VERSION)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")

# The RE-UE4SS source tree. The mod and UE4SS.dll are built from the same tree, since the mod
# links against it; the default is the pinned submodule.
set(DA_UE4SS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/RE-UE4SS" CACHE PATH "Path to the RE-UE4SS source tree")

if(NOT EXISTS "${DA_UE4SS_SOURCE_DIR}/deps/first/Unreal/CMakeLists.txt")
    message(FATAL_ERROR
        "The RE-UE4SS source tree at '${DA_UE4SS_SOURCE_DIR}' is incomplete. "
        "Run scripts\\setup.cmd to fetch the submodules (UE4SS's UEPseudo submodule needs a "
        "GitHub account linked to Epic Games), or pass -DDA_UE4SS_SOURCE_DIR=<an RE-UE4SS checkout>.")
endif()

add_subdirectory("${DA_UE4SS_SOURCE_DIR}" RE-UE4SS)

# The release's DLLs name no folder of the machine that built them. The linker records UE4SS's
# and its loader's symbol files by name alone, as the mod's own, and the Rust code in UE4SS (its
# pattern scanner) records its sources below Cargo's home, which lies in the user's profile, and
# below the UE4SS tree by those short names instead of full paths. RUSTFLAGS splits at spaces,
# so a folder whose path has a space keeps its full path.
if(MSVC)
    foreach(da_target UE4SS proxy)
        target_link_options(${da_target} PRIVATE /PDBALTPATH:%_PDB%)
    endforeach()
endif()
if(DEFINED ENV{CARGO_HOME})
    set(da_cargo_home "$ENV{CARGO_HOME}")
else()
    set(da_cargo_home "$ENV{USERPROFILE}/.cargo")
endif()
set(da_rustflags "")
foreach(da_remap IN ITEMS "cargo|${da_cargo_home}" "RE-UE4SS|${DA_UE4SS_SOURCE_DIR}")
    string(REPLACE "|" ";" da_remap "${da_remap}")
    list(GET da_remap 0 da_name)
    list(GET da_remap 1 da_path)
    file(TO_NATIVE_PATH "${da_path}" da_path)
    string(FIND "${da_path}" " " da_space)
    if(da_space EQUAL -1)
        list(APPEND da_rustflags "--remap-path-prefix=${da_path}=${da_name}")
    endif()
endforeach()
if(da_rustflags AND TARGET patternsleuth_bind AND COMMAND corrosion_add_target_rustflags)
    corrosion_add_target_rustflags(patternsleuth_bind ${da_rustflags})
endif()

add_subdirectory(src)
