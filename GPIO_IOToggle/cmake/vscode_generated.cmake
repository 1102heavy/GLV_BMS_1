# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Compiler options
target_compile_options(${BUILD_UNIT_0_NAME} PRIVATE
    $<$<COMPILE_LANGUAGE:C>: ${CUBE_CMAKE_C_FLAGS}>
    $<$<COMPILE_LANGUAGE:CXX>: ${CUBE_CMAKE_CXX_FLAGS}>
    $<$<COMPILE_LANGUAGE:ASM>: ${CUBE_CMAKE_ASM_FLAGS}>
)

# Linker options
target_link_options(${BUILD_UNIT_0_NAME} PRIVATE ${CUBE_CMAKE_EXE_LINKER_FLAGS})

# Add sources to executable/library
target_sources(${BUILD_UNIT_0_NAME} PRIVATE
    "readme.txt"
    "Drivers/BSP/STM32F4xx-Nucleo/stm32f4xx_nucleo.c"
    "Src/system_stm32f4xx.c"
    "Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal.c"
    "Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_adc.c"
    "Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_adc_ex.c"
    "Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_cortex.c"
    "Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dma.c"
    "Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dma_ex.c"
    "Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash.c"
    "Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash_ex.c"
    "Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_gpio.c"
    "Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr_ex.c"
    "Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc.c"
    "Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc_ex.c"
    "Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_spi.c"
    "Example/Startup/startup_stm32f446retx.s"
    "Src/main.c"
    "Src/stm32f4xx_it.c"
    "Example/User/syscalls.c"
    "Example/User/sysmem.c"
)

target_include_directories(${BUILD_UNIT_0_NAME} PRIVATE
    "Inc"
    "Drivers/CMSIS/Device/ST/STM32F4xx/Include"
    "Drivers/STM32F4xx_HAL_Driver/Inc"
    "Drivers/BSP/Components"
    "Drivers/BSP/STM32F4xx-Nucleo"
    "Drivers/BSP/Components/Common"
    "Utilities/Log"
    "Utilities/Fonts"
    "Utilities/CPU"
    "Drivers/CMSIS/Include"
)

configure_file("${CMAKE_SOURCE_DIR}/STM32F446RETX_FLASH.ld" "${CMAKE_BINARY_DIR}" COPYONLY)

set_target_properties(${BUILD_UNIT_0_NAME} PROPERTIES LINK_DEPENDS "STM32F446RETX_FLASH.ld")

