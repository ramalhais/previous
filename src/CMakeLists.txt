set(SOURCES
	adb.c audio.c bmap.c cfgopts.c configuration.c change.c cycInt.c 
	dialog.c dma.c esp.c enet_slirp.c enet_pcap.c ethernet.c file.c 
	floppy.c ioMem.c ioMemTabNEXT.c ioMemTabTurbo.c keymap.c kms.c 
	m68000.c main.c mo.c nbic.c NextBus.cpp paths.c printer.c queue.c 
	ramdac.c reset.c rs.c rtcnvram.c scandir.c scc.c fast_screen.c host.c 
	scsi.c shortcut.c snd.c statusbar.c str.c sysReg.c tmc.c unzip.c 
	utils.c video.c zip.c)

# When building for OSX, define specific sources for gui and ressources
if(ENABLE_OSX_BUNDLE)
	set(GUIOSX_RSRCS
	    gui-osx/Previous.icns gui-osx/English.lproj gui-osx/French.lproj)
endif(ENABLE_OSX_BUNDLE)


# When building for Windows, define specific sources for gui and resources
# and set the subsytem of the resulting .exe to "windows GUI" instead of "console"
if(WIN32)
	set(GUIWIN_SOURCES gui-win/opencon.c)
    set(GUIWIN_RES gui-win/previous-winicon.rc)
	#set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -mwindows")
	add_definitions(-DSDL_MAIN_HANDLED)
endif(WIN32)

include_directories(${CMAKE_BINARY_DIR} includes debug softfloat cpu
		    dsp dimension slirp ${SDL2_INCLUDE_DIR})

if(ZLIB_FOUND)
	include_directories(${ZLIB_INCLUDE_DIR})
endif(ZLIB_FOUND)

if(PNG_FOUND)
	include_directories(${PNG_INCLUDE_DIR})
endif(PNG_FOUND)

if(PCAP_FOUND)
	include_directories(${PCAP_INCLUDE_DIR})
endif(PCAP_FOUND)

link_directories(${CMAKE_CURRENT_BINARY_DIR}/debug ${CMAKE_CURRENT_BINARY_DIR}/softfloat ${CMAKE_CURRENT_BINARY_DIR}/gui-sdl ${CMAKE_CURRENT_BINARY_DIR}/cpu ${CMAKE_CURRENT_BINARY_DIR}/dsp ${CMAKE_CURRENT_BINARY_DIR}/dimension ${CMAKE_CURRENT_BINARY_DIR}/slirp)

add_subdirectory(debug)
add_subdirectory(softfloat)
add_subdirectory(gui-sdl)
add_subdirectory(cpu)
add_subdirectory(dsp)
add_subdirectory(dimension)
add_subdirectory(slirp)
add_subdirectory(ditool)

# When building for OSX, add specific sources
if(ENABLE_OSX_BUNDLE)
	add_executable(Previous MACOSX_BUNDLE ${GUIOSX_RSRCS} ${SOURCES})
	set_target_properties(Previous PROPERTIES MACOSX_BUNDLE_INFO_PLIST
			      ${CMAKE_CURRENT_SOURCE_DIR}/gui-osx/Info-Previous.plist)
	set(MACOSX_BUNDLE_ICON_FILE Previous.icns)
	# Create Previous.app bundle
	add_custom_target(osx_bundle_dirs
			COMMAND mkdir -p ${CMAKE_CURRENT_BINARY_DIR}/Previous.app/Contents/Resources
			COMMAND mkdir -p ${CMAKE_CURRENT_BINARY_DIR}/Previous.app/Contents/MacOS
			COMMAND cp ${CMAKE_CURRENT_SOURCE_DIR}/gui-osx/${MACOSX_BUNDLE_ICON_FILE}
				   ${CMAKE_CURRENT_BINARY_DIR}/Previous.app/Contents/Resources/${MACOSX_BUNDLE_ICON_FILE}
			# Copy Localized .nib to Bundle
			COMMAND cp -R ${CMAKE_CURRENT_SOURCE_DIR}/gui-osx/*.lproj ${CMAKE_CURRENT_BINARY_DIR}/Previous.app/Contents/Resources/
	)	
	add_dependencies(Previous osx_bundle_dirs)
	set_source_files_properties(${GUIOSX_RSRCS}
			PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

# When building for Windows, add specific sources + method to compile .rc files
elseif(WIN32)
	# Set a default rc compiler if it was not defined yet
	if(NOT CMAKE_RC_COMPILER)
		set(CMAKE_RC_COMPILER windres)
	endif(NOT CMAKE_RC_COMPILER)
	ENABLE_LANGUAGE(RC)
	set(CMAKE_RC_COMPILE_OBJECT "<CMAKE_RC_COMPILER> -Ocoff -o <OBJECT> <SOURCE>")
	set_source_files_properties(${GUIWIN_RES} PROPERTIES LANGUAGE RC)
	add_executable(Previous WIN32 ${GUIWIN_RES} ${SOURCES} ${GUIWIN_SOURCES})

# Other targets, use default sources
else()
	add_executable(Previous ${SOURCES})
endif(ENABLE_OSX_BUNDLE)

target_link_libraries(Previous Debug GuiSdl UaeCpu DSP Dimension Slirp SoftFloat ${SDL2_LIBRARY})

if(MATH_FOUND AND NOT APPLE)
	target_link_libraries(Previous ${MATH_LIBRARY})
endif()

if(SDL2MAIN_LIBRARY)
	target_link_libraries(Previous ${SDL2MAIN_LIBRARY})
endif(SDL2MAIN_LIBRARY)

if(READLINE_FOUND)
	target_link_libraries(Previous ${READLINE_LIBRARY})
endif(READLINE_FOUND)

if(ZLIB_FOUND)
	target_link_libraries(Previous ${ZLIB_LIBRARY})
endif(ZLIB_FOUND)

if(PNG_FOUND)
	target_link_libraries(Previous ${PNG_LIBRARY})
endif(PNG_FOUND)

if(PCAP_FOUND)
	target_link_libraries(Previous ${PCAP_LIBRARY})
endif(PCAP_FOUND)

if(WIN32)
	# Needed for socket() on Windows
	target_link_libraries(Previous ws2_32 Iphlpapi)
endif(WIN32)

if(HAIKU)
	# Needed for socket() on Haiku
	target_link_libraries(Previous network)
endif(HAIKU)

if(ENABLE_OSX_BUNDLE)
	install(TARGETS Previous BUNDLE DESTINATION /Applications)
else()
	install(TARGETS Previous RUNTIME DESTINATION ${BINDIR})
	install(FILES Previous-icon.bmp DESTINATION ${DATADIR})
	install(FILES ND_step1_v43_eeprom.bin DESTINATION ${BINDIR})
	install(FILES Rev_1.0_v41.BIN DESTINATION ${BINDIR})
	install(FILES Rev_2.5_v66.BIN DESTINATION ${BINDIR})
	install(FILES Rev_3.3_v74.BIN DESTINATION ${BINDIR})
endif(ENABLE_OSX_BUNDLE)
