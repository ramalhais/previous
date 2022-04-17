//
//  DiskImage.cpp
//  Previous
//
//  Created by Simon Schubiger on 03.03.19.
//

#include "DiskImage.h"

#include <iostream>
#include <cstring>

/* Pull in ntohs()/ntohl()/htons()/htonl() declarations... shotgun approach */
#if defined(linux)
    /* netinet/in.h doesn't have proper extern "C" declarations for these... may also apply to other Unices */
    extern "C" uint32_t ntohl(uint32_t);
    extern "C" uint16_t ntohs(uint16_t);
    extern "C" uint32_t htonl(uint32_t);
    extern "C" uint16_t htons(uint16_t);
#else
    #if HAVE_ARPA_INET_H
        #include <arpa/inet.h>
    #endif
    #if HAVE_NETINET_IN_H
        #include <netinet/in.h>
    #endif
    #if HAVE_WINSOCK_H
        #include <winsock.h>
    #endif
#endif

using namespace std;

uint32_t fsv(uint32_t v) { return ntohl(v); }
uint16_t fsv(uint16_t v) { return ntohs(v); }

int16_t  fsv(int16_t v)  { return ntohs(v); }
int32_t  fsv(int32_t v)  { return ntohl(v); }

DiskImage::DiskImage(const string& path)
: imf(path, ios::binary | ios::in)
, path(path)
, diskOffset(0)
, blockSize(BLOCKSZ)
, sectorSize(0)
, rsDecode(false) {
    if(!(imf)) {
        error = strerror(errno);
        return;
    }
    
    read(0, sizeof(dl), &dl);
    if(
        strncmp(dl.dl_version, "NeXT", 4) &&
        strncmp(dl.dl_version, "dlV2", 4) &&
        strncmp(dl.dl_version, "dlV3", 4)
        ) {
            diskOffset = MO_BLOCK0;
            blockSize  = MO_BLOCKSZ;
            rsDecode   = true;
            read(0, sizeof(dl), &dl);
            if(
               strncmp(dl.dl_version, "NeXT", 4) &&
               strncmp(dl.dl_version, "dlV2", 4) &&
               strncmp(dl.dl_version, "dlV3", 4)
               ) {
                   cout << "Unknown version: " << dl.dl_version << endl;
                   exit(1);
               }
        }
    sectorSize = fsv(dl.dl_dt.d_secsize);
    if(sectorSize != 0x400) {
        cout << "Unsupported sector size: " << fsv(dl.dl_size);
        exit(1);
    }
    for(int p = 0; p < NPART; p++) {
        if(fsv(dl.dl_dt.d_partitions[p].p_bsize) == 0 || fsv(dl.dl_dt.d_partitions[p].p_bsize) == ~0)
            continue;
        parts.push_back(Partition(p, parts.size(), this, &dl, dl.dl_dt.d_partitions[p]));
    }
}

