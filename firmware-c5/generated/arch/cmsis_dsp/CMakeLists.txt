# origin-pack: STMicroelectronics::CMSIS-DSP@1.17.0
# file-format: 1.0.0
project(STMicroelectronics_CMSIS_DSP_1_17_0)
cmake_minimum_required(VERSION 3.30)
add_library(STMicroelectronics_CMSIS_DSP_1_17_0 INTERFACE)

# List of all CMSIS properties that influence conditions for this package
if(NOT DEFINED CMSIS_Dcore)
  set(CMSIS_Dcore "CMSIS_Dcore not set")
  message(DEBUG "CMSIS_Dcore is not set to any value")
endif()


# Device specific defined symbols








# Enable all components in this package
if(CMSIS_ENTIRE_STMicroelectronics_CMSIS_DSP_1_17_0)
  list(APPEND CMSIS_COMPONENTS_LIST "Cvendor:STMicroelectronics#Cclass:CMSIS#Cgroup:DSP#Cvariant:Source#Cversion:1.17.0")
endif()

# All conditions used by this package

# condition: STMicroelectronics.CMSIS-DSP.1.17.0:ARMv7-A Device
# description: Armv7-A architecture based device
if((CMSIS_Dcore STREQUAL "Cortex-A5" OR CMSIS_Dcore STREQUAL "Cortex-A7" OR CMSIS_Dcore STREQUAL "Cortex-A9"))
  set(STMicroelectronics.CMSIS-DSP.1.17.0_ARMv7-A_Device "1")
  message(DEBUG "CMSIS condition STMicroelectronics.CMSIS-DSP.1.17.0_ARMv7-A_Device enabled")
else()
  set(STMicroelectronics.CMSIS-DSP.1.17.0_ARMv7-A_Device 0)
endif()


# condition: STMicroelectronics.CMSIS-DSP.1.17.0:CMSISCORE
# description: Components required for CMSIS Core
set(STMicroelectronics.CMSIS-DSP.1.17.0_CMSISCORE "$<NOT:$<STREQUAL:$<FILTER:${CMSIS_COMPONENTS_LIST},INCLUDE,.*Cclass:CMSIS#.*Cgroup:CORE(#.*|$)>,>>")
message(DEBUG "CMSIS condition STMicroelectronics.CMSIS-DSP.1.17.0_CMSISCORE enabled")

