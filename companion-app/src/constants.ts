// chars of hex text per program_chunk — see relay/README.md
export const CHUNK_SIZE = 4096;
// must match RELAY_PROGRAM_MAX_HEX_BYTES in firmware (06_relay_client.inl) —
// sized for a fully-packed Mega 2560 image (256KB flash, ~2.8x hex text
// expansion), now that the relay path streams chunks straight to LittleFS
// on the device instead of buffering them in RAM.
export const MAX_HEX_BYTES = 800 * 1024;
