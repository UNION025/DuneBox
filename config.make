# Project settings for the make-based (macOS) build. Not used by Visual Studio.

APPNAME = DuneBox

# dlib (src/KinectProjector/libs/dlib) uses std::binary_function, which C++17
# removed. MSVC gets the same fix via _HAS_AUTO_PTR_ETC=1 in the CI workflow.
PROJECT_CFLAGS += -D_LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION
