
int encode_position (int messaging, int compressed, double lat, double lon, int ambiguity, int alt_ft,
		char symtab, char symbol, 
		int power, int height, int gain, char *dir,
		int course, int speed_knots,
		float freq, float tone, float offset,
		char *comment,
		char *presult, size_t result_size);

int encode_object (char *name, int compressed, time_t thyme, double lat, double lon, int ambiguity,
		char symtab, char symbol, 
		int power, int height, int gain, char *dir,
		int course, int speed_knots,
		float freq, float tone, float offset, char *comment,
		char *presult, size_t result_size);

