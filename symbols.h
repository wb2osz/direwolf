
/* symbols.h */

void symbols_init (void);

void symbols_from_dest_or_src (char dti, char *src, char *dest, char *symtab, char *symbol);

int symbols_into_dest (char symtab, char symbol, char *dest);

void symbols_get_description (char symtab, char symbol, char *description);

int symbols_code_from_description (char overlay, char *description, char *symtab, char *symbol);

/* end symbols.h */
