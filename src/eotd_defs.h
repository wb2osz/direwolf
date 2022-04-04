#ifndef __EOTD_DEFS
#define __EOTD_DEFS
#define EOTD_LENGTH			8

#define EOTD_PREAMBLE_AND_BARKER_CODE   0x48eaaaaa00000000ULL
#define EOTD_PREAMBLE_MASK              0xffffffff00000000ULL

#define HOTD_PREAMBLE_AND_BARKER_CODE   0x9488f1aa00000000ULL
#define HOTD_PREAMBLE_MASK              0xffffffff00000000ULL

#define EOTD_TYPE_F2R			'F'
#define EOTD_TYPE_R2F			'R'

#endif
