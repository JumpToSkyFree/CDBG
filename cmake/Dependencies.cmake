find_package(ftxui REQUIRED)

find_package(fmt REQUIRED)

find_path(POPL_INCLUDE_DIR popl.hpp)
if(NOT POPL_INCLUDE_DIR)
    message(FATAL_ERROR
        "popl.hpp not found. Run setup-deps-fedora.sh, or install it manually "
        "into a directory on your include path.")
endif()

add_library(popl INTERFACE)
target_include_directories(popl INTERFACE ${POPL_INCLUDE_DIR})
add_library(popl::popl ALIAS popl)

find_path(TREE_SITTER_INCLUDE_DIR tree_sitter/api.h)
find_library(TREE_SITTER_LIBRARY tree-sitter)

if(NOT TREE_SITTER_INCLUDE_DIR OR NOT TREE_SITTER_LIBRARY)
    message(FATAL_ERROR
        "libtree-sitter not found. Install libtree-sitter-devel "
        "(see setup-deps-fedora.sh).")
endif()

add_library(tree-sitter INTERFACE)
target_include_directories(tree-sitter INTERFACE ${TREE_SITTER_INCLUDE_DIR})
target_link_libraries(tree-sitter INTERFACE ${TREE_SITTER_LIBRARY})

find_library(TREE_SITTER_C_LIBRARY tree-sitter-c)
find_library(TREE_SITTER_CPP_LIBRARY tree-sitter-cpp)

if(NOT TREE_SITTER_C_LIBRARY OR NOT TREE_SITTER_CPP_LIBRARY)
    message(FATAL_ERROR
        "tree-sitter-c / tree-sitter-cpp grammar libraries not found. "
        "Re-run setup-deps-fedora.sh.")
endif()

add_library(tree-sitter-c STATIC IMPORTED)
set_target_properties(tree-sitter-c PROPERTIES
    IMPORTED_LOCATION ${TREE_SITTER_C_LIBRARY}
)

add_library(tree-sitter-cpp STATIC IMPORTED)
set_target_properties(tree-sitter-cpp PROPERTIES
    IMPORTED_LOCATION ${TREE_SITTER_CPP_LIBRARY}
)
