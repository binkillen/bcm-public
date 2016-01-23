#include "../include/bcm4339.h"
#include "../include/wrapper.h"
#include "../include/structs.h"
#include "../include/helper.h"
#include "../include/types.h" /* needs to be included before bcmsdpcm.h */
#include "../include/bcmdhd/bcmsdpcm.h"
#include "../include/bcmdhd/bcmcdc.h"

#define SDIO_SEQ_NUM 0x108

extern void *dngl_sendpkt_alternative(void *sdio, void *p, int chan);

unsigned char bdc_ethernet_ip_udp_header_array[] = {
  0x00, 0x00, 0x00, 0x00,		/* BDC Header */
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,	/* ETHERNET: Destination MAC Address */
  'N', 'E', 'X', 'M', 'O', 'N',		/* ETHERNET: Source MAC Address */
  0x80, 0x00,				/* ETHERNET: Type */
  0x45,					/* IP: Version / IHL */
  0x00,					/* IP: DSCP / ECN */
  0x00, 0x00,				/* IP: Total Length */
  0x00, 0x00,				/* IP: Identification */
  0x00, 0x00,				/* IP: Flags / Fragment Offset */
  0x10,					/* IP: Time to Live (TTL) */
  0x88,					/* IP: Protocol = UDPLITE */
  0x00, 0x00,				/* IP: Header Checksum */
  0xFF, 0xFF, 0xFF, 0xFF,		/* IP: Source IP */
  0xFF, 0xFF, 0xFF, 0xFF,		/* IP: Destination IP */
  0xD6, 0xD8,				/* UDPLITE: Source Port = 55000 */
  0xD6, 0xD8,				/* UDPLITE: Destination Port = 55000 */
  0x00, 0x08,				/* UDPLITE: Checksum Coverage */
  0x52, 0x46,				/* UDPLITE: Checksum */
};

unsigned char bdc_ethernet_ipv6_udp_header_array[] = {
  0x20, 0x00, 0x00, 0x00,		/* BDC Header */
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,	/* ETHERNET: Destination MAC Address */
  'N', 'E', 'X', 'M', 'O', 'N',		/* ETHERNET: Source MAC Address */
  0x86, 0xDD,				/* ETHERNET: Type */
  0x60, 0x00, 0x00, 0x00,		/* IPv6: Version / Traffic Class / Flow Label */
  0x00, 0x08,				/* IPv6: Payload Length */
  0x88,					/* IPv6: Next Header = UDPLite */
  0x01,					/* IPv6: Hop Limit */
  0xFF, 0x02, 0x00, 0x00,		/* IPv6: Source IP */
  0x00, 0x00, 0x00, 0x00,		/* IPv6: Source IP */
  0x00, 0x00, 0x00, 0x00,		/* IPv6: Source IP */
  0x00, 0x00, 0x00, 0x01,		/* IPv6: Source IP */
  0xFF, 0x02, 0x00, 0x00,		/* IPv6: Destination IP */
  0x00, 0x00, 0x00, 0x00,		/* IPv6: Destination IP */
  0x00, 0x00, 0x00, 0x00,		/* IPv6: Destination IP */
  0x00, 0x00, 0x00, 0x01,		/* IPv6: Destination IP */
  0xD6, 0xD8,				/* UDPLITE: Source Port = 55000 */
  0xD6, 0xD8,				/* UDPLITE: Destination Port = 55000 */
  0x00, 0x08,				/* UDPLITE: Checksum Coverage */
  0x52, 0x46,				/* UDPLITE: Checksum only over UDPLITE header*/
};

struct ethernet_header {
	uint8 dst[6];
	uint8 src[6];
	uint16 type;
} __attribute__((packed));

struct ipv6_header {
	uint32 version : 4;
	uint32 traffic_class : 8;
	uint32 flow_label : 20;
	uint16 payload_length;
	uint8 next_header;
	uint8 hop_limit;
	uint8 src_ip[16];
	uint8 dst_ip[16];
} __attribute__((packed));

