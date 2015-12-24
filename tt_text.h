
/* tt_text.h */


/* Encode normal human readable to DTMF representation. */

int tt_text_to_multipress (const char *text, int quiet, char *buttons);

int tt_text_to_two_key (const char *text, int quiet, char *buttons);

int tt_text_to_call10 (const char *text, int quiet, char *buttons);

int tt_text_to_mhead (const char *text, int quiet, char *buttons, size_t buttonsiz);

int tt_text_to_satsq (const char *text, int quiet, char *buttons, size_t buttonsiz);

int tt_text_to_ascii2d (const char *text, int quiet, char *buttons);


/* Decode DTMF to normal human readable form. */

int tt_multipress_to_text (const char *buttons, int quiet, char *text);

int tt_two_key_to_text (const char *buttons, int quiet, char *text);

int tt_call10_to_text (const char *buttons, int quiet, char *text);

int tt_call5_suffix_to_text (const char *buttons, int quiet, char *text);

int tt_mhead_to_text (const char *buttons, int quiet, char *text, size_t textsiz);

int tt_satsq_to_text (const char *buttons, int quiet, char *text);

int tt_ascii2d_to_text (const char *buttons, int quiet, char *text);



/* end tt_text.h */