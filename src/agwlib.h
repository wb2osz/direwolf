
#ifndef AGWLIB_H
#define AGWLIB_H 1


// Call at beginning to start it up.

int agwlib_init (char *host, char *port, int (*init_func)(void));



// Send commands to TNC.


int agwlib_X_register_callsign (int chan, char *call_from);

int agwlib_x_unregister_callsign (int chan, char *call_from);

int agwlib_G_ask_port_information (void);

int agwlib_C_connect (int chan, char *call_from, char *call_to);

int agwlib_d_disconnect (int chan, char *call_from, char *call_to);

int agwlib_D_send_connected_data (int chan, int pid, char *call_from, char *call_to, int data_len, char *data);

int agwlib_Y_outstanding_frames_for_station (int chan, char *call_from, char *call_to);



// The application must define these.

void agw_cb_C_connection_received (int chan, char *call_from, char *call_to, int data_len, char *data);
void on_C_connection_received (int chan, char *call_from, char *call_to, int incoming, char *data);

void agw_cb_d_disconnected (int chan, char *call_from, char *call_to, int data_len, char *data);

void agw_cb_D_connected_data (int chan, char *call_from, char *call_to, int data_len, char *data);

void agw_cb_G_port_information (int num_chan, char *chan_descriptions[]);

void agw_cb_Y_outstanding_frames_for_station (int chan, char *call_from, char *call_to, int frame_count);


#endif