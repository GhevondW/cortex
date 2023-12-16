include(cmake/CPM.cmake)

# Done as a function so that updates to variables like
# CMAKE_CXX_FLAGS don't propagate out to other
# targets
function(cortex_setup_dependencies)

  # Check if Boost is already available
  find_package(Boost QUIET)

  # If Boost is not found, add it
  if(NOT Boost_FOUND)
    # boost is a huge project and will take a while to download
    # using `CPM_SOURCE_CACHE` is strongly recommended
    CPMAddPackage(
            NAME Boost
            VERSION 1.81.0
            GITHUB_REPOSITORY "boostorg/boost"
            GIT_TAG "boost-1.81.0"
    )
  endif()

  # Check if the target exists
  if(NOT TARGET gtest)
      CPMAddPackage(
          NAME googletest
          GITHUB_REPOSITORY google/googletest
          GIT_TAG release-1.12.1
          VERSION 1.12.1
          OPTIONS "INSTALL_GTEST OFF" "gtest_force_shared_crt"
      )
  endif()

endfunction()
