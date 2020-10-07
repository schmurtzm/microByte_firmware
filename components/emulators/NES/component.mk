#
# Component Makefile
#
# This Makefile can be left empty. By default, it will take the sources in the
# src/ directory, compile them and link them into lib(subdirectory_name).a
# in the build directory. This behaviour is entirely configurable,
# please read the ESP-IDF documents if you need to do this.
#

COMPONENT_DEPENDS := nofrendo
COMPONENT_ADD_INCLUDEDIRS := .

CFLAGS += -Wno-error=char-subscripts -Wno-error=attributes -DNOFRENDO_DEBUG

