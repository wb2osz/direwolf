# UTM, USNG, MGRS conversions

set(GEOTRANZ_LIBRARIES geotranz CACHE INTERNAL "geotranz")

set(geotranz_SOURCES
  ${geotranz_SOURCES}
  error_string.c
  mgrs.c
  polarst.c
  tranmerc.c
  ups.c
  usng.c
  utm.c
  )

ADD_LIBRARY(geotranz STATIC
  ${geotranz_SOURCES}
  )
