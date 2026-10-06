/* PC stand-in for the IOP sound driver (see pc_snddrv.c). */
#ifndef PC_SNDDRV_H
#define PC_SNDDRV_H

/* What the driver sees of IOP memory (pc_iop.c). */
typedef struct {
    const unsigned char *ram;
    unsigned int ram_size;
    unsigned int last_dma_addr, last_dma_size; /* the last block received */
    unsigned int data_buff; /* where the game sends bank pieces */
} PcIopView;

/* Starts the driver: its sound is mixed from then on. */
void pc_snddrv_init(void);

/* Handles a request stream sent by SdrSendReq(). */
void pc_snddrv_requests(const unsigned char *buf, int size, const PcIopView *iop);

/* 3D sound: where the sound of the next request for effect slot ch of
 * bank port (midi = 0), or for sequence port port (midi = 1), is, seen from
 * the camera (x right, y up, z forward); NULL: the game gives no position.
 * Called by the game (ps2_sg_sd.c) just before it queues that request. */
void pc_snddrv_dir(int midi, int port, int ch, const float *dir);

/* Copies the driver's status block (SND_STATUS, 0x42 bytes) to dst, the IOP
 * memory that get_iopsnd_info() reads. */
void pc_snddrv_status(void *dst);

#endif