/* Partial remainders, e.g. t_rem[i] = (i*x^4) % ((x-1)*(x-2)*(x-4)*(x-8)) */
const uint32_t t_rem[256] = {
    0x00000000, 0x0f367840, 0x1e6cf080, 0x115a88c0, 0x3cd8fd1d, 0x33ee855d, 0x22b40d9d, 0x2d8275dd,
    0x78ade73a, 0x779b9f7a, 0x66c117ba, 0x69f76ffa, 0x44751a27, 0x4b436267, 0x5a19eaa7, 0x552f92e7,
    0xf047d374, 0xff71ab34, 0xee2b23f4, 0xe11d5bb4, 0xcc9f2e69, 0xc3a95629, 0xd2f3dee9, 0xddc5a6a9,
    0x88ea344e, 0x87dc4c0e, 0x9686c4ce, 0x99b0bc8e, 0xb432c953, 0xbb04b113, 0xaa5e39d3, 0xa5684193,
    0xfd8ebbe8, 0xf2b8c3a8, 0xe3e24b68, 0xecd43328, 0xc15646f5, 0xce603eb5, 0xdf3ab675, 0xd00cce35,
    0x85235cd2, 0x8a152492, 0x9b4fac52, 0x9479d412, 0xb9fba1cf, 0xb6cdd98f, 0xa797514f, 0xa8a1290f,
    0x0dc9689c, 0x02ff10dc, 0x13a5981c, 0x1c93e05c, 0x31119581, 0x3e27edc1, 0x2f7d6501, 0x204b1d41,
    0x75648fa6, 0x7a52f7e6, 0x6b087f26, 0x643e0766, 0x49bc72bb, 0x468a0afb, 0x57d0823b, 0x58e6fa7b,
    0xe7016bcd, 0xe837138d, 0xf96d9b4d, 0xf65be30d, 0xdbd996d0, 0xd4efee90, 0xc5b56650, 0xca831e10,
    0x9fac8cf7, 0x909af4b7, 0x81c07c77, 0x8ef60437, 0xa37471ea, 0xac4209aa, 0xbd18816a, 0xb22ef92a,
    0x1746b8b9, 0x1870c0f9, 0x092a4839, 0x061c3079, 0x2b9e45a4, 0x24a83de4, 0x35f2b524, 0x3ac4cd64,
    0x6feb5f83, 0x60dd27c3, 0x7187af03, 0x7eb1d743, 0x5333a29e, 0x5c05dade, 0x4d5f521e, 0x42692a5e,
    0x1a8fd025, 0x15b9a865, 0x04e320a5, 0x0bd558e5, 0x26572d38, 0x29615578, 0x383bddb8, 0x370da5f8,
    0x6222371f, 0x6d144f5f, 0x7c4ec79f, 0x7378bfdf, 0x5efaca02, 0x51ccb242, 0x40963a82, 0x4fa042c2,
    0xeac80351, 0xe5fe7b11, 0xf4a4f3d1, 0xfb928b91, 0xd610fe4c, 0xd926860c, 0xc87c0ecc, 0xc74a768c,
    0x9265e46b, 0x9d539c2b, 0x8c0914eb, 0x833f6cab, 0xaebd1976, 0xa18b6136, 0xb0d1e9f6, 0xbfe791b6,
    0xd302d687, 0xdc34aec7, 0xcd6e2607, 0xc2585e47, 0xefda2b9a, 0xe0ec53da, 0xf1b6db1a, 0xfe80a35a,
    0xabaf31bd, 0xa49949fd, 0xb5c3c13d, 0xbaf5b97d, 0x9777cca0, 0x9841b4e0, 0x891b3c20, 0x862d4460,
    0x234505f3, 0x2c737db3, 0x3d29f573, 0x321f8d33, 0x1f9df8ee, 0x10ab80ae, 0x01f1086e, 0x0ec7702e,
    0x5be8e2c9, 0x54de9a89, 0x45841249, 0x4ab26a09, 0x67301fd4, 0x68066794, 0x795cef54, 0x766a9714,
    0x2e8c6d6f, 0x21ba152f, 0x30e09def, 0x3fd6e5af, 0x12549072, 0x1d62e832, 0x0c3860f2, 0x030e18b2,
    0x56218a55, 0x5917f215, 0x484d7ad5, 0x477b0295, 0x6af97748, 0x65cf0f08, 0x749587c8, 0x7ba3ff88,
    0xdecbbe1b, 0xd1fdc65b, 0xc0a74e9b, 0xcf9136db, 0xe2134306, 0xed253b46, 0xfc7fb386, 0xf349cbc6,
    0xa6665921, 0xa9502161, 0xb80aa9a1, 0xb73cd1e1, 0x9abea43c, 0x9588dc7c, 0x84d254bc, 0x8be42cfc,
    0x3403bd4a, 0x3b35c50a, 0x2a6f4dca, 0x2559358a, 0x08db4057, 0x07ed3817, 0x16b7b0d7, 0x1981c897,
    0x4cae5a70, 0x43982230, 0x52c2aaf0, 0x5df4d2b0, 0x7076a76d, 0x7f40df2d, 0x6e1a57ed, 0x612c2fad,
    0xc4446e3e, 0xcb72167e, 0xda289ebe, 0xd51ee6fe, 0xf89c9323, 0xf7aaeb63, 0xe6f063a3, 0xe9c61be3,
    0xbce98904, 0xb3dff144, 0xa2857984, 0xadb301c4, 0x80317419, 0x8f070c59, 0x9e5d8499, 0x916bfcd9,
    0xc98d06a2, 0xc6bb7ee2, 0xd7e1f622, 0xd8d78e62, 0xf555fbbf, 0xfa6383ff, 0xeb390b3f, 0xe40f737f,
    0xb120e198, 0xbe1699d8, 0xaf4c1118, 0xa07a6958, 0x8df81c85, 0x82ce64c5, 0x9394ec05, 0x9ca29445,
    0x39cad5d6, 0x36fcad96, 0x27a62556, 0x28905d16, 0x051228cb, 0x0a24508b, 0x1b7ed84b, 0x1448a00b,
    0x416732ec, 0x4e514aac, 0x5f0bc26c, 0x503dba2c, 0x7dbfcff1, 0x7289b7b1, 0x63d33f71, 0x6ce54731,
};

