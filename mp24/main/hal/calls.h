/*
 * mp24/main/hal/calls.h — voice-call control on the Quectel EG912U.
 *
 * Sits on top of hal/modem.h. Subscribes to the relevant call URCs
 * (RING, +CLIP, NO CARRIER, BUSY, NO ANSWER, +QIND for the alerting
 * tone) and maintains an authoritative call state. App code drives
 * the modem with calls_dial / calls_answer / calls_hangup, and
 * reacts to state changes via the registered event callback.
 *
 * Audio routing (PCM forwarding between the modem's I²S2 and the
 * MAX98357A speaker amp / SPH0645 MEMS mic on I²S1) lands in a
 * follow-up session (S-MP10b). The control plane works on its own
 * — you'll hear nothing through the speaker, but ATD/ATA/ATH and
 * the inbound RING flow are testable in isolation.
 *
 * Calling-number presentation (CLIP) is enabled in calls_init so
 * the RING URC is followed by +CLIP carrying the caller's number.
 * The number is exposed in the event payload and is also stored
 * as calls_remote_number() between events.
 *
 * Numbers are E.164 with leading '+' for outgoing dials. Quectel
 * accepts national numbers too; we don't enforce a format.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    CALL_IDLE,         /* no call in progress */
    CALL_DIALING,      /* outbound, modem dialling, before remote ring */
    CALL_RINGING_OUT,  /* outbound, remote phone alerting */
    CALL_RINGING_IN,   /* inbound, our phone alerting */
    CALL_ACTIVE,       /* media path established, audio flowing */
    CALL_TERMINATED,   /* just hung up; transient — auto-returns to IDLE */
} call_state_t;

typedef enum {
    CALL_EVT_STATE_CHANGED,
    CALL_EVT_INCOMING,      /* RING + CLIP arrived */
    CALL_EVT_CONNECTED,     /* call ACTIVE */
    CALL_EVT_HANGUP,        /* peer or self disconnected */
} call_event_t;

typedef struct {
    call_event_t event;
    call_state_t state;
    const char  *remote_number;   /* caller / callee, or "" if unknown */
} call_event_payload_t;

typedef void (*calls_event_cb_t)(const call_event_payload_t *evt,
                                 void *user);

const char *call_state_name(call_state_t s);

/* Subscribe to the call URCs (RING, +CLIP, NO CARRIER, BUSY, NO
 * ANSWER), enable CLIP, and reset state to IDLE. Blocks for the
 * modem to enter READY (caps at ready_wait_ms). */
esp_err_t calls_init(uint32_t ready_wait_ms);

/* Register / clear the event callback. The callback fires from a
 * dedicated event-pump task, so it may take time — but it's still
 * serialised with state transitions, so blocking too long delays
 * the next event. */
void calls_set_event_callback(calls_event_cb_t cb, void *user);

/* Outbound dial. number can be E.164 or national. Returns ESP_OK
 * when the ATD command has been accepted by the modem (i.e. the
 * dial started); does NOT wait for remote ring or pickup. Final
 * outcome arrives via the event callback. */
esp_err_t calls_dial(const char *number);

/* Answer the current incoming RING (CALL_RINGING_IN). No-op if
 * we're not currently ringing. */
esp_err_t calls_answer(void);

/* Hang up whatever call is in progress (any non-IDLE state). */
esp_err_t calls_hangup(void);

/* Current snapshot — lock-free atomic read. */
call_state_t calls_state(void);

/* Remote party number for the current/last call. Empty string if
 * never populated. Pointer valid for firmware lifetime. */
const char *calls_remote_number(void);

/* Counters for the dashboard. */
uint32_t calls_outbound_count(void);
uint32_t calls_inbound_count(void);
