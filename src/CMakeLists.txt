include(GNUInstallDirs)

file(GLOB CODING_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/coding/*.h")
file(GLOB CODING_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/coding/*.c")
file(GLOB LAYOUT_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/layout/*.h")
file(GLOB LAYOUT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/layout/*.c")
file(GLOB META_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/meta/*.h")
file(GLOB META_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/meta/*.c")
set(EXT_HEADERS
	${CMAKE_SOURCE_DIR}/ext_includes/clHCA.h
	${CMAKE_SOURCE_DIR}/ext_includes/pstdint.h)
set(EXT_SOURCES
	${CMAKE_SOURCE_DIR}/ext_libs/clHCA.c)
file(GLOB MAIN_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
file(GLOB MAIN_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.c")

# stack_allow.h is not being used, so no need to include it in the sources
list(REMOVE_ITEM MAIN_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/stack_alloc.h)

# Setup source groups, mainly for Visual Studio
source_group("Header Files\\coding" FILES ${CODING_HEADERS})
source_group("Header Files\\layout" FILES ${LAYOUT_HEADERS})
source_group("Header Files\\meta" FILES ${META_HEADERS})
source_group("Header Files\\ext" FILES ${EXT_HEADERS})
source_group("Source Files\\coding" FILES ${CODING_SOURCES})
source_group("Source Files\\layout" FILES ${LAYOUT_SOURCES})
source_group("Source Files\\meta" FILES ${META_SOURCES})
source_group("Source Files\\ext" FILES ${EXT_SOURCES})

add_library(vgmstream STATIC
	${CODING_HEADERS}
	${CODING_SOURCES}
	${LAYOUT_HEADERS}
	${LAYOUT_SOURCES}
	${META_HEADERS}
	${META_SOURCES}
	${EXT_HEADERS}
	${EXT_SOURCES}
	${MAIN_HEADERS}
	${MAIN_SOURCES})

setup_target(vgmstream)

# Set up the proper include directories
target_include_directories(vgmstream PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	coding
	layout
	meta)

if(MSVC)
	target_compile_definitions (vgmstream PRIVATE "USE_ALLOCA")
endif()

# Install library
install(TARGETS vgmstream
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)

# Install headers
install( FILES ${CMAKE_CURRENT_SOURCE_DIR}/vgmstream.h DESTINATION include/libvgmstream )
install( FILES ${CMAKE_CURRENT_SOURCE_DIR}/util.h DESTINATION include/libvgmstream )
install( FILES ${CMAKE_CURRENT_SOURCE_DIR}/util.h DESTINATION include/libvgmstream )
install( FILES ${CMAKE_CURRENT_SOURCE_DIR}/streamtypes.h DESTINATION include/libvgmstream )
install( FILES ${CMAKE_CURRENT_SOURCE_DIR}/streamfile.h DESTINATION include/libvgmstream )
install( FILES ${CMAKE_CURRENT_SOURCE_DIR}/../ext_includes/clHCA.h DESTINATION include/libvgmstream )
install( FILES ${CMAKE_CURRENT_SOURCE_DIR}/coding/g72x_state.h DESTINATION include/libvgmstream/coding )
install( FILES ${CMAKE_CURRENT_SOURCE_DIR}/coding/nwa_decoder.h DESTINATION include/libvgmstream/coding )