const uint8_t t_exp[768] = {
    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1d, 0x3a, 0x74, 0xe8, 0xcd, 0x87, 0x13, 0x26,
    0x4c, 0x98, 0x2d, 0x5a, 0xb4, 0x75, 0xea, 0xc9, 0x8f, 0x03, 0x06, 0x0c, 0x18, 0x30, 0x60, 0xc0,
    0x9d, 0x27, 0x4e, 0x9c, 0x25, 0x4a, 0x94, 0x35, 0x6a, 0xd4, 0xb5, 0x77, 0xee, 0xc1, 0x9f, 0x23,
    0x46, 0x8c, 0x05, 0x0a, 0x14, 0x28, 0x50, 0xa0, 0x5d, 0xba, 0x69, 0xd2, 0xb9, 0x6f, 0xde, 0xa1,
    0x5f, 0xbe, 0x61, 0xc2, 0x99, 0x2f, 0x5e, 0xbc, 0x65, 0xca, 0x89, 0x0f, 0x1e, 0x3c, 0x78, 0xf0,
    0xfd, 0xe7, 0xd3, 0xbb, 0x6b, 0xd6, 0xb1, 0x7f, 0xfe, 0xe1, 0xdf, 0xa3, 0x5b, 0xb6, 0x71, 0xe2,
    0xd9, 0xaf, 0x43, 0x86, 0x11, 0x22, 0x44, 0x88, 0x0d, 0x1a, 0x34, 0x68, 0xd0, 0xbd, 0x67, 0xce,
    0x81, 0x1f, 0x3e, 0x7c, 0xf8, 0xed, 0xc7, 0x93, 0x3b, 0x76, 0xec, 0xc5, 0x97, 0x33, 0x66, 0xcc,
    0x85, 0x17, 0x2e, 0x5c, 0xb8, 0x6d, 0xda, 0xa9, 0x4f, 0x9e, 0x21, 0x42, 0x84, 0x15, 0x2a, 0x54,
    0xa8, 0x4d, 0x9a, 0x29, 0x52, 0xa4, 0x55, 0xaa, 0x49, 0x92, 0x39, 0x72, 0xe4, 0xd5, 0xb7, 0x73,
    0xe6, 0xd1, 0xbf, 0x63, 0xc6, 0x91, 0x3f, 0x7e, 0xfc, 0xe5, 0xd7, 0xb3, 0x7b, 0xf6, 0xf1, 0xff,
    0xe3, 0xdb, 0xab, 0x4b, 0x96, 0x31, 0x62, 0xc4, 0x95, 0x37, 0x6e, 0xdc, 0xa5, 0x57, 0xae, 0x41,
    0x82, 0x19, 0x32, 0x64, 0xc8, 0x8d, 0x07, 0x0e, 0x1c, 0x38, 0x70, 0xe0, 0xdd, 0xa7, 0x53, 0xa6,
    0x51, 0xa2, 0x59, 0xb2, 0x79, 0xf2, 0xf9, 0xef, 0xc3, 0x9b, 0x2b, 0x56, 0xac, 0x45, 0x8a, 0x09,
    0x12, 0x24, 0x48, 0x90, 0x3d, 0x7a, 0xf4, 0xf5, 0xf7, 0xf3, 0xfb, 0xeb, 0xcb, 0x8b, 0x0b, 0x16,
    0x2c, 0x58, 0xb0, 0x7d, 0xfa, 0xe9, 0xcf, 0x83, 0x1b, 0x36, 0x6c, 0xd8, 0xad, 0x47, 0x8e, 0x01,
    0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1d, 0x3a, 0x74, 0xe8, 0xcd, 0x87, 0x13, 0x26, 0x4c,
    0x98, 0x2d, 0x5a, 0xb4, 0x75, 0xea, 0xc9, 0x8f, 0x03, 0x06, 0x0c, 0x18, 0x30, 0x60, 0xc0, 0x9d,
    0x27, 0x4e, 0x9c, 0x25, 0x4a, 0x94, 0x35, 0x6a, 0xd4, 0xb5, 0x77, 0xee, 0xc1, 0x9f, 0x23, 0x46,
    0x8c, 0x05, 0x0a, 0x14, 0x28, 0x50, 0xa0, 0x5d, 0xba, 0x69, 0xd2, 0xb9, 0x6f, 0xde, 0xa1, 0x5f,
    0xbe, 0x61, 0xc2, 0x99, 0x2f, 0x5e, 0xbc, 0x65, 0xca, 0x89, 0x0f, 0x1e, 0x3c, 0x78, 0xf0, 0xfd,
    0xe7, 0xd3, 0xbb, 0x6b, 0xd6, 0xb1, 0x7f, 0xfe, 0xe1, 0xdf, 0xa3, 0x5b, 0xb6, 0x71, 0xe2, 0xd9,
    0xaf, 0x43, 0x86, 0x11, 0x22, 0x44, 0x88, 0x0d, 0x1a, 0x34, 0x68, 0xd0, 0xbd, 0x67, 0xce, 0x81,
    0x1f, 0x3e, 0x7c, 0xf8, 0xed, 0xc7, 0x93, 0x3b, 0x76, 0xec, 0xc5, 0x97, 0x33, 0x66, 0xcc, 0x85,
    0x17, 0x2e, 0x5c, 0xb8, 0x6d, 0xda, 0xa9, 0x4f, 0x9e, 0x21, 0x42, 0x84, 0x15, 0x2a, 0x54, 0xa8,
    0x4d, 0x9a, 0x29, 0x52, 0xa4, 0x55, 0xaa, 0x49, 0x92, 0x39, 0x72, 0xe4, 0xd5, 0xb7, 0x73, 0xe6,
    0xd1, 0xbf, 0x63, 0xc6, 0x91, 0x3f, 0x7e, 0xfc, 0xe5, 0xd7, 0xb3, 0x7b, 0xf6, 0xf1, 0xff, 0xe3,
    0xdb, 0xab, 0x4b, 0x96, 0x31, 0x62, 0xc4, 0x95, 0x37, 0x6e, 0xdc, 0xa5, 0x57, 0xae, 0x41, 0x82,
    0x19, 0x32, 0x64, 0xc8, 0x8d, 0x07, 0x0e, 0x1c, 0x38, 0x70, 0xe0, 0xdd, 0xa7, 0x53, 0xa6, 0x51,
    0xa2, 0x59, 0xb2, 0x79, 0xf2, 0xf9, 0xef, 0xc3, 0x9b, 0x2b, 0x56, 0xac, 0x45, 0x8a, 0x09, 0x12,
    0x24, 0x48, 0x90, 0x3d, 0x7a, 0xf4, 0xf5, 0xf7, 0xf3, 0xfb, 0xeb, 0xcb, 0x8b, 0x0b, 0x16, 0x2c,
    0x58, 0xb0, 0x7d, 0xfa, 0xe9, 0xcf, 0x83, 0x1b, 0x36, 0x6c, 0xd8, 0xad, 0x47, 0x8e, 0x01, 0x02,
    0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1d, 0x3a, 0x74, 0xe8, 0xcd, 0x87, 0x13, 0x26, 0x4c, 0x98,
    0x2d, 0x5a, 0xb4, 0x75, 0xea, 0xc9, 0x8f, 0x03, 0x06, 0x0c, 0x18, 0x30, 0x60, 0xc0, 0x9d, 0x27,
    0x4e, 0x9c, 0x25, 0x4a, 0x94, 0x35, 0x6a, 0xd4, 0xb5, 0x77, 0xee, 0xc1, 0x9f, 0x23, 0x46, 0x8c,
    0x05, 0x0a, 0x14, 0x28, 0x50, 0xa0, 0x5d, 0xba, 0x69, 0xd2, 0xb9, 0x6f, 0xde, 0xa1, 0x5f, 0xbe,
    0x61, 0xc2, 0x99, 0x2f, 0x5e, 0xbc, 0x65, 0xca, 0x89, 0x0f, 0x1e, 0x3c, 0x78, 0xf0, 0xfd, 0xe7,
    0xd3, 0xbb, 0x6b, 0xd6, 0xb1, 0x7f, 0xfe, 0xe1, 0xdf, 0xa3, 0x5b, 0xb6, 0x71, 0xe2, 0xd9, 0xaf,
    0x43, 0x86, 0x11, 0x22, 0x44, 0x88, 0x0d, 0x1a, 0x34, 0x68, 0xd0, 0xbd, 0x67, 0xce, 0x81, 0x1f,
    0x3e, 0x7c, 0xf8, 0xed, 0xc7, 0x93, 0x3b, 0x76, 0xec, 0xc5, 0x97, 0x33, 0x66, 0xcc, 0x85, 0x17,
    0x2e, 0x5c, 0xb8, 0x6d, 0xda, 0xa9, 0x4f, 0x9e, 0x21, 0x42, 0x84, 0x15, 0x2a, 0x54, 0xa8, 0x4d,
    0x9a, 0x29, 0x52, 0xa4, 0x55, 0xaa, 0x49, 0x92, 0x39, 0x72, 0xe4, 0xd5, 0xb7, 0x73, 0xe6, 0xd1,
    0xbf, 0x63, 0xc6, 0x91, 0x3f, 0x7e, 0xfc, 0xe5, 0xd7, 0xb3, 0x7b, 0xf6, 0xf1, 0xff, 0xe3, 0xdb,
    0xab, 0x4b, 0x96, 0x31, 0x62, 0xc4, 0x95, 0x37, 0x6e, 0xdc, 0xa5, 0x57, 0xae, 0x41, 0x82, 0x19,
    0x32, 0x64, 0xc8, 0x8d, 0x07, 0x0e, 0x1c, 0x38, 0x70, 0xe0, 0xdd, 0xa7, 0x53, 0xa6, 0x51, 0xa2,
    0x59, 0xb2, 0x79, 0xf2, 0xf9, 0xef, 0xc3, 0x9b, 0x2b, 0x56, 0xac, 0x45, 0x8a, 0x09, 0x12, 0x24,
    0x48, 0x90, 0x3d, 0x7a, 0xf4, 0xf5, 0xf7, 0xf3, 0xfb, 0xeb, 0xcb, 0x8b, 0x0b, 0x16, 0x2c, 0x58,
    0xb0, 0x7d, 0xfa, 0xe9, 0xcf, 0x83, 0x1b, 0x36, 0x6c, 0xd8, 0xad, 0x47, 0x8e, 0x01, 0x02, 0x04,
};