# Files and components in this package
if("Cvendor:STMicroelectronics#Cclass:CMSIS#Cgroup:DSP#Cvariant:Source#Cversion:1.17.0" IN_LIST CMSIS_COMPONENTS_LIST)  # CMSIS-DSP Library for Cortex-M and Cortex-A
  message(DEBUG "Using component CMSIS_DSP_Source_1_17_0")
  target_compile_definitions(STMicroelectronics_CMSIS_DSP_1_17_0 INTERFACE "$<${STMicroelectronics.CMSIS-DSP.1.17.0_CMSISCORE}:-DCMSIS_USE_CMSIS_DSP_Source_1_17_0=1>")
  target_include_directories(STMicroelectronics_CMSIS_DSP_1_17_0 INTERFACE "$<${STMicroelectronics.CMSIS-DSP.1.17.0_CMSISCORE}:${CMAKE_CURRENT_LIST_DIR}/Include>")
  target_include_directories(STMicroelectronics_CMSIS_DSP_1_17_0 INTERFACE "$<${STMicroelectronics.CMSIS-DSP.1.17.0_CMSISCORE}:${CMAKE_CURRENT_LIST_DIR}/Include/>")
  target_include_directories(STMicroelectronics_CMSIS_DSP_1_17_0 INTERFACE "$<${STMicroelectronics.CMSIS-DSP.1.17.0_CMSISCORE}:${CMAKE_CURRENT_LIST_DIR}/PrivateInclude/>")
  target_sources(STMicroelectronics_CMSIS_DSP_1_17_0 INTERFACE "$<${STMicroelectronics.CMSIS-DSP.1.17.0_CMSISCORE}:${CMAKE_CURRENT_LIST_DIR}/Source/BasicMathFunctions/BasicMathFunctions.c>")
  target_sources(STMicroelectronics_CMSIS_DSP_1_17_0 INTERFACE "$<${STMicroelectronics.CMSIS-DSP.1.17.0_CMSISCORE}:${CMAKE_CURRENT_LIST_DIR}/Source/BasicMathFunctions/BasicMathFunctionsF16.c>")
  target_sources(STMicroelectronics_CMSIS_DSP_1_17_0 INTERFACE "$<${STMicroelectronics.CMSIS-DSP.1.17.0_CMSISCORE}:${CMAKE_CURRENT_LIST_DIR}/Source/BayesFunctions/BayesFunctions.c>")
  target_sources(STMicroelectronics_CMSIS_DSP_1_17_0 INTERFACE "$<${STMicroelectronics.CMSIS-DSP.1.17.0_CMSISCORE}:${CMAKE_CURRENT_LIST_DIR}/Source/BayesFunctions/BayesFunctionsF16.c>")
  target_sources(STMicroelectronics_CMSIS_DSP_1_17_0 INTERFACE "$<${STMicroelectronics.CMSIS-DSP.1.17.0_CMSISCORE}:${CMAKE_CURRENT_LIST_DIR}/Source/CommonTables/CommonTables.c>")
  target_sources(STMicroelectronics_CMSIS_DSP_1_17_0 INTERFACE "$<${STMicroelectronics.CMSIS-DSP.1.17.0_CMSISCORE}:${CMAKE_CURRENT_LIST_DIR}/Source/CommonTables/CommonTablesF16.c>")
  target_sources(STMicroelectronics_CMSIS_DSP_1_17_0 INTERFACE "$<${STMicroelectronics.CMSIS-DSP.1.17.0_CMSISCORE}:${CMAKE_CURRENT_LIST_DIR}/Source/ComplexMathFunctions/ComplexMathFunctions.c>")
  target_sources(STMicroelectronics_CMSIS_DSP_1_17_0 INTERFACE "$<${STMicroelectronics.CMSIS-DSP.1.17.0_CMSISCORE}:${CMAKE_CURRENT_LIST_DIR}/Source/ComplexMathFunctions/ComplexMathFunctionsF16.c>")
  target_sources(STMicroelectronics_CMSIS_DSP_1_17_0 INTERFACE "$<${STMicroelectronics.CMSIS-DSP.1.17.0_CMSISCORE}:${CMAKE_CURRENT_LIST_DIR}/Source/ControllerFunctions/ControllerFunctions.c>")
  target_sources(STMicroelectronics_CMSIS_DSP_1_17_0 INTERFACE "$<${STMicroelectronics.CMSIS-DSP.1.17.0_CMSISCORE}:${CMAKE_CURRENT_LIST_DIR}/Source/DistanceFunctions/DistanceFunctions.c>")
  target_sources(STMicroelectronics_CMSIS_DSP_1_17_0 INTERFACE "$<${STMicroelectronics.CMSIS-DSP.1.17.0_CMSISCORE}:${CMAKE_CURRENT_LIST_DIR}/Source/DistanceFunctions/DistanceFunctionsF16.c>")
  target_sources(STMicroelectronics_CMSIS_DSP_1_17_0 INTERFACE "$<${STMicroelectronics.CMSIS-DSP.1.17.0_CMSISCORE}:${CMAKE_CURRENT_LIST_DIR}/Source/FastMathFunctions/FastMathFunctions.c>")
  target_sources(STMicroelectronics_CMSIS_DSP_1_17_0 INTERFACE "$<${STMicroelectronics.CMSIS-DSP.1.17.0_CMSISCORE}:${CMAKE_CURRENT_LIST_DIR}/Source/FastMathFunctions/FastMathFunctionsF16.c>")
  target_sources(STMicroelectronics_CMSIS_DSP_1_17_0 INTERFACE "$<${STMicroelectronics.CMSIS-DSP.1.17.0_CMSISCORE}:${CMAKE_CURRENT_LIST_DIR}/Source/FilteringFunctions/FilteringFunctions.c>")
  target_sources(STMicroelectronics_CMSIS_DSP_1_17_0 INTERFACE "$<${STMicroelectronics.CMSIS-DSP.1.17.0_CMSISCORE}:${CMAKE_CURRENT_LIST_DIR}/Source/FilteringFunctions/FilteringFunctionsF16.c>")
  target_sources(STMicroelectronics_CMSIS_DSP_1_17_0 INTERFACE "$<${STMicroelectronics.CMSIS-DSP.1.17.0_CMSISCORE}:${CMAKE_CURRENT_LIST_DIR}/Source/InterpolationFunctions/InterpolationFunctions.c>")
  target_sources(STMicroelectronics_CMSIS_DSP_1_17_0 INTERFACE "$<${STMicroelectronics.CMSIS-DSP.1.17.0_CMSISCORE}:${CMAKE_CURRENT_LIST_DIR}/Source/InterpolationFunctions/InterpolationFunctionsF16.c>")
  target_sources(STMicroelectronics_CMSIS_DSP_1_17_0 INTERFACE "$<${STMicroelectronics.CMSIS-DSP.1.17.0_CMSISCORE}:${CMAKE_CURRENT_LIST_DIR}/Source/MatrixFunctions/MatrixFunctions.c>")
  target_sources(STMicroelectronics_CMSIS_DSP_1_17_0 INTERFACE "$<${STMicroelectronics.CMSIS-DSP.1.17.0_CMSISCORE}:${CMAKE_CURRENT_LIST_DIR}/Source/MatrixFunctions/MatrixFunctionsF16.c>")
  target_sources(STMicroelectronics_CMSIS_DSP_1_17_0 INTERFACE "$<${STMicroelectronics.CMSIS-DSP.1.17.0_CMSISCORE}:${CMAKE_CURRENT_LIST_DIR}/Source/QuaternionMathFunctions/QuaternionMathFunctions.c>")
  target_sources(STMicroelectronics_CMSIS_DSP_1_17_0 INTERFACE "$<${STMicroelectronics.CMSIS-DSP.1.17.0_CMSISCORE}:${CMAKE_CURRENT_LIST_DIR}/Source/SVMFunctions/SVMFunctions.c>")
  target_sources(STMicroelectronics_CMSIS_DSP_1_17_0 INTERFACE "$<${STMicroelectronics.CMSIS-DSP.1.17.0_CMSISCORE}:${CMAKE_CURRENT_LIST_DIR}/Source/SVMFunctions/SVMFunctionsF16.c>")
  target_sources(STMicroelectronics_CMSIS_DSP_1_17_0 INTERFACE "$<${STMicroelectronics.CMSIS-DSP.1.17.0_CMSISCORE}:${CMAKE_CURRENT_LIST_DIR}/Source/StatisticsFunctions/StatisticsFunctions.c>")
  target_sources(STMicroelectronics_CMSIS_DSP_1_17_0 INTERFACE "$<${STMicroelectronics.CMSIS-DSP.1.17.0_CMSISCORE}:${CMAKE_CURRENT_LIST_DIR}/Source/StatisticsFunctions/StatisticsFunctionsF16.c>")
  target_sources(STMicroelectronics_CMSIS_DSP_1_17_0 INTERFACE "$<${STMicroelectronics.CMSIS-DSP.1.17.0_CMSISCORE}:${CMAKE_CURRENT_LIST_DIR}/Source/SupportFunctions/SupportFunctions.c>")
  target_sources(STMicroelectronics_CMSIS_DSP_1_17_0 INTERFACE "$<${STMicroelectronics.CMSIS-DSP.1.17.0_CMSISCORE}:${CMAKE_CURRENT_LIST_DIR}/Source/SupportFunctions/SupportFunctionsF16.c>")
  target_sources(STMicroelectronics_CMSIS_DSP_1_17_0 INTERFACE "$<${STMicroelectronics.CMSIS-DSP.1.17.0_CMSISCORE}:${CMAKE_CURRENT_LIST_DIR}/Source/TransformFunctions/TransformFunctions.c>")
  target_sources(STMicroelectronics_CMSIS_DSP_1_17_0 INTERFACE "$<${STMicroelectronics.CMSIS-DSP.1.17.0_CMSISCORE}:${CMAKE_CURRENT_LIST_DIR}/Source/TransformFunctions/TransformFunctionsF16.c>")
  target_sources(STMicroelectronics_CMSIS_DSP_1_17_0 INTERFACE "$<${STMicroelectronics.CMSIS-DSP.1.17.0_CMSISCORE}:${CMAKE_CURRENT_LIST_DIR}/Source/WindowFunctions/WindowFunctions.c>")
endif()

