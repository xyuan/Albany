

# 1. Copy Input file from source to binary dir
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/input.xml
               ${CMAKE_CURRENT_BINARY_DIR}/input.xml COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/ns.exo
               ${CMAKE_CURRENT_BINARY_DIR}/ns.exo COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/ns-m4-bKL.par
               ${CMAKE_CURRENT_BINARY_DIR}/ns-m4-bKL.par COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/ns-m4-bKL.par.4.0
               ${CMAKE_CURRENT_BINARY_DIR}/ns-m4-bKL.par.4.0 COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/ns-m4-bKL.par.4.1
               ${CMAKE_CURRENT_BINARY_DIR}/ns-m4-bKL.par.4.1 COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/ns-m4-bKL.par.4.2
               ${CMAKE_CURRENT_BINARY_DIR}/ns-m4-bKL.par.4.2 COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/ns-m4-bKL.par.4.3
               ${CMAKE_CURRENT_BINARY_DIR}/ns-m4-bKL.par.4.3 COPYONLY)
# 2. Name the test with the directory name
get_filename_component(testName ${CMAKE_CURRENT_SOURCE_DIR} NAME)
# 3. Create the test with this name and standard executable
add_test(${testName} ${Albany.exe})
# disable test - not conserving energy for some reason
set_tests_properties(${testName}  PROPERTIES REQUIRED_FILES "TEST DISABLED!!! NOT CONSERVING ENERGY")

# 4. Repeat process for SG problems if "inputSG.xml" exists
if (ALBANY_SG_MP)
if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/inputSG.xml)
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/inputSG.xml
                 ${CMAKE_CURRENT_BINARY_DIR}/inputSG.xml COPYONLY)
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/input.xml
                 ${CMAKE_CURRENT_BINARY_DIR}/input.xml COPYONLY)
  get_filename_component(testName ${CMAKE_CURRENT_SOURCE_DIR}_SG NAME)
  add_test(${testName} ${AlbanySG.exe})
endif (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/inputSG.xml)
endif ()

# 5. Repeat process for Dakota problems if "dakota.in" exists
if (ALBANY_DAKOTA)
if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/dakota.in)
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/dakota.in
                 ${CMAKE_CURRENT_BINARY_DIR}/dakota.in COPYONLY)
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/input.xml
                 ${CMAKE_CURRENT_BINARY_DIR}/input.xml COPYONLY)
  if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/dakota_nls.data)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/dakota_nls.data
                   ${CMAKE_CURRENT_BINARY_DIR}/dakota_nls.data COPYONLY)
  endif (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/dakota_nls.data)
  get_filename_component(testName ${CMAKE_CURRENT_SOURCE_DIR}_Dakota NAME)
  add_test(${testName} ${AlbanyDakota.exe})
endif (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/dakota.in)
endif()

# 6. Repeat process for Analysis problems if "inputAnalysis.xml" exists
if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/inputAnalysis.xml)
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/inputAnalysis.xml
                 ${CMAKE_CURRENT_BINARY_DIR}/inputAnalysis.xml COPYONLY)

  if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/dakota.in)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/dakota.in
                   ${CMAKE_CURRENT_BINARY_DIR}/dakota.in COPYONLY)
  endif (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/dakota.in)

  if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/Moocho.opt)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Moocho.opt
                   ${CMAKE_CURRENT_BINARY_DIR}/Moocho.opt COPYONLY)
  endif (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/Moocho.opt)

  get_filename_component(testName ${CMAKE_CURRENT_SOURCE_DIR}_Analysis NAME)
  add_test(${testName} ${AlbanyAnalysis.exe})
endif (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/inputAnalysis.xml)