const uint8_t t_log[256] = {
    0x00, 0x00, 0x01, 0x19, 0x02, 0x32, 0x1a, 0xc6, 0x03, 0xdf, 0x33, 0xee, 0x1b, 0x68, 0xc7, 0x4b,
    0x04, 0x64, 0xe0, 0x0e, 0x34, 0x8d, 0xef, 0x81, 0x1c, 0xc1, 0x69, 0xf8, 0xc8, 0x08, 0x4c, 0x71,
    0x05, 0x8a, 0x65, 0x2f, 0xe1, 0x24, 0x0f, 0x21, 0x35, 0x93, 0x8e, 0xda, 0xf0, 0x12, 0x82, 0x45,
    0x1d, 0xb5, 0xc2, 0x7d, 0x6a, 0x27, 0xf9, 0xb9, 0xc9, 0x9a, 0x09, 0x78, 0x4d, 0xe4, 0x72, 0xa6,
    0x06, 0xbf, 0x8b, 0x62, 0x66, 0xdd, 0x30, 0xfd, 0xe2, 0x98, 0x25, 0xb3, 0x10, 0x91, 0x22, 0x88,
    0x36, 0xd0, 0x94, 0xce, 0x8f, 0x96, 0xdb, 0xbd, 0xf1, 0xd2, 0x13, 0x5c, 0x83, 0x38, 0x46, 0x40,
    0x1e, 0x42, 0xb6, 0xa3, 0xc3, 0x48, 0x7e, 0x6e, 0x6b, 0x3a, 0x28, 0x54, 0xfa, 0x85, 0xba, 0x3d,
    0xca, 0x5e, 0x9b, 0x9f, 0x0a, 0x15, 0x79, 0x2b, 0x4e, 0xd4, 0xe5, 0xac, 0x73, 0xf3, 0xa7, 0x57,
    0x07, 0x70, 0xc0, 0xf7, 0x8c, 0x80, 0x63, 0x0d, 0x67, 0x4a, 0xde, 0xed, 0x31, 0xc5, 0xfe, 0x18,
    0xe3, 0xa5, 0x99, 0x77, 0x26, 0xb8, 0xb4, 0x7c, 0x11, 0x44, 0x92, 0xd9, 0x23, 0x20, 0x89, 0x2e,
    0x37, 0x3f, 0xd1, 0x5b, 0x95, 0xbc, 0xcf, 0xcd, 0x90, 0x87, 0x97, 0xb2, 0xdc, 0xfc, 0xbe, 0x61,
    0xf2, 0x56, 0xd3, 0xab, 0x14, 0x2a, 0x5d, 0x9e, 0x84, 0x3c, 0x39, 0x53, 0x47, 0x6d, 0x41, 0xa2,
    0x1f, 0x2d, 0x43, 0xd8, 0xb7, 0x7b, 0xa4, 0x76, 0xc4, 0x17, 0x49, 0xec, 0x7f, 0x0c, 0x6f, 0xf6,
    0x6c, 0xa1, 0x3b, 0x52, 0x29, 0x9d, 0x55, 0xaa, 0xfb, 0x60, 0x86, 0xb1, 0xbb, 0xcc, 0x3e, 0x5a,
    0xcb, 0x59, 0x5f, 0xb0, 0x9c, 0xa9, 0xa0, 0x51, 0x0b, 0xf5, 0x16, 0xeb, 0x7a, 0x75, 0x2c, 0xd7,
    0x4f, 0xae, 0xd5, 0xe9, 0xe6, 0xe7, 0xad, 0xe8, 0x74, 0xd6, 0xf4, 0xea, 0xa8, 0x50, 0x58, 0xaf,
};

