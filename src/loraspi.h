/*
 * loraspi.h  —  Native SPI LoRa driver for Dire Wolf.
 *
 * Public interface for loraspi.c.  Handles SX1276 and SX1262 LoRa modules
 * connected directly to a Raspberry Pi (or compatible SBC) via SPI and GPIO.
 *
 * Called from direwolf.c at startup when a CHANNEL with MODEM LORA is
 * configured.  Packets received over LoRa are injected into the normal
 * Dire Wolf frame queue.  Outgoing packets are routed here from tq.c.
 */

#pragma once

#include "audio.h"	// struct audio_s

/*
 * Chip type codes stored in audio_s.lora_chip[].
 */
#define LORA_CHIP_NONE   0
#define LORA_CHIP_SX1276 1   /* Also SX1278, RFM95W, RFM98W */
#define LORA_CHIP_SX1262 2   /* Also SX1268 — identical command set */

/*
 * loraspi_init  —  Start native SPI LoRa driver threads.
 *
 * Called once at startup from direwolf.c for every channel whose
 * chan_medium is MEDIUM_LORA.  Safe to call when no LORA channels
 * are configured (returns immediately).
 *
 * pa  — pointer to the global audio/channel configuration.
 */
void loraspi_init (struct audio_s *pa);

/*
 * loraspi_send_packet  —  Transmit a packet over LoRa.
 *
 * Called from tq.c when a packet is queued for a MEDIUM_LORA channel.
 * The packet is encoded as TNC2 text with the standard LoRa APRS preamble
 * (0x3C 0xFF 0x01) and transmitted via SPI.
 *
 * chan  — Dire Wolf channel number (must be MEDIUM_LORA).
 * pp    — AX.25 packet object.  Caller retains ownership.
 */
void loraspi_send_packet (int chan, packet_t pp);

/*
 * loraspi_apply_profile  —  Populate hardware fields in audio_s from a
 * named built-in profile (e.g. "lorapi_rfm95w").
 *
 * Called from config.c when the LORAHW directive is parsed.
 * Returns 0 on success, -1 if the profile name is unknown.
 *
 * chan  — channel index into audio_s arrays.
 * name  — profile name string (case-insensitive).
 * pa    — pointer to the audio/channel configuration.
 */
int loraspi_apply_profile (int chan, const char *name, struct audio_s *pa);