struct ip_header {
	uint8 version : 4;
	uint8 ihl : 4;
	uint8 dscp : 6;
	uint8 ecn : 2;
	uint16 total_length;
	uint16 identification;
	uint16 flags : 3;
	uint16 fragment_offset : 13;
	uint8 ttl;
	uint8 protocol;
	uint16 header_checksum;
	union {
		uint32 integer;
		uint8 array[4];
	} src_ip;
	union {
		uint32 integer;
		uint8 array[4];
	} dst_ip;
} __attribute__((packed));

struct udp_header {
	uint16 src_port;
	uint16 dst_port;
	union {
		uint16 length;			/* UDP: length of UDP header and payload */
		uint16 checksum_coverage;	/* UDPLITE: checksum_coverage */
	} len_chk_cov;
	uint16 checksum;
} __attribute__((packed));

struct bdc_ethernet_ip_udp_header {
	struct bdc_header bdc;
	struct ethernet_header ethernet;
	struct ip_header ip;
	struct udp_header udp;
} __attribute__((packed));

struct bdc_ethernet_ipv6_udp_header {
	struct bdc_header bdc;
	struct ethernet_header ethernet;
	struct ipv6_header ipv6;
	struct udp_header udp;
	uint8 payload[1];
} __attribute__((packed));

struct nexmon_header {
	void *hooked_fct;			/* Address of hooked function */
	uint32 stack_trace_offset;		/* Offset to beginning of stack trace */
	uint8 stack_trace[1];
} __attribute__((packed));

void
*dma_txfast_hook(void *di, struct sk_buff *p, int commit)
{
	struct sk_buff *p1 = 0;
	struct osl_info *osh = OSL_INFO_ADDR;
	void *ret = 0;
	unsigned char *sdio = (unsigned char *) SDIO_INFO_ADDR;
	struct bdc_ethernet_ipv6_udp_header *hdr;
	struct nexmon_header *nexmon_hdr;

	int copy_size = BOTTOM_OF_STACK - (int) get_stack_ptr();

	p1 = pkt_buf_get_skb(osh, sizeof(struct bdc_ethernet_ipv6_udp_header) - 1 + sizeof(struct nexmon_header) - 1 + copy_size);

	// copy headers to target buffer
	memcpy(p1->data, bdc_ethernet_ipv6_udp_header_array, sizeof(bdc_ethernet_ipv6_udp_header_array));

	hdr = p1->data;
	hdr->ipv6.payload_length = htons(sizeof(struct udp_header) + sizeof(struct nexmon_header) - 1 + copy_size);
	nexmon_hdr = (struct nexmon_header *) hdr->payload;

	nexmon_hdr->hooked_fct = &dma_txfast;
	nexmon_hdr->stack_trace_offset = 0;

	copy_stack(nexmon_hdr->stack_trace, copy_size);

	// We transmit the original frame first, as it already contains the correct sequence number
	ret = dma_txfast(di, p, commit);

	// Now we check, if an SDIO DMA transfer was done and fix the sequence number that is increased automatically in the dngl_sendpkt function
	if (di == DMA_INFO_SDIODEV_ADDR && ((unsigned char *) p->data)[4] == sdio[SDIO_SEQ_NUM]) {
		sdio[SDIO_SEQ_NUM]++;
		dngl_sendpkt_alternative(SDIO_INFO_ADDR, p1, SDPCM_DATA_CHANNEL);
		sdio[SDIO_SEQ_NUM]--;
	} else {
		dngl_sendpkt_alternative(SDIO_INFO_ADDR, p1, SDPCM_DATA_CHANNEL);
	}

	printf("X %08x %08x %08x %d\n", (unsigned int) di, (unsigned int) p, (unsigned int) p->data, p->len);

	return ret;
}