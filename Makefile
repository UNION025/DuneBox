# macOS / Linux build (make). Windows builds use projectGenerator + Visual Studio
# (see .github/workflows/build.yml) and ignore this file.
#
# OF_ROOT defaults to an openFrameworks tree next to this repo
# (scripts/setup-mac.sh creates it). Override with: make OF_ROOT=/path/to/of
ifneq ($(wildcard config.make),)
	include config.make
endif
ifndef OF_ROOT
	OF_ROOT=$(realpath ../openFrameworks)
endif
include $(OF_ROOT)/libs/openFrameworksCompiled/project/makefileCommon/compile.project.mk
