INCLUDE("${CMAKE_CURRENT_LIST_DIR}/SPARCMiniTrilinosPackagesList.cmake")
FOREACH(TRIBITS_PACKAGE ${SPARC_MiniTrilinos_Package_Enables})
  SET(${PROJECT_NAME}_ENABLE_${TRIBITS_PACKAGE} ON CACHE BOOL "")
ENDFOREACH()
FOREACH(TRIBITS_PACKAGE ${SPARC_MiniTrilinos_Package_Disables})
  SET(${PROJECT_NAME}_ENABLE_${TRIBITS_PACKAGE} OFF CACHE BOOL "")
ENDFOREACH()
FOREACH(TRIBITS_TPL ${SPARC_MiniTrilinos_TPL_Disables})
  SET(TPL_ENABLE_${TRIBITS_TPL} OFF CACHE BOOL "")
ENDFOREACH()
