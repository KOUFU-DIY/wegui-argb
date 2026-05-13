/**
  @page mdkarm MDK-ARM Project Template for STM32F0xx devices
  
  @verbatim
  ******************** (C) COPYRIGHT 2014 STMicroelectronics *******************
  * @file    readme.txt
  * @author  MCD Application Team
  * @version V1.6.0
  * @date    13-October-2021
  * @brief   This sub-directory contains all the user-modifiable files 
  *          needed to create a new project linked with the STM32F0xx  
  *          Standard Peripheral Library and working with RealView Microcontroller
  *          Development Kit(MDK-ARM) software toolchain
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2014 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  @endverbatim
 
 @par Directory contents
 
 - Project.uvproj/.uvopt: A pre-configured project file with the provided library structure
                          that produces an executable image with MDK-ARM.

Enabling "Options for Target — Output – Browser Information" is useful for quick 
source files navigation but may slow the compilation time.
 
 
 @par How to use it ?
 
 - Open the Project.uvproj project
 - In the build toolbar select the project config:
      - STM32F051: to configure the project for STM32F051 devices.
                   You can use STMicroelectronics STM320518-EVAL or STM32F0-Discovery
                   board to run this project.

      - STM32F031: to configure the project for STM32F031 devices.
                   You need to use custom HW board to run this project.

      - STM32F030: to configure the project for STM32F030 devices.
                   You can use STM32F0308-Discovery board to run this project.

      - STM32F072: to configure the project for STM32F072 devices.
                  You can use STMicroelectronics STM32072B-EVAL or STM32F072B-Discovery.
                  
      - STM32F042: to configure the project for STM32F042 devices.
                  You need to use custom HW board to run this project.
    
      - STM32F091: to configure the project for STM32F091 devices.
                  You can use STMicroelectronics STM32091C-EVAL.

      - STM32F030xC: to configure the project for STM32F030xC devices.
                  You need to use custom HW board to run this project.
                  
      - STM32F070xB: to configure the project for STM32F070xB devices.
                  You need to use custom HW board to run this project.
                  
      - STM32F070x6: to configure the project for STM32F070x6 devices.
                  You need to use custom HW board to run this project.
      
 - Rebuild all files: Project->Rebuild all target files
 - Load project image: Debug->Start/Stop Debug Session
 - Run program: Debug->Run (F5)

@note The needed define symbols for this config are already declared in the
      preprocessor section: USE_STDPERIPH_DRIVER, STM32F0XX, USE_STM320XXX_EVAL


 * <h3><center>&copy; COPYRIGHT STMicroelectronics</center></h3>
 */