static uint32_t ecc_block(const uint8_t* s, int ss) {
    int i;
    uint32_t r = (s[0] << 24) | (s[ss] << 16) | (s[2*ss] << 8) | s[3*ss];
    
    s += 4*ss;
    for(i=4; i<36; i++) {
        r = t_rem[r >> 24] ^ (r << 8);
        if(i < 32) {
            r ^= *s;
            s += ss;
        }
    }
    return r;
}

static int rs_decode_string(uint8_t* sector, int off, int step) {
    int i;
    uint32_t ecc = ecc_block(sector+off, step);
    uint32_t ref_ecc =
    (sector[off+32*step] << 24) |
    (sector[off+33*step] << 16) |
    (sector[off+34*step] << 8) |
    sector[off+35*step];
    if(ref_ecc == ecc)
        return 0;
    
    // Syndrome polynomial
    // syn[i] = value of the codebook polynom at 2**(i-1)
    uint8_t syn[5];
    memset(syn, 0, 5);
    syn[0] = 1;
    for(i=0; i<36; i++) {
        uint8_t v = sector[off+step*(35-i)];
        if(v) {
            syn[1] ^= v;
            int lv = t_log[v];
            syn[2] ^= t_exp[lv+i];
            syn[3] ^= t_exp[lv+2*i];
            syn[4] ^= t_exp[lv+3*i];
        }
    }
    
    // Berlekamp-Massey
    
    uint8_t sigma[5];
    uint8_t omega[5];
    uint8_t tau[5];
    uint8_t gamma[5];
    int d;
    int b;
    
    memset(sigma, 0, 5);
    memset(omega, 0, 5);
    memset(tau, 0, 5);
    memset(gamma, 0, 5);
    sigma[0] = 1;
    omega[0] = 1;
    tau[0] = 1;
    d = 0;
    b = 0;
    
    int l;
    for(l=1; l<5; l++) {
        // 1- Determine the l-order coefficient syn*sigma
        uint8_t delta = 0;
        for(i=0; i<=l; i++)
            if(sigma[i] && syn[l-i])
                delta ^= t_exp[t_log[sigma[i]] + t_log[syn[l-i]]];
        
        // 2- Select update method a/b
        int limit = (l+1)/2;
        int exact = l & 1;
        if(!delta || d > limit || (d == limit && exact && !b)) {
            // 2.1- Method a
            // b and d unchanged
            // tau and gamma multiplied by x
            // sigma = sigma - delta * tau
            // omega = omega - delta * gamma
            
            for(i=0; i<l; i++) {
                tau[l-i] = tau[l-i-1];
                gamma[l-i] = gamma[l-i-1];
            }
            tau[0] = gamma[0] = 0;
            
            if(delta) {
                uint8_t ldelta = t_log[delta];
                for(i=1; i<=l; i++) {
                    if(tau[i])
                        sigma[i] ^= t_exp[t_log[tau  [i]] + ldelta];
                    if(gamma[i])
                        omega[i] ^= t_exp[t_log[gamma[i]] + ldelta];
                }
            }
        } else {
            // 2.2- Method b
            d = l-d;
            b = !b;
            // tau(n+1)   = sigma(n) / delta
            // gamma(n+1) = omega(n) / delta
            // sigma(n+1) = sigma(n) - delta*x*tau(n)
            // omega(n+1) = omega(n) - delta*x*gamma(n)
            uint8_t ldelta = t_log[delta];
            uint8_t ildelta = ldelta ^ 255;
            for(i=l; i>0; i--) {
                if(tau[i-1])
                    sigma[i]   = sigma[i] ^ t_exp[t_log[tau  [i-1]] + ldelta];
                
                if(gamma[i-1])
                    omega[i]   = omega[i] ^ t_exp[t_log[gamma[i-1]] + ldelta];
                
                if(sigma[i-1])
                    tau[i-1]   = t_exp[t_log[sigma[i-1]] + ildelta];
                else
                    tau[i-1]   = 0x00;
                
                if(omega[i-1])
                    gamma[i-1] = t_exp[t_log[omega[i-1]] + ildelta];
                else
                    gamma[i-1]   = 0x00;
            }
        }
    }
    
    // Find the roots of sigma to get the error positions (they're the inverses of 2**position)
    // Compute the error(s)
    
    if(sigma[3] || sigma[4]) {
        // Should not happen
        return -1;
    }
    
    if(sigma[2] && sigma[0]) {
        int epos1, epos2;
        uint8_t ls1 = t_log[sigma[1]];
        uint8_t ls2 = t_log[sigma[2]];
        for(epos1 = 0; epos1 < 256; epos1++) {
            uint8_t res = sigma[0];
            if(sigma[1])
                res ^= t_exp[255-epos1+ls1];
            res ^= t_exp[2*(255-epos1)+ls2];
            if(!res)
                break;
        }
        if(epos1 > 35)
            return -1;
        
        // sigma = c.(x-1/r1)(x-1/r2) = c.x*x - c*x*(1/r1+1/r2) + c/(r1*r2)
        
        // s0 = s2/(r1*r2) -> r1*r2*s0 = s2 -> r1 = s2 / (r1*s0)
        // -> r2 = sigma[2]/(r1*sigma[0])
        epos2 = ls2 - epos1 - t_log[sigma[0]];
        if(epos2 < 0)
            epos2 += 255;
        if(epos2 > 35)
            return -1;
        
        uint8_t err1 = omega[0];
        if(omega[1])
            err1 ^= t_exp[t_log[omega[1]] + 255 - epos1];
        if(omega[2])
            err1 ^= t_exp[t_log[omega[2]] + 2*(255 - epos1)];
        err1 = t_log[err1] + epos1;
        uint8_t div = t_log[1 ^ t_exp[255 + epos2 - epos1]];
        err1 = t_exp[255 + err1 - div];
        
        uint8_t err2 = omega[0];
        if(omega[1])
            err2 ^= t_exp[t_log[omega[1]] + 255 - epos2];
        if(omega[2])
            err2 ^= t_exp[t_log[omega[2]] + 2*(255 - epos2)];
        err2 = t_log[err2] + epos2;
        div = t_log[1 ^ t_exp[255 + epos1 - epos2]];
        err2 = t_exp[255 + err2 - div];
        
        sector[off + step*(35-epos1)] ^= err1;
        sector[off + step*(35-epos2)] ^= err2;
        return 2;
        
    } else if(sigma[1] && sigma[0]) {
        // sigma = c.(x-1/r1) -> r1=sigma[1]/sigma[0]
        int epos = t_log[sigma[1]] - t_log[sigma[0]];
        
        if(epos > 35)
            return -1;
        
        uint8_t err = sigma[1]^omega[1];
        sector[off + step*(35-epos)] ^= err;
        return 1;
        
    } else {
        // Should not happen
        return -1;
    }
}

