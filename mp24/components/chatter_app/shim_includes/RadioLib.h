/*
 * mp24/components/chatter_app/shim_includes/RadioLib.h
 *
 * Stand-in for the RadioLib LoRa-transceiver library. The real
 * RadioLib isn't vendored on MP2.4 — Decision B replaced the
 * SX1278/LLCC68 LoRa radio with a Quectel EG912U-GL cellular
 * modem (UART). The upstream Chatter firmware's LoRaService.h
 * still has 'LLCC68 radio;' as a private member, so any TU that
 * includes LoRaService.h needs LLCC68 to be a complete type.
 *
 * This file provides the minimal definition: an empty class
 * with a default constructor. Sufficient for the header to
 * compile. The stubbed LoRaService.cpp never actually calls any
 * RadioLib methods, so no further surface is needed.
 *
 * If a future SMS-adapter layer (S-MP15) wants to live behind
 * LoRaService.h for the sake of consuming code that uses
 * 'LoRa.send(...)', the adapter would still use this stub —
 * the real RadioLib is irrelevant.
 */
#ifndef MP24_SHIM_RADIOLIB_H
#define MP24_SHIM_RADIOLIB_H

class LLCC68 {
public:
    LLCC68() {}
};

#endif /* MP24_SHIM_RADIOLIB_H */
