# Copyright 2019-2020 CERN and copyright holders of ALICE O2.
# See https://alice-o2.web.cern.ch/copyright for details of the copyright holders.
# All rights not expressly granted are reserved.
#
# This software is distributed under the terms of the GNU General Public
# License v3 (GPL Version 3), copied verbatim in the file "COPYING".
#
# In applying this license CERN does not waive the privileges and immunities
# granted to it by virtue of its status as an Intergovernmental Organization
# or submit itself to any jurisdiction.

# TODO: remove this file once FairRoot correctly exports its cmake config

find_path(FairRoot_INC FairDetector.h
          PATH_SUFFIXES FairRoot/include
          PATHS ${FAIRROOTPATH}/include
          ${FAIRROOT_ROOT}/include
          $ENV{FAIRROOT_ROOT}/include)

get_filename_component(FairRoot_TOPDIR "${FairRoot_INC}/.." ABSOLUTE)

set(OLD_CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH})
set(CMAKE_PREFIX_PATH ${FairRoot_TOPDIR})

find_library(FairRoot_Tools FairTools)
find_library(FairRoot_ParBase ParBase)
find_library(FairRoot_GeoBase GeoBase)
find_library(FairRoot_Base Base)
find_library(FairRoot_ParMQ ParMQ)
find_library(FairRoot_Gen Gen)

set(CMAKE_PREFIX_PATH ${OLD_CMAKE_PREFIX_PATH})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FairRoot
                                  DEFAULT_MSG FairRoot_Base
                                              FairRoot_Tools
                                              FairRoot_ParBase
                                              FairRoot_GeoBase
                                              FairRoot_ParMQ
                                              FairRoot_Gen
                                              FairRoot_INC)

if(NOT TARGET FairRoot::Tools)
  add_library(FairRoot::Tools IMPORTED INTERFACE)
  set_target_properties(FairRoot::Tools
                        PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${FairRoot_INC}
                                   INTERFACE_LINK_LIBRARIES ${FairRoot_Tools})
  target_link_libraries(FairRoot::Tools INTERFACE FairLogger::FairLogger)
endif()

if(NOT TARGET FairRoot::ParBase)
  add_library(FairRoot::ParBase IMPORTED INTERFACE)
  set_target_properties(FairRoot::ParBase
                        PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${FairRoot_INC}
                                   INTERFACE_LINK_LIBRARIES ${FairRoot_ParBase})
  target_link_libraries(FairRoot::ParBase INTERFACE FairRoot::Tools)
endif()

if(NOT TARGET FairRoot::GeoBase)
  add_library(FairRoot::GeoBase IMPORTED INTERFACE)
  set_target_properties(FairRoot::GeoBase
                        PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${FairRoot_INC}
                                   INTERFACE_LINK_LIBRARIES ${FairRoot_GeoBase})
endif()

if(NOT TARGET FairRoot::Base)
  add_library(FairRoot::Base IMPORTED INTERFACE)
  get_filename_component(libdir ${FairRoot_Base} DIRECTORY)
  set_target_properties(FairRoot::Base
                        PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${FairRoot_INC}
                                   INTERFACE_LINK_LIBRARIES ${FairRoot_Base}
                                   INTERFACE_LINK_DIRECTORIES ${libdir})
  target_link_libraries(FairRoot::Base
                        INTERFACE FairRoot::Tools FairRoot::ParBase FairRoot::GeoBase MC::VMC)
endif()

if(NOT TARGET FairRoot::ParMQ)
  add_library(FairRoot::ParMQ IMPORTED INTERFACE)
  set_target_properties(FairRoot::ParMQ
                        PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${FairRoot_INC}
                                   INTERFACE_LINK_LIBRARIES ${FairRoot_ParMQ})
  target_link_libraries(FairRoot::ParMQ
                        INTERFACE FairRoot::ParBase FairMQ::FairMQ)
endif()

if(NOT TARGET FairRoot::Gen)
  add_library(FairRoot::Gen IMPORTED INTERFACE)
  set_target_properties(FairRoot::Gen
                        PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${FairRoot_INC}
                                   INTERFACE_LINK_LIBRARIES ${FairRoot_Gen})
  target_link_libraries(FairRoot::Gen
                        INTERFACE FairRoot::ParBase FairRoot::Base
                                  FairRoot::ParBase)
endif()
