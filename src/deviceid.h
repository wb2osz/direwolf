
// deviceid.h

void deviceid_init(void);
void deviceid_decode_dest (char *dest, char *device, size_t device_size);
void deviceid_decode_mice (char *comment, char *trimmed, size_t trimmed_size, char *device, size_t device_size);