static int rs_decode(uint8_t* block) {
    int i,e;
    int ecount = 0;
    /* Decode rows */
    for(i=0; i<36; i++) {
        e = rs_decode_string(block, 36*i, 1);
        if(e!=-1) {
            ecount += e;
        }
    }
    /* Decode columns */
    for(i=0; i<32; i++) {
        e = rs_decode_string(block, i, 36);
        if(e==-1) {
            return -1; /* Uncorrectable */
        } else {
            ecount += e;
        }
    }
    /* Build decoded sector structure */
    for(i=1; i<32; i++)
        memmove(block+i*32, block+i*36, 32);
    
    return ecount;
}

ios_base::iostate DiskImage::read(streampos offset, streamsize size, void* data) {
    int64_t block     = offset / BLOCKSZ;
    int64_t blockOff  = offset % BLOCKSZ;
    ios_base::iostate result(ios_base::goodbit);
    char*   dataPtr   = (char*)data;
    char    buffer[blockSize];
    while(size > 0) {
        int64_t rdSize = std::min((int64_t)size, BLOCKSZ - blockOff);
        imf.seekg(block * blockSize + diskOffset, ios::beg);
        imf.read(buffer, blockSize);
        if(rsDecode)
            if(rs_decode((uint8_t*)buffer) < 0)
                memset(buffer, 0, blockSize);
        memcpy(dataPtr, buffer + blockOff, rdSize);
        blockOff = 0;
        size    -= rdSize;
        dataPtr += rdSize;
        block++;
        ios_base::iostate result = imf.rdstate();
        if(result != ios_base::goodbit) {
            cout << "Can't read " << size << " bytes at offset " << offset << endl;
            break;
        }
    }
    
     return result;
}

DiskImage::~DiskImage() {}

bool DiskImage::valid() {
    return error.empty();
}

ostream& operator<< (ostream& os, const DiskImage& im) {
    int64_t size = im.sectorSize;
    size *= fsv(im.dl.dl_dt.d_ntracks);
    size *= fsv(im.dl.dl_dt.d_nsectors);
    size *= fsv(im.dl.dl_dt.d_ncylinders);
    size >>= 20;
    os << "Disk '" << im.dl.dl_dt.d_name << "' '" << im.dl.dl_label << "' '" << im.dl.dl_dt.d_type << "' " << size << " MBytes" << endl;
    os << "  Sector size: " << im.sectorSize << " Bytes" << endl << endl;
    for(size_t p = 0; p < im.parts.size(); p++)
        os << "  " << im.parts[p] << endl;
    return os;
}

