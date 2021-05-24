#define CTL_CMD		0
#define CTL_ALIAS	2
#define CTL_GATEWAY CTL_ALIAS
#define NAME_HOST   "previous"
#define NAME_DOMAIN ".local"
#define FQDN_HOST   "previous" NAME_DOMAIN
#define CTL_HOST    15
#define CTL_DNS		3
#define FQDN_DNS    "dns" NAME_DOMAIN
#define CTL_NFSD    254
#define FQDN_NFSD   "shared" NAME_DOMAIN

#define CTL_NET          0x0A000200 //10.0.2.0
#define CTL_NET_MASK     0xFFFFFF00 //255.255.255.0
#define CTL_CLASS_MASK(x)   (((x & 0x80000000) == 0x00000000) ? 0xFF000000 : \
                             ((x & 0xC0000000) == 0x80000000) ? 0xFFFF0000 : \
                                                                0xFFFFFF00 )
