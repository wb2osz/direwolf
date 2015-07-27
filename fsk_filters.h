/* 1200 bits/sec with Audio sample rate = 11025 */
/* Mark freq = 1200, Space freq = 2200 */

static const signed short m_sin_table[9] = { 0 , 7347 , 11257 , 9899 , 3909 , -3909 , -9899 , -11257 , -7347  };
static const signed short m_cos_table[9] = { 11431 , 8756 , 1984 , -5715 , -10741 , -10741 , -5715 , 1984 , 8756  };
static const signed short s_sin_table[9] = { 0 , 10950 , 6281 , -7347 , -10496 , 1327 , 11257 , 5130 , -8314  };
static const signed short s_cos_table[9] = { 11431 , 3278 , -9550 , -8756 , 4527 , 11353 , 1984 , -10215 , -7844  };
