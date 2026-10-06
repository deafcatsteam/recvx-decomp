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

/* Copies the driver's status block (SND_STATUS, 0x42 bytes) to dst, the IOP
 * memory that get_iopsnd_info() reads. */
void pc_snddrv_status(void *dst);

#endif
