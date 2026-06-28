#ifndef BPE_TELEPORT_H
#define BPE_TELEPORT_H

#define TELEPORT_MAX_SLOTS 10

typedef struct {
    float x, y, z;
    char label[32];
    int active;  /* 1 if this slot has a saved position */
} TeleportSlot;

/** Initialize teleport system (find player entity, load saved slots) */
void teleport_init(void);

/** Save current position to slot index */
void teleport_save(int slot);

/** Teleport to saved slot position */
void teleport_load(int slot);

/** Get current player position (reads from game memory) */
int teleport_get_current_pos(float *x, float *y, float *z);

/** Set player position (writes to game memory) */
int teleport_set_pos(float x, float y, float z);

/** Get slot data (read-only) */
const TeleportSlot* teleport_get_slot(int slot);

/** Set slot label */
void teleport_set_label(int slot, const char *label);

/** Serialization for config save/load */
void teleport_serialize(void);
void teleport_deserialize(void);

#endif
