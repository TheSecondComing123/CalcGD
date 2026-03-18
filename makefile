# ----------------------------
# Makefile Options
# ----------------------------

NAME = CALCGD
ICON = icon.png
DESCRIPTION = "CalcGD"
COMPRESSED = YES
ARCHIVED = NO

CFLAGS = -Wall -Wextra -Oz
CXXFLAGS = -Wall -Wextra -Oz

# ----------------------------
# Normally, you should not need to edit below this line
# ----------------------------
ifndef CEDEV
$(error CEDEV environment variable is not set. Install the CE C/C++ Toolchain.)
endif

include $(CEDEV)/meta/makefile.mk
