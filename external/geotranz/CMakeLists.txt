# UTM, USNG, MGRS conversions

set(GEOTRANZ_LIBRARIES geotranz CACHE INTERNAL "geotranz")

list(APPEND geotranz_SOURCES
  error_string.c
  mgrs.c
  polarst.c
  tranmerc.c
  ups.c
  usng.c
  utm.c
  )

add_library(geotranz STATIC
  ${geotranz_SOURCES}
  )